#pragma once

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <fastpdf/pdfium/PdfDocument.h>
#include "PdfToPngOptions.h"

namespace fastpdf::app::convert {

enum class BatchResultStatus {
    Success,
    Cancelled,
    FileAlreadyExists,
    RenderFailed,
    WriteFailed,
    InvalidSource
};

struct BatchProgress {
    int current = 0;
    int total = 0;
    int pageNumber = 0; // 1-based page number being processed
};

struct BatchConversionResult {
    BatchResultStatus status = BatchResultStatus::Success;
    int completedPages = 0;
    int totalPages = 0;
    std::wstring failedFile;
    std::wstring userErrorMessage;
};

// Callback invoked on the worker thread for progress updates.
// Return false to cancel conversion early.
using ProgressCallback = std::function<bool(const BatchProgress& progress)>;

// Off-thread cancelable batch PDF to PNG conversion.
// Uses PdfDocument directly on the caller thread. Serialized via pdfiumGate
// internally in PdfDocument.
// Atomically writes each page to a temporary file in the destination folder
// and renames to the final name. Cleans up partial temp files on failure or cancellation.
// Never overwrites existing destination files (stops and reports FileAlreadyExists).
BatchConversionResult RunPdfToPngBatch(
    const std::wstring& pdfPath,
    const ConversionOptions& options,
    const std::vector<int>& pagesToConvert,
    const std::atomic<bool>& cancelFlag,
    ProgressCallback onProgress) noexcept;

} // namespace fastpdf::app::convert
