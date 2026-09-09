#include "fastpdf/pdfium/PdfiumLibrary.h"

// Stub implementation used when FASTPDF_WITH_PDFIUM=OFF.
// No PDFium headers or symbols are referenced in this translation unit.

namespace fastpdf::pdfium {

RenderResult OpenAndRenderFirstPage(const RenderRequest& /*request*/) noexcept {
    RenderResult result;
    result.error = OpenError::PdfiumUnavailable;
    return result;
}

DocumentInfoResult OpenDocumentInfo(const DocumentInfoRequest& /*request*/) noexcept {
    DocumentInfoResult result;
    result.error = OpenError::PdfiumUnavailable;
    return result;
}

PageRenderResult RenderPage(const PageRenderRequest& /*request*/) noexcept {
    PageRenderResult result;
    result.error = OpenError::PdfiumUnavailable;
    return result;
}

} // namespace fastpdf::pdfium