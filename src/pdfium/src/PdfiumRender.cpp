// Legacy open/render entry points, kept as compatible wrappers over the
// Phase-3 PdfSource/PdfDocument primitive so existing callers and tests keep
// working unchanged. Each wrapper loads the source, opens a document, performs
// the requested work, and closes it again entirely within the call - the same
// single-call ownership contract the Phase-1/Phase-2 API exposed. Every raw
// PDFium call runs through the process-wide gate inside PdfDocument.

#include "fastpdf/pdfium/PdfiumLibrary.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "fastpdf/pdfium/PdfDocument.h"
#include "pdfium_internal.h"

namespace fastpdf::pdfium {

RenderResult OpenAndRenderFirstPage(const RenderRequest& request) noexcept {
    RenderResult result;

    const auto openStart = internal::Clock::now();
    OpenError error = OpenError::None;
    PdfSource source = PdfSource::Load(request.path, error);
    if (!source.isValid()) {
        result.error = error;
        return result;
    }
    PdfDocument document(source);
    if (!document.isOpen()) {
        result.error = document.openError();
        return result;
    }
    const auto openEnd = internal::Clock::now();
    result.openDurationMs =
        std::chrono::duration<double, std::milli>(openEnd - openStart).count();

    result.pageCount = document.pageCount();
    if (result.pageCount <= 0) {
        result.error = OpenError::Corrupted;
        return result;
    }

    double pageWidth = 0.0;
    double pageHeight = 0.0;
    if (!document.pageSize(0, pageWidth, pageHeight)) {
        result.error = OpenError::Corrupted;
        return result;
    }
    result.pageWidthPoints = pageWidth;
    result.pageHeightPoints = pageHeight;

    int renderWidth = 0;
    int renderHeight = 0;
    internal::ComputeFitSize(pageWidth, pageHeight, request.targetWidth,
                             request.targetHeight, renderWidth, renderHeight);

    double renderDurationMs = 0.0;
    if (document.renderPage(0, renderWidth, renderHeight, 0, result.bitmap,
                            renderDurationMs)) {
        result.ok = true;
        result.renderDurationMs = renderDurationMs;
    } else {
        result.error = OpenError::Unknown;
    }
    return result;
}

DocumentInfoResult OpenDocumentInfo(const DocumentInfoRequest& request) noexcept {
    DocumentInfoResult result;

    const auto openStart = internal::Clock::now();
    OpenError error = OpenError::None;
    PdfSource source = PdfSource::Load(request.path, error);
    if (!source.isValid()) {
        result.error = error;
        return result;
    }
    PdfDocument document(source);
    if (!document.isOpen()) {
        result.error = document.openError();
        return result;
    }
    const auto openEnd = internal::Clock::now();
    result.openDurationMs =
        std::chrono::duration<double, std::milli>(openEnd - openStart).count();

    result.pageCount = document.pageCount();
    if (result.pageCount <= 0) {
        result.error = OpenError::Corrupted;
        return result;
    }

    result.pages.reserve(static_cast<size_t>(result.pageCount));
    for (int i = 0; i < result.pageCount; ++i) {
        double width = 0.0;
        double height = 0.0;
        if (document.pageSize(i, width, height)) {
            result.pages.push_back(PageInfo{width, height});
        } else {
            // A single unloadable page should not fail the whole document;
            // report a zero-size placeholder so layout stays consistent.
            result.pages.push_back(PageInfo{0.0, 0.0});
        }
    }

    result.ok = true;
    return result;
}

PageRenderResult RenderPage(const PageRenderRequest& request) noexcept {
    PageRenderResult result;
    result.pageIndex = request.pageIndex;

    if (request.width <= 0 || request.height <= 0) {
        result.error = OpenError::Unknown;
        return result;
    }

    OpenError error = OpenError::None;
    PdfSource source = PdfSource::Load(request.path, error);
    if (!source.isValid()) {
        result.error = error;
        return result;
    }
    PdfDocument document(source);
    if (!document.isOpen()) {
        result.error = document.openError();
        return result;
    }

    double renderDurationMs = 0.0;
    if (document.renderPage(request.pageIndex, request.width, request.height, 0,
                            result.bitmap, renderDurationMs)) {
        result.ok = true;
        result.renderDurationMs = renderDurationMs;
    } else {
        result.error = OpenError::Unknown;
    }
    return result;
}

} // namespace fastpdf::pdfium
