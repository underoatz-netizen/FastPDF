// fastpdf_worker_pipeline_tests - exercises the Phase-3 RenderWorker
// end-to-end against a real generated mixed-size PDF fixture:
//   * document open reports page count + per-page dimensions and keeps the
//     document open on the worker thread
//   * page render jobs return bitmaps at the exact requested size and echo the
//     document/view epoch and job id
//   * multiple pages render against the SAME persistent document (no re-open)
//   * preview (half-size) and final (full-size) renders return distinct sizes
//   * a job for a stale document epoch is dropped without a completion
// Requires a valid pinned PDFium artifact (FASTPDF_WITH_PDFIUM=ON).
//
// The worker posts completions to a real hidden window, so this test also
// exercises the cross-thread PostMessage handoff exactly as the app does.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <fastpdf/pdfium/PdfiumLibrary.h>
#include <fastpdf/renderer/render_key.h>

#include "RenderWorker.h"
#include "test_harness.h"

namespace {

using fastpdf::app::RenderWorker;
using fastpdf::renderer::PreviewSizeFor;
using fastpdf::renderer::Quality;
using fastpdf::renderer::RenderKey;
using fastpdf::renderer::RenderPriority;

constexpr wchar_t kTestWindowClass[] = L"FastPDF.WorkerPipelineTestWindow";
constexpr UINT kDoneMessage = WM_APP + 10;

// Builds a minimal valid multi-page PDF with mixed page sizes (same structure
// as the adapter tests use).
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
    if (GetTempFileNameW(tempDir, L"fpdfw", 0, tempFile) == 0) {
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

// A minimal hidden window that receives worker completions and records them.
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

    // Pumps messages until |count| completions have arrived or |timeoutMs|
    // elapses. Returns the number of completions received.
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
                                int height, Quality quality) {
    RenderWorker::RenderJob job;
    job.key = RenderKey{docEpoch, page, width, height, 0, quality};
    job.priority = RenderPriority::Visible;
    job.viewEpoch = viewEpoch;
    job.jobId = jobId;
    return job;
}

} // namespace

FASTPDF_TEST(worker_open_document_reports_page_count_and_sizes) {
    const std::wstring path = WriteBytesToTempFile(MakeMixedSizePdf());
    FASTPDF_CHECK(!path.empty());

    TestWindow window;
    FASTPDF_CHECK(window.Create());

    RenderWorker worker;
    worker.Start(window.hwnd, kDoneMessage);
    worker.OpenDocument(1, path);

    FASTPDF_CHECK_EQ(window.PumpUntil(1, 5000), 1u);
    const RenderWorker::WorkerCompletion* result = window.completions[0];
    FASTPDF_CHECK(result->kind == RenderWorker::ResultKind::DocumentInfo);
    FASTPDF_CHECK_EQ(result->docEpoch, 1u);
    FASTPDF_CHECK(result->documentInfo.ok);
    FASTPDF_CHECK_EQ(result->documentInfo.pageCount, 4);
    FASTPDF_CHECK_EQ(result->documentInfo.pages.size(), 4u);
    FASTPDF_CHECK_EQ(result->documentInfo.pages[0].widthPoints, 612.0);
    FASTPDF_CHECK_EQ(result->documentInfo.pages[1].widthPoints, 792.0);
    FASTPDF_CHECK_EQ(result->documentInfo.pages[2].widthPoints, 300.0);
    FASTPDF_CHECK_EQ(result->documentInfo.pages[3].widthPoints, 1224.0);

    window.FreeCompletions();
    worker.Shutdown();
    DestroyWindow(window.hwnd);
    DeleteFileW(path.c_str());
}

