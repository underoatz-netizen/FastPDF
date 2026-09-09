#pragma once

// Internal-only: the process-wide PDFium call gate. This is the ONLY allowed
// global mutable state in fastpdf_pdfium (per the implementation guardrails);
// it serializes every raw PDFium API call so PDFium is never entered
// concurrently from two threads. PDFium's process-wide state is not safe for
// concurrent document/render use, so this gate is the architectural backstop
// even though FastPDF uses a single render thread.
//
// Consumers inside this boundary lock pdfiumGate() around any raw FPDF_* call.
// The RAII PdfiumCallGuard is the preferred way to do that.

#include <mutex>

namespace fastpdf::pdfium::detail {

std::mutex& pdfiumGate() noexcept;

class PdfiumCallGuard {
public:
    PdfiumCallGuard() : lock_(pdfiumGate()) {}

    PdfiumCallGuard(const PdfiumCallGuard&) = delete;
    PdfiumCallGuard& operator=(const PdfiumCallGuard&) = delete;

private:
    std::lock_guard<std::mutex> lock_;
};

}  // namespace fastpdf::pdfium::detail
