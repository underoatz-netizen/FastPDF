#include "fastpdf/pdfium/PdfDocument.h"

// Stub implementation used when FASTPDF_WITH_PDFIUM=OFF.
// No PDFium headers or symbols are referenced in this translation unit.

namespace fastpdf::pdfium {

PdfSource PdfSource::Load(const std::wstring& /*path*/,
                          OpenError& error) noexcept {
    error = OpenError::PdfiumUnavailable;
    return {};
}

const std::uint8_t* PdfSource::data() const noexcept { return nullptr; }

std::size_t PdfSource::size() const noexcept { return 0; }

PdfDocument::PdfDocument(PdfSource /*source*/) noexcept {
    openError_ = OpenError::PdfiumUnavailable;
}

PdfDocument::PdfDocument(
    std::shared_ptr<const std::vector<std::uint8_t>> /*source*/) noexcept {
    openError_ = OpenError::PdfiumUnavailable;
}

PdfDocument::~PdfDocument() = default;

int PdfDocument::pageCount() const noexcept { return 0; }

bool PdfDocument::pageSize(int /*index*/, double& /*widthPoints*/,
                           double& /*heightPoints*/) const noexcept {
    return false;
}

bool PdfDocument::renderPage(int /*index*/, int /*width*/, int /*height*/,
                             int /*rotation*/, Bitmap& /*out*/,
                             double& /*renderDurationMs*/) const noexcept {
    return false;
}

bool PdfDocument::renderPageToDC(int /*index*/, void* /*hdc*/, int /*destX*/, int /*destY*/,
                                int /*destWidth*/, int /*destHeight*/, int /*rotation*/) const noexcept {
    return false;
}

std::vector<PageTextRange> PdfDocument::searchPage(int /*pageIndex*/,
                                                   const std::wstring& /*query*/,
                                                   bool /*matchCase*/) const noexcept {
    return {};
}

std::vector<PdfRect> PdfDocument::getTextRects(int /*pageIndex*/,
                                               int /*charIndex*/,
                                               int /*charCount*/) const noexcept {
    return {};
}

}  // namespace fastpdf::pdfium
