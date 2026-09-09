// Phase-6A PDF to PNG conversion integration and cancellation tests:
// Generates multi-page PDF fixtures, tests Standard (150 DPI) and High (300 DPI) conversion,
// verifies output file naming, PNG validity and pixel dimensions via WIC,
// verifies that existing files are never overwritten,
// and verifies cancellation cleanly stops and removes temporary files.

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_harness.h"
#include <fastpdf/platform/win/ComInitializer.h>
#include <fastpdf/pdfium/PdfiumLibrary.h>
#include "PdfToPngOptions.h"
#include "PdfToPngWorker.h"
#include "ScreenshotClipboard.h"

namespace {

// Helper to generate a 3-page minimal valid PDF with distinct page sizes:
// Page 1: 144 x 72 pt (2 x 1 inches) -> At 150 DPI: 300 x 150 px. At 300 DPI: 600 x 300 px.
// Page 2: 72 x 144 pt (1 x 2 inches) -> At 150 DPI: 150 x 300 px.
// Page 3: 216 x 216 pt (3 x 3 inches) -> At 150 DPI: 450 x 450 px.
std::string GenerateThreePagePdf() {
    std::string pdf;
    pdf.reserve(4096);
    pdf += "%PDF-1.4\n";
    std::vector<size_t> offsets;

    auto add_obj = [&](const std::string& body) {
        offsets.push_back(pdf.size());
        const size_t id = offsets.size();
        pdf += std::to_string(id) + " 0 obj\n" + body + "\nendobj\n";
        return id;
    };

    add_obj("<< /Type /Catalog /Pages 2 0 R >>");
    add_obj("<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>");
    add_obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 144 72] /Resources <<>> >>");
    add_obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 72 144] /Resources <<>> >>");
    add_obj("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 216 216] /Resources <<>> >>");

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

// Creates a temporary directory for test output
class ScopedTempDirectory {
public:
    ScopedTempDirectory() {
        wchar_t tempPath[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tempPath);
        wchar_t tempDir[MAX_PATH]{};
        GetTempFileNameW(tempPath, L"FPD", 0, tempDir);
        DeleteFileW(tempDir);
        CreateDirectoryW(tempDir, nullptr);
        path_ = tempDir;
    }

    ~ScopedTempDirectory() {
        if (!path_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }

    const std::wstring& Path() const noexcept { return path_; }

private:
    std::wstring path_;
};

std::wstring CreateTestPdfFile(const std::wstring& dir) {
    const std::wstring filePath = dir + L"\\test_document.pdf";
    const std::string bytes = GenerateThreePagePdf();
    std::ofstream out(filePath, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return filePath;
}

std::vector<std::uint8_t> ReadFileBytes(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return {};
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(size));
    if (in.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return buffer;
    }
    return {};
}

} // namespace

FASTPDF_TEST(conversion_standard_and_high_dpi_pipeline) {
    fastpdf::platform::win::ComInitializer com;
    FASTPDF_CHECK(com.initialized());

    fastpdf::pdfium::PdfiumLibrary lib;
    FASTPDF_CHECK(lib.isAvailable());

    ScopedTempDirectory tempDir;
    const std::wstring pdfPath = CreateTestPdfFile(tempDir.Path());
    FASTPDF_CHECK(!pdfPath.empty());

    using namespace fastpdf::app::convert;

    // 1. Test Standard DPI (150 DPI) conversion for Page 1 & 2
    {
        ConversionOptions opts;
        opts.outputFolder = tempDir.Path();
        opts.docBaseName = L"testdoc";
        opts.quality = QualityPreset::Standard; // 150 DPI

        std::vector<int> pages = {0, 1};
        std::atomic<bool> cancel{false};
        int progressCalls = 0;

        BatchConversionResult res = RunPdfToPngBatch(
            pdfPath, opts, pages, cancel,
            [&](const BatchProgress& p) {
                progressCalls++;
                FASTPDF_CHECK(p.current >= 1 && p.current <= 2);
                FASTPDF_CHECK_EQ(p.total, 2);
                return true;
            });

        FASTPDF_CHECK_EQ(static_cast<int>(res.status), static_cast<int>(BatchResultStatus::Success));
        FASTPDF_CHECK_EQ(res.completedPages, 2);
        FASTPDF_CHECK(progressCalls >= 2);

        // Verify page 1: 144x72 pt @ 150 DPI -> 300 x 150 px
        const std::wstring file0 = tempDir.Path() + L"\\testdoc_page_001.png";
        FASTPDF_CHECK(std::filesystem::exists(file0));
        const std::vector<std::uint8_t> b0 = ReadFileBytes(file0);
        FASTPDF_CHECK(!b0.empty());
        int w0 = 0, h0 = 0;
        std::vector<std::uint8_t> bgra0;
        FASTPDF_CHECK(fastpdf::app::screenshot::DecodePngWithWic(b0, w0, h0, bgra0));
        FASTPDF_CHECK_EQ(w0, 300);
        FASTPDF_CHECK_EQ(h0, 150);

        // Verify page 2: 72x144 pt @ 150 DPI -> 150 x 300 px
        const std::wstring file1 = tempDir.Path() + L"\\testdoc_page_002.png";
        FASTPDF_CHECK(std::filesystem::exists(file1));
        const std::vector<std::uint8_t> b1 = ReadFileBytes(file1);
        FASTPDF_CHECK(!b1.empty());
        int w1 = 0, h1 = 0;
        std::vector<std::uint8_t> bgra1;
        FASTPDF_CHECK(fastpdf::app::screenshot::DecodePngWithWic(b1, w1, h1, bgra1));
        FASTPDF_CHECK_EQ(w1, 150);
        FASTPDF_CHECK_EQ(h1, 300);
    }

    // 2. Test High DPI (300 DPI) conversion for Page 1 into a separate folder
    {
        ScopedTempDirectory highDpiDir;
        ConversionOptions opts;
        opts.outputFolder = highDpiDir.Path();
        opts.docBaseName = L"sample";
        opts.quality = QualityPreset::High; // 300 DPI

        std::vector<int> pages = {0};
        std::atomic<bool> cancel{false};

        BatchConversionResult res = RunPdfToPngBatch(
            pdfPath, opts, pages, cancel, nullptr);

        FASTPDF_CHECK_EQ(static_cast<int>(res.status), static_cast<int>(BatchResultStatus::Success));
        FASTPDF_CHECK_EQ(res.completedPages, 1);

        // Page 1: 144x72 pt @ 300 DPI -> 600 x 300 px
        const std::wstring file0 = highDpiDir.Path() + L"\\sample_page_001.png";
        FASTPDF_CHECK(std::filesystem::exists(file0));
        const std::vector<std::uint8_t> b0 = ReadFileBytes(file0);
        int w0 = 0, h0 = 0;
        std::vector<std::uint8_t> bgra0;
        FASTPDF_CHECK(fastpdf::app::screenshot::DecodePngWithWic(b0, w0, h0, bgra0));
        FASTPDF_CHECK_EQ(w0, 600);
        FASTPDF_CHECK_EQ(h0, 300);
    }
}

