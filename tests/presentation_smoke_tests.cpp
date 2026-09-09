// fastpdf_presentation_smoke_tests - end-to-end smoke of the Phase-4
// presentation pre-render path against a real generated multi-page PDF fixture.
//
// Presentation relies on the shared Phase-3 pipeline: the persistent
// serialized RenderWorker, the bounded coalescing priority scheduler, and the
// byte-bounded LRU CPU cache. This smoke drives that pipeline exactly as the
// app does when it enters presentation:
//   * opens a 4-page mixed-size fixture through the real worker + window loop
//   * computes the presentation pre-render set (previous/current/next/next+1)
//     via fastpdf::app::presentation::PreRenderPages
//   * submits final-quality render jobs for every page in the set
//   * verifies each completes with the correct page index and exact size
//   * verifies the "next" page is available (rendered) so a cached advance is
//     possible, and reports the render timing truthfully
//
// Requires a valid pinned PDFium artifact (FASTPDF_WITH_PDFIUM=ON).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <fastpdf/pdfium/PdfiumLibrary.h>
#include <fastpdf/renderer/render_key.h>

#include "Presentation.h"
#include "RenderWorker.h"
#include "test_harness.h"

namespace {

using fastpdf::app::RenderWorker;
using fastpdf::app::presentation::PreRenderPages;
using fastpdf::renderer::Quality;
using fastpdf::renderer::RenderKey;
using fastpdf::renderer::RenderPriority;

constexpr wchar_t kTestWindowClass[] = L"FastPDF.PresentationSmokeWindow";
constexpr UINT kDoneMessage = WM_APP + 20;

// Builds a minimal valid multi-page PDF with mixed page sizes (same structure
// as the adapter/worker tests use).
std::vector<std::uint8_t> MakeMixedSizePdf() {
    const std::pair<double, double> sizes[] = {
        {612.0, 792.0}, {792.0, 612.0}, {300.0, 400.0}, {1224.0, 1584.0}};
    const size_t pageCount = std::size(sizes);

    std::string pdf = "%PDF-1.4\n";
    std::vector<size_t> offsets;
    offsets.reserve(pageCount + 3);
    offsets.push_back(0);

    offsets.push_back(pdf.size());
    pdf += "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";

    offsets.push_back(pdf.size());
    std::string kids = "[";
    for (size_t i = 0; i < pageCount; ++i) {
        kids += std::to_string(3 + i) + " 0 R ";
    }
    kids += "]";
    pdf += "2 0 obj\n<< /Type /Pages /Kids " + kids + " /Count " +
           std::to_string(pageCount) + " >>\nendobj\n";

    for (size_t i = 0; i < pageCount; ++i) {
        offsets.push_back(pdf.size());
        char box[64]{};
        std::snprintf(box, sizeof(box), "[0 0 %.0f %.0f]", sizes[i].first,
                      sizes[i].second);
        pdf += std::to_string(3 + i) + " 0 obj\n<< /Type /Page /Parent 2 0 R " +
               "/MediaBox " + box + " /Resources << >> >>\nendobj\n";
    }

    const size_t xrefOffset = pdf.size();
    pdf += "xref\n0 " + std::to_string(offsets.size()) + "\n";
    pdf += "0000000000 65535 f \n";
    for (size_t i = 1; i < offsets.size(); ++i) {
        char entry[32]{};
        std::snprintf(entry, sizeof(entry), "%010zu 00000 n \n", offsets[i]);
        pdf += entry;
    }
    pdf += "trailer\n<< /Size " + std::to_string(offsets.size()) +
           " /Root 1 0 R >>\n";
    pdf += "startxref\n" + std::to_string(xrefOffset) + "\n%%EOF\n";

    return std::vector<std::uint8_t>(pdf.begin(), pdf.end());
}

std::wstring WriteBytesToTempFile(const std::vector<std::uint8_t>& bytes) {
    wchar_t tempDir[MAX_PATH]{};
    if (GetTempPathW(MAX_PATH, tempDir) == 0) {
        return {};
    }
    wchar_t tempFile[MAX_PATH]{};
    if (GetTempFileNameW(tempDir, L"fpdfp", 0, tempFile) == 0) {
        return {};
    }
    HANDLE file = CreateFileW(tempFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    DWORD written = 0;
    WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written,
              nullptr);
    CloseHandle(file);
    return std::wstring(tempFile);
}

struct TestWindow {
    HWND hwnd = nullptr;
    std::vector<RenderWorker::WorkerCompletion*> completions;

    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        if (msg == kDoneMessage) {
            auto* self = reinterpret_cast<TestWindow*>(
                GetWindowLongPtrW(h, GWLP_USERDATA));
            if (self != nullptr) {
                self->completions.push_back(
                    reinterpret_cast<RenderWorker::WorkerCompletion*>(l));
            }
            return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }

    bool Create() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &TestWindow::Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kTestWindowClass;
        if (RegisterClassExW(&wc) == 0 &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        hwnd = CreateWindowExW(0, kTestWindowClass, L"", WS_OVERLAPPED, 0, 0, 0,
                               0, nullptr, nullptr, wc.hInstance, this);
        if (hwnd == nullptr) {
            return false;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(this));
        return true;
    }

    size_t PumpUntil(size_t count, DWORD timeoutMs) {
        const DWORD start = GetTickCount();
        while (completions.size() < count) {
            MSG msg{};
            if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            } else if (GetTickCount() - start > timeoutMs) {
                break;
            } else {
                Sleep(1);
            }
        }
        return completions.size();
    }

    void FreeCompletions() {
        for (auto* completion : completions) {
            delete completion;
        }
        completions.clear();
    }
};

RenderWorker::RenderJob MakeJob(std::uint64_t docEpoch, std::uint64_t viewEpoch,
                                std::uint64_t jobId, int page, int width,
                                int height, RenderPriority priority) {
    RenderWorker::RenderJob job;
    job.key = RenderKey{docEpoch, page, width, height, 0, Quality::Final};
    job.priority = priority;
    job.viewEpoch = viewEpoch;
    job.jobId = jobId;
    return job;
}

} // namespace

FASTPDF_TEST(presentation_smoke_pre_renders_page_set_for_cached_advance) {
    const std::wstring path = WriteBytesToTempFile(MakeMixedSizePdf());
    FASTPDF_CHECK(!path.empty());

    TestWindow window;
    FASTPDF_CHECK(window.Create());

    RenderWorker worker;
    worker.Start(window.hwnd, kDoneMessage);
    worker.OpenDocument(1, path);
    FASTPDF_CHECK_EQ(window.PumpUntil(1, 5000), 1u);
    FASTPDF_CHECK(window.completions[0]->documentInfo.ok);
    FASTPDF_CHECK_EQ(window.completions[0]->documentInfo.pageCount, 4);
    window.FreeCompletions();

    worker.SetViewEpoch(1);

    // Presentation starts at page 1 (0-based). Pre-render the set the app
    // would request: prev(0), current(1), next(2), next+1(3).
    const int current = 1;
    const int pageCount = 4;
    const auto preRender = PreRenderPages(current, pageCount);
    FASTPDF_CHECK_EQ(preRender.size(), 4u);

    // A fixed presentation pixel size for all pages (Fit Page at a fixed
    // viewport); the exact size is not important, only that it is consistent
    // so the "next" page render is a cache-equivalent advance.
    const int width = 400;
    const int height = 500;

    std::uint64_t jobId = 1;
    for (const auto& [page, priority] : preRender) {
        worker.SubmitRender(MakeJob(1, 1, jobId++, page, width, height,
                                    static_cast<RenderPriority>(priority)));
    }

    // All four pre-render jobs must complete.
    FASTPDF_CHECK_EQ(window.PumpUntil(4, 5000), 4u);

    bool sawCurrent = false;
    bool sawNext = false;
    for (const auto* completion : window.completions) {
        FASTPDF_CHECK(completion->kind == RenderWorker::ResultKind::PageRender);
        FASTPDF_CHECK(completion->pageRender.ok);
        FASTPDF_CHECK_EQ(completion->key.width, width);
        FASTPDF_CHECK_EQ(completion->key.height, height);
        FASTPDF_CHECK_EQ(completion->pageRender.bitmap.width, width);
        FASTPDF_CHECK_EQ(completion->pageRender.bitmap.height, height);
        if (completion->key.pageIndex == current) {
            sawCurrent = true;
        }
        if (completion->key.pageIndex == current + 1) {
            sawNext = true;
        }
    }
    FASTPDF_CHECK(sawCurrent);
    FASTPDF_CHECK(sawNext);

    // Truthful timing evidence: report the render duration of the "next" page
    // (the page that would be shown on a cached advance). The pre-render set
    // guarantees it is already rasterized before the user advances.
    for (const auto* completion : window.completions) {
        if (completion->key.pageIndex == current + 1) {
            std::printf("presentation smoke: next page render took %.1f ms\n",
                        completion->pageRender.renderDurationMs);
        }
    }

    window.FreeCompletions();
    worker.Shutdown();
    DestroyWindow(window.hwnd);
    DeleteFileW(path.c_str());
}

int main() {
    fastpdf::pdfium::PdfiumLibrary library;
    if (!library.isAvailable()) {
        std::printf("fastpdf_presentation_smoke_tests: PDFium is not available.\n");
        return 2;
    }
    return fastpdf::test::RunAll();
}
