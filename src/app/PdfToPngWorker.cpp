#include "PdfToPngWorker.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

#include <fastpdf/platform/win/ComInitializer.h>
#include "ScreenshotClipboard.h"

namespace fastpdf::app::convert {

namespace {

// Compute target dimensions at the specified DPI (PDF point = 1/72 inch).
bool ComputeDimensions(double widthPoints, double heightPoints, double dpi,
                       int& outWidth, int& outHeight) noexcept {
    if (widthPoints <= 0.0 || heightPoints <= 0.0 || dpi <= 0.0) {
        return false;
    }
    const double scale = dpi / 72.0;
    const double w = widthPoints * scale;
    const double h = heightPoints * scale;

    // Hard bounds: [1, 16384] to prevent memory overflow
    if (w < 1.0 || h < 1.0 || w > 16384.0 || h > 16384.0) {
        return false;
    }

    outWidth = static_cast<int>(std::lround(w));
    outHeight = static_cast<int>(std::lround(h));
    return (outWidth >= 1 && outHeight >= 1);
}

// Scoped helper to clean up a temp file if not explicitly committed.
class ScopedTempFile {
public:
    explicit ScopedTempFile(std::wstring path) : path_(std::move(path)) {}
    ~ScopedTempFile() {
        if (!committed_ && !path_.empty()) {
            DeleteFileW(path_.c_str());
        }
    }