FASTPDF_TEST(worker_render_returns_bitmap_at_exact_size_and_echoes_identity) {
    const std::wstring path = WriteBytesToTempFile(MakeMixedSizePdf());
    FASTPDF_CHECK(!path.empty());

    TestWindow window;
    FASTPDF_CHECK(window.Create());

    RenderWorker worker;
    worker.Start(window.hwnd, kDoneMessage);
    worker.OpenDocument(3, path);
    FASTPDF_CHECK_EQ(window.PumpUntil(1, 5000), 1u);
    window.FreeCompletions();

    worker.SetViewEpoch(7);
    worker.SubmitRender(MakeJob(3, 7, 42, 2, 150, 200, Quality::Final));

    FASTPDF_CHECK_EQ(window.PumpUntil(1, 5000), 1u);
    const RenderWorker::WorkerCompletion* result = window.completions[0];
    FASTPDF_CHECK(result->kind == RenderWorker::ResultKind::PageRender);
    FASTPDF_CHECK_EQ(result->docEpoch, 3u);
    FASTPDF_CHECK_EQ(result->viewEpoch, 7u);
    FASTPDF_CHECK_EQ(result->jobId, 42u);
    FASTPDF_CHECK_EQ(result->key.pageIndex, 2);
    FASTPDF_CHECK_EQ(result->key.width, 150);
    FASTPDF_CHECK_EQ(result->key.height, 200);
    FASTPDF_CHECK(result->pageRender.ok);
    FASTPDF_CHECK_EQ(result->pageRender.pageIndex, 2);
    FASTPDF_CHECK_EQ(result->pageRender.bitmap.width, 150);
    FASTPDF_CHECK_EQ(result->pageRender.bitmap.height, 200);

    window.FreeCompletions();
    worker.Shutdown();
    DestroyWindow(window.hwnd);
    DeleteFileW(path.c_str());
}

FASTPDF_TEST(worker_persistent_document_renders_multiple_pages_without_reopen) {
    const std::wstring path = WriteBytesToTempFile(MakeMixedSizePdf());
    FASTPDF_CHECK(!path.empty());

    TestWindow window;
    FASTPDF_CHECK(window.Create());

    RenderWorker worker;
    worker.Start(window.hwnd, kDoneMessage);
    worker.OpenDocument(5, path);
    FASTPDF_CHECK_EQ(window.PumpUntil(1, 5000), 1u);
    window.FreeCompletions();

    worker.SetViewEpoch(1);

    // Render three different pages sequentially against the same open
    // document; each completes with the correct page and exact size.
    worker.SubmitRender(MakeJob(5, 1, 1, 0, 100, 120, Quality::Final));
    FASTPDF_CHECK_EQ(window.PumpUntil(1, 5000), 1u);
    FASTPDF_CHECK_EQ(window.completions[0]->pageRender.pageIndex, 0);
    FASTPDF_CHECK_EQ(window.completions[0]->pageRender.bitmap.width, 100);
    FASTPDF_CHECK_EQ(window.completions[0]->pageRender.bitmap.height, 120);
    window.FreeCompletions();

    worker.SubmitRender(MakeJob(5, 1, 2, 2, 90, 120, Quality::Final));
    FASTPDF_CHECK_EQ(window.PumpUntil(1, 5000), 1u);
    FASTPDF_CHECK_EQ(window.completions[0]->pageRender.pageIndex, 2);
    FASTPDF_CHECK_EQ(window.completions[0]->pageRender.bitmap.width, 90);
    FASTPDF_CHECK_EQ(window.completions[0]->pageRender.bitmap.height, 120);
    window.FreeCompletions();

    worker.SubmitRender(MakeJob(5, 1, 3, 3, 200, 200, Quality::Final));
    FASTPDF_CHECK_EQ(window.PumpUntil(1, 5000), 1u);
    FASTPDF_CHECK_EQ(window.completions[0]->pageRender.pageIndex, 3);
    FASTPDF_CHECK_EQ(window.completions[0]->pageRender.bitmap.width, 200);
    FASTPDF_CHECK_EQ(window.completions[0]->pageRender.bitmap.height, 200);

    window.FreeCompletions();
    worker.Shutdown();
    DestroyWindow(window.hwnd);
    DeleteFileW(path.c_str());
}

FASTPDF_TEST(worker_preview_and_final_render_distinct_sizes) {
    const std::wstring path = WriteBytesToTempFile(MakeMixedSizePdf());
    FASTPDF_CHECK(!path.empty());

    TestWindow window;
    FASTPDF_CHECK(window.Create());

    RenderWorker worker;
    worker.Start(window.hwnd, kDoneMessage);
    worker.OpenDocument(9, path);
    FASTPDF_CHECK_EQ(window.PumpUntil(1, 5000), 1u);
    window.FreeCompletions();

    worker.SetViewEpoch(1);

    // Preview is half of final (150x200 -> 75x100). Both must render at their
    // exact requested dimensions.
    const auto preview = PreviewSizeFor(150, 200);
    FASTPDF_CHECK_EQ(preview.width, 75);
    FASTPDF_CHECK_EQ(preview.height, 100);

    worker.SubmitRender(MakeJob(9, 1, 1, 0, preview.width, preview.height,
                                Quality::Preview));
    worker.SubmitRender(MakeJob(9, 1, 2, 0, 150, 200, Quality::Final));

    FASTPDF_CHECK_EQ(window.PumpUntil(2, 5000), 2u);

    bool sawPreview = false;
    bool sawFinal = false;
    for (const auto* completion : window.completions) {
        if (completion->key.quality == Quality::Preview) {
            sawPreview = true;
            FASTPDF_CHECK_EQ(completion->pageRender.bitmap.width, 75);
            FASTPDF_CHECK_EQ(completion->pageRender.bitmap.height, 100);
        } else {
            sawFinal = true;
            FASTPDF_CHECK_EQ(completion->pageRender.bitmap.width, 150);
            FASTPDF_CHECK_EQ(completion->pageRender.bitmap.height, 200);
        }
    }
    FASTPDF_CHECK(sawPreview);
    FASTPDF_CHECK(sawFinal);

    window.FreeCompletions();
    worker.Shutdown();
    DestroyWindow(window.hwnd);
    DeleteFileW(path.c_str());
}

FASTPDF_TEST(worker_drops_job_for_stale_document_epoch) {
    const std::wstring path = WriteBytesToTempFile(MakeMixedSizePdf());
    FASTPDF_CHECK(!path.empty());

    TestWindow window;
    FASTPDF_CHECK(window.Create());

    RenderWorker worker;
    worker.Start(window.hwnd, kDoneMessage);
    worker.OpenDocument(11, path);
    FASTPDF_CHECK_EQ(window.PumpUntil(1, 5000), 1u);
    window.FreeCompletions();

    worker.SetViewEpoch(1);

    // A job tagged with a document epoch that is not the current document must
    // be dropped (no completion), and a valid job must still complete.
    worker.SubmitRender(MakeJob(99, 1, 1, 0, 100, 100, Quality::Final));
    worker.SubmitRender(MakeJob(11, 1, 2, 1, 100, 100, Quality::Final));

    FASTPDF_CHECK_EQ(window.PumpUntil(1, 5000), 1u);
    FASTPDF_CHECK_EQ(window.completions[0]->key.pageIndex, 1);
    FASTPDF_CHECK_EQ(window.completions[0]->docEpoch, 11u);

    // Give the worker a moment; the stale job must never produce a completion.
    FASTPDF_CHECK_EQ(window.PumpUntil(2, 250), 1u);

    window.FreeCompletions();
    worker.Shutdown();
    DestroyWindow(window.hwnd);
    DeleteFileW(path.c_str());
}

int main() {
    fastpdf::pdfium::PdfiumLibrary library;
    if (!library.isAvailable()) {
        std::printf("fastpdf_worker_pipeline_tests: PDFium is not available.\n");
        return 2;
    }
    return fastpdf::test::RunAll();
}
