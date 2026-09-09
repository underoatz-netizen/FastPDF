#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "PrintCommon.h"
#include "fastpdf/core/print_layout.h"
#include "fastpdf/pdfium/PdfDocument.h"

namespace fastpdf::app::print {

struct PrintJobProgress {
    int currentPageIndex = 0;
    int currentJobPage = 0;     // 1-based page in current job
    int totalJobPages = 0;
    bool canceled = false;
};

struct PrintJobResult {
    bool success = false;
    bool canceled = false;
    std::wstring errorMessage;
    int pagesPrinted = 0;
};

// Spools pages to printer in a background worker thread.
// Ensures UI stays responsive and serialized PDFium call gate is respected.
class PrintWorker {
public:
    PrintWorker() = default;
    ~PrintWorker() = default;

    // Executes the print job synchronously on the calling thread.
    // Callers run this on a background thread.
    static PrintJobResult ExecutePrintJob(
        const pdfium::PdfSource& source,
        const PrintDialogOptions& options,
        const std::vector<int>& pageIndices,
        std::atomic<bool>& cancelFlag,
        std::function<void(const PrintJobProgress&)> onProgress) noexcept;
};

} // namespace fastpdf::app::print