    void Commit() noexcept { committed_ = true; }
    const std::wstring& Path() const noexcept { return path_; }

private:
    std::wstring path_;
    bool committed_ = false;
};

} // namespace

BatchConversionResult RunPdfToPngBatch(
    const std::wstring& pdfPath,
    const ConversionOptions& options,
    const std::vector<int>& pagesToConvert,
    const std::atomic<bool>& cancelFlag,
    ProgressCallback onProgress) noexcept {

    BatchConversionResult result;
    result.totalPages = static_cast<int>(pagesToConvert.size());

    if (pagesToConvert.empty()) {
        result.status = BatchResultStatus::Success;
        return result;
    }

    // Ensure COM is initialized on this thread for WIC.
    fastpdf::platform::win::ComInitializer com;
    if (!com.initialized()) {
        result.status = BatchResultStatus::WriteFailed;
        result.userErrorMessage = L"Failed to initialize COM for image encoding.";
        return result;
    }

    // Verify output directory exists or can be accessed.
    std::error_code ec;
    std::filesystem::path outDir(options.outputFolder);
    if (!std::filesystem::is_directory(outDir, ec)) {
        result.status = BatchResultStatus::WriteFailed;
        result.userErrorMessage = L"Output folder does not exist.";
        return result;
    }

    // Load PDF source bytes
    fastpdf::pdfium::OpenError openError = fastpdf::pdfium::OpenError::None;
    fastpdf::pdfium::PdfSource source = fastpdf::pdfium::PdfSource::Load(pdfPath, openError);
    if (!source.isValid()) {
        result.status = BatchResultStatus::InvalidSource;
        result.userErrorMessage = L"Failed to load PDF file.";
        return result;
    }

    // Open PDF document
    fastpdf::pdfium::PdfDocument doc(source);
    if (!doc.isOpen()) {
        result.status = BatchResultStatus::InvalidSource;
        result.userErrorMessage = L"Failed to open PDF document.";
        return result;
    }

    const int totalDocPages = doc.pageCount();
    const double dpi = DpiForPreset(options.quality);

    for (std::size_t i = 0; i < pagesToConvert.size(); ++i) {
        if (cancelFlag.load(std::memory_order_relaxed)) {
            result.status = BatchResultStatus::Cancelled;
            result.userErrorMessage = L"Conversion cancelled.";
            return result;
        }

        const int pageIdx = pagesToConvert[i];
        if (pageIdx < 0 || pageIdx >= totalDocPages) {
            continue;
        }

        if (onProgress) {
            BatchProgress prog;
            prog.current = static_cast<int>(i) + 1;
            prog.total = result.totalPages;
            prog.pageNumber = pageIdx + 1;
            if (!onProgress(prog)) {
                result.status = BatchResultStatus::Cancelled;
                result.userErrorMessage = L"Conversion cancelled.";
                return result;
            }
        }

        // Determine destination file name
        const std::wstring fileName =
            FormatPageFileName(options.docBaseName, pageIdx, totalDocPages);
        const std::filesystem::path destPath = outDir / fileName;
        const std::wstring destPathW = destPath.wstring();

        // Check if destination file already exists - NEVER overwrite unexpected existing files!
        if (std::filesystem::exists(destPath, ec)) {
            result.status = BatchResultStatus::FileAlreadyExists;
            result.failedFile = fileName;
            result.userErrorMessage = L"File already exists: " + fileName;
            return result;
        }

        // Generate a temporary file name in the target directory
        wchar_t tempFileName[MAX_PATH]{};
        if (GetTempFileNameW(options.outputFolder.c_str(), L"FPT", 0, tempFileName) == 0) {
            result.status = BatchResultStatus::WriteFailed;
            result.userErrorMessage = L"Failed to create temporary output file.";
            return result;
        }

        ScopedTempFile scopedTemp(tempFileName);

        // Render page through PDFium
        double wPts = 0.0, hPts = 0.0;
        if (!doc.pageSize(pageIdx, wPts, hPts)) {
            result.status = BatchResultStatus::RenderFailed;
            result.failedFile = fileName;
            result.userErrorMessage = L"Failed to get page size for page " + std::to_wstring(pageIdx + 1);
            return result;
        }

        int pixelW = 0, pixelH = 0;
        if (!ComputeDimensions(wPts, hPts, dpi, pixelW, pixelH)) {
            result.status = BatchResultStatus::RenderFailed;
            result.failedFile = fileName;
            result.userErrorMessage = L"Invalid or oversized dimensions for page " + std::to_wstring(pageIdx + 1);
            return result;
        }

        fastpdf::renderer::Bitmap renderedBmp;
        double renderMs = 0.0;
        if (!doc.renderPage(pageIdx, pixelW, pixelH, 0, renderedBmp, renderMs)) {
            result.status = BatchResultStatus::RenderFailed;
            result.failedFile = fileName;
            result.userErrorMessage = L"Failed to render page " + std::to_wstring(pageIdx + 1);
            return result;
        }

        // Check cancellation before disk write
        if (cancelFlag.load(std::memory_order_relaxed)) {
            result.status = BatchResultStatus::Cancelled;
            result.userErrorMessage = L"Conversion cancelled.";
            return result; // scopedTemp will automatically delete temp file
        }

        // Save to temp file with WIC
        if (!fastpdf::app::screenshot::SavePngToFile(renderedBmp, scopedTemp.Path())) {
            result.status = BatchResultStatus::WriteFailed;
            result.failedFile = fileName;
            result.userErrorMessage = L"Failed to write PNG image for page " + std::to_wstring(pageIdx + 1);
            return result;
        }

        // Check cancellation before atomic rename
        if (cancelFlag.load(std::memory_order_relaxed)) {
            result.status = BatchResultStatus::Cancelled;
            result.userErrorMessage = L"Conversion cancelled.";
            return result; // scopedTemp cleans up
        }

        // Atomic replace/move from temp file to final destination
        // MOVEFILE_WRITE_THROUGH ensures flush; no MOVEFILE_REPLACE_EXISTING because we don't want overwrite
        const BOOL moveOk = MoveFileExW(
            scopedTemp.Path().c_str(),
            destPathW.c_str(),
            MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);

        if (!moveOk) {
            result.status = BatchResultStatus::WriteFailed;
            result.failedFile = fileName;
            result.userErrorMessage = L"Failed to finalize output file: " + fileName;
            return result;
        }

        scopedTemp.Commit();
        result.completedPages++;
    }

    result.status = BatchResultStatus::Success;
    return result;
}

} // namespace fastpdf::app::convert