FASTPDF_TEST(conversion_never_overwrites_existing_files) {
    fastpdf::platform::win::ComInitializer com;
    FASTPDF_CHECK(com.initialized());

    fastpdf::pdfium::PdfiumLibrary lib;
    FASTPDF_CHECK(lib.isAvailable());

    ScopedTempDirectory tempDir;
    const std::wstring pdfPath = CreateTestPdfFile(tempDir.Path());

    using namespace fastpdf::app::convert;

    // Create a pre-existing file matching target page 1
    const std::wstring existingFile = tempDir.Path() + L"\\existing_page_001.png";
    {
        std::ofstream dummy(existingFile);
        dummy << "DO NOT OVERWRITE THIS DATA";
    }

    ConversionOptions opts;
    opts.outputFolder = tempDir.Path();
    opts.docBaseName = L"existing";
    opts.quality = QualityPreset::Standard;

    std::vector<int> pages = {0, 1}; // page 0 will conflict with existing_page_001.png
    std::atomic<bool> cancel{false};

    BatchConversionResult res = RunPdfToPngBatch(
        pdfPath, opts, pages, cancel, nullptr);

    FASTPDF_CHECK_EQ(static_cast<int>(res.status), static_cast<int>(BatchResultStatus::FileAlreadyExists));
    FASTPDF_CHECK_EQ(res.completedPages, 0);

    // Verify existing file content is intact and was not modified or deleted
    std::ifstream check(existingFile);
    std::string content;
    std::getline(check, content);
    FASTPDF_CHECK_EQ(content, "DO NOT OVERWRITE THIS DATA");

    // Page 2 should not have been created either
    const std::wstring file2 = tempDir.Path() + L"\\existing_page_002.png";
    FASTPDF_CHECK(!std::filesystem::exists(file2));
}

FASTPDF_TEST(conversion_cancellation_and_cleanup) {
    fastpdf::platform::win::ComInitializer com;
    FASTPDF_CHECK(com.initialized());

    fastpdf::pdfium::PdfiumLibrary lib;
    FASTPDF_CHECK(lib.isAvailable());

    ScopedTempDirectory tempDir;
    const std::wstring pdfPath = CreateTestPdfFile(tempDir.Path());

    using namespace fastpdf::app::convert;

    ConversionOptions opts;
    opts.outputFolder = tempDir.Path();
    opts.docBaseName = L"canceltest";
    opts.quality = QualityPreset::Standard;

    std::vector<int> pages = {0, 1, 2};
    std::atomic<bool> cancel{false};

    // Trigger cancel on second page progress callback
    BatchConversionResult res = RunPdfToPngBatch(
        pdfPath, opts, pages, cancel,
        [&](const BatchProgress& p) {
            if (p.current >= 2) {
                cancel.store(true);
                return false; // cancel signal via return code
            }
            return true;
        });

    FASTPDF_CHECK_EQ(static_cast<int>(res.status), static_cast<int>(BatchResultStatus::Cancelled));

    // Check that no temporary files (*.tmp or FPT*.tmp) remain in tempDir
    for (const auto& entry : std::filesystem::directory_iterator(tempDir.Path())) {
        const std::wstring ext = entry.path().extension().wstring();
        const std::wstring name = entry.path().filename().wstring();
        FASTPDF_CHECK(ext != L".tmp");
        FASTPDF_CHECK(name.find(L"FPT") == std::wstring::npos);
    }
}

int main() {
    return fastpdf::test::RunAll();
}
