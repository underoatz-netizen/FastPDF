#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fastpdf::app::convert {

enum class ImageToPdfStatus {
    Success,
    Cancelled,
    OutputFileAlreadyExists,
    NoImagesProvided,
    ImageDecodeFailed,
    PdfCreationFailed,
    WriteFailed,
    PdfiumUnavailable
};

struct ImageToPdfProgress {
    int current = 0;
    int total = 0;
    std::wstring currentFileName;
};

struct ImageToPdfResult {
    ImageToPdfStatus status = ImageToPdfStatus::Success;
    int completedImages = 0;
    int totalImages = 0;
    std::wstring failedFile;
    std::wstring userErrorMessage;
};

using ImageToPdfProgressCallback = std::function<bool(const ImageToPdfProgress& progress)>;

// Creates a multi-page PDF document from an ordered list of image file paths
// (PNG, JPEG, JPG, BMP).
// Each image is decoded via WIC, placed on its own A4 page (auto portrait/landscape,
// fixed margin, aspect ratio preserved, safe alpha handling).
// Writes atomically to a temporary file in the destination directory and renames to outputPath.
// Never overwrites an existing file at outputPath.
// Thread safety: serializes all raw PDFium calls through pdfiumGate(). Safe to call on worker thread.
ImageToPdfResult CreatePdfFromImages(
    const std::vector<std::wstring>& imagePaths,
    const std::wstring& outputPath,
    const std::atomic<bool>& cancelFlag,
    ImageToPdfProgressCallback onProgress) noexcept;

} // namespace fastpdf::app::convert
