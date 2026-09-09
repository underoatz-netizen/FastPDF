// Phase-5 screenshot end-to-end smoke: loads a generated multi-page fixture,
// renders a page, simulates an area selection spanning across pages and viewport,
// verifies offscreen composite bitmap generation, clipboard copy, and WIC PNG encoding.
// Requires the pinned PDFium artifact.

#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_harness.h"
#include <fastpdf/platform/win/ComInitializer.h>
#include <fastpdf/pdfium/PdfiumLibrary.h>
#include "ScreenshotGeometry.h"
#include "ScreenshotCompositor.h"
#include "ScreenshotClipboard.h"

namespace {

// Minimal blank 2-page PDF
std::string GenerateTwoPagePdf() {
    std::string pdf;
    pdf.reserve(2048);
    pdf += "%PDF-1.4\n";
    std::vector<size_t> offsets;

    auto add_obj = [&](const std::string& body) {
        offsets.push_back(pdf.size());
        const size_t id = offsets.size();
        pdf += std::to_string(id) + " 0 obj\n" + body + "\nendobj\n";
        return id;
    };

    add_obj("<< /Type /Catalog /Pages 2 0 R >>");
    add_obj("<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>");
    add_obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources <<>> >>");
    add_obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources <<>> >>");

    const size_t xref_start = pdf.size();
    pdf += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    for (size_t o : offsets) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%010zu 00000 n \n", o);
        pdf += buf;
    }
    pdf += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) +
           " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref_start) + "\n%%EOF\n";
    return pdf;
}

std::wstring CreateTempPdfFile() {
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t tempFile[MAX_PATH]{};
    GetTempFileNameW(tempDir, L"FP5", 0, tempFile);

    const std::string bytes = GenerateTwoPagePdf();
    std::ofstream out(tempFile, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();

    return tempFile;
}

} // namespace

FASTPDF_TEST(screenshot_pipeline_smoke) {
    fastpdf::platform::win::ComInitializer com;
    FASTPDF_CHECK(com.initialized());

    const std::wstring pdfPath = CreateTempPdfFile();
    FASTPDF_CHECK(!pdfPath.empty());

    fastpdf::pdfium::PdfiumLibrary lib;
    FASTPDF_CHECK(lib.isAvailable());

    // 1. Render page 0 and page 1 via fastpdf::pdfium::RenderPage
    fastpdf::pdfium::PageRenderRequest req0;
    req0.path = pdfPath;
    req0.pageIndex = 0;
    req0.width = 100;
    req0.height = 100;
    const fastpdf::pdfium::PageRenderResult r0 = fastpdf::pdfium::RenderPage(req0);
    FASTPDF_CHECK(r0.ok);
    FASTPDF_CHECK(!r0.bitmap.empty());
    FASTPDF_CHECK_EQ(r0.bitmap.width, 100);
    FASTPDF_CHECK_EQ(r0.bitmap.height, 100);

    fastpdf::pdfium::PageRenderRequest req1;
    req1.path = pdfPath;
    req1.pageIndex = 1;
    req1.width = 100;
    req1.height = 100;
    const fastpdf::pdfium::PageRenderResult r1 = fastpdf::pdfium::RenderPage(req1);
    FASTPDF_CHECK(r1.ok);
    FASTPDF_CHECK(!r1.bitmap.empty());

    // 2. Set up PageSources: Page 0 at (10, 10), Page 1 at (10, 120) (10px vertical gap)
    fastpdf::app::screenshot::PageSource p0{0, 10, 10, 100, 100, &r0.bitmap};
    fastpdf::app::screenshot::PageSource p1{1, 10, 120, 100, 100, &r1.bitmap};

    // 3. Selection from (0, 0) to (120, 230) spanning both pages, margins, and the gap
    const fastpdf::app::screenshot::Rect sel{0, 0, 120, 230};
    const fastpdf::renderer::Bitmap composite =
        fastpdf::app::screenshot::CompositeSelection(sel, {p0, p1});

    FASTPDF_CHECK(!composite.empty());
    FASTPDF_CHECK_EQ(composite.width, 120);
    FASTPDF_CHECK_EQ(composite.height, 230);
    FASTPDF_CHECK_EQ(composite.stride, 120 * 4);

    // 4. Test clipboard copy
    const bool copied = fastpdf::app::screenshot::CopyBitmapToClipboard(nullptr, composite);
    FASTPDF_CHECK(copied);

    // 5. Test WIC PNG encoding and decoding
    const std::vector<std::uint8_t> pngBytes =
        fastpdf::app::screenshot::EncodePngWithWic(composite);
    FASTPDF_CHECK(!pngBytes.empty());

    int w = 0, h = 0;
    std::vector<std::uint8_t> dec;
    const bool decOk = fastpdf::app::screenshot::DecodePngWithWic(pngBytes, w, h, dec);
    FASTPDF_CHECK(decOk);
    FASTPDF_CHECK_EQ(w, 120);
    FASTPDF_CHECK_EQ(h, 230);

    // Clean up temporary file
    DeleteFileW(pdfPath.c_str());
}

int main() { return fastpdf::test::RunAll(); }
