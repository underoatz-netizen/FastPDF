#include "fastpdf/pdfium/PdfiumLibrary.h"

// Stub implementation used when FASTPDF_WITH_PDFIUM=OFF.
// No PDFium headers or symbols are referenced in this translation unit.

namespace fastpdf::pdfium {

PdfiumLibrary::PdfiumLibrary() noexcept = default;
PdfiumLibrary::~PdfiumLibrary() noexcept = default;

bool PdfiumLibrary::isAvailable() const noexcept { return false; }

std::string_view PdfiumLibrary::sdkVersion() const noexcept { return {}; }

} // namespace fastpdf::pdfium