#pragma once

// Low-level persistent-document render primitive for FastPDF.
//
// PdfSource loads a PDF's bytes exactly once into immutable, reference-counted
// storage. PdfDocument opens that source as a PDFium document and owns the
// open document for its lifetime. Together they give the render worker a
// document that stays open across many page renders (the Phase-2 design
// re-opened the document on every render; Phase 3 keeps it open).
//
// Threading contract (enforced by design):
//   * PdfSource is an immutable value: it may be created on any thread and
//     shared freely. Its bytes outlive any document that referenced them
//     (reference counting), including after the document is closed.
//   * PdfDocument is creator-thread-affine and noncopyable/nonmovable: every
//     method and the destructor must run on the thread that constructed it.
//     In FastPDF that is the single render worker thread.
//   * Every raw PDFium call in this boundary is serialized through the
//     process-wide internal PDFium call gate, so PDFium can never be entered
//     concurrently from two threads even if a future caller violates the
//     single-thread contract.

#include <cstdint>
#include <memory>
#include <vector>
#include <string>

#include "fastpdf/pdfium/PdfiumLibrary.h"
#include "fastpdf/pdfium/SearchTypes.h"

// Opaque PDFium document handle. Forward-declared so raw PDFium headers stay
// confined to the .cpp; the type matches fpdfview.h's FPDF_DOCUMENT.
struct fpdf_document_t__;

namespace fastpdf::pdfium {

// Immutable, shared, loaded-once source bytes for a PDF.
class PdfSource {
public:
    // Loads the file at |path| into shared immutable bytes. On failure returns
    // an invalid source (isValid() == false) and sets |error| to the mapped
    // user-facing outcome. Performs only file I/O - no PDF parsing - so it is
    // safe to call on any thread.
    static PdfSource Load(const std::wstring& path, OpenError& error) noexcept;

    PdfSource() = default;

    bool isValid() const noexcept { return bytes_ != nullptr; }

    // The immutable, reference-counted source bytes (nullptr when invalid).
    // The caller must keep this shared_ptr (or a copy) alive for as long as
    // any document opened from it remains open.
    std::shared_ptr<const std::vector<std::uint8_t>> bytes() const noexcept {
        return bytes_;
    }

private:
    std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
};

// An open PDFium document. Noncopyable and nonmovable; bound to the thread
// that constructed it.
class PdfDocument {
public:
    // Opens |source| as a PDFium document on the calling (creator) thread. On
    // failure isOpen() is false and openError() reports the mapped outcome.
    explicit PdfDocument(
        std::shared_ptr<const std::vector<std::uint8_t>> source) noexcept;

    ~PdfDocument();  // Closes the document; must run on the creator thread.

    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;
    PdfDocument(PdfDocument&&) = delete;
    PdfDocument& operator=(PdfDocument&&) = delete;

    bool isOpen() const noexcept { return document_ != nullptr; }
    OpenError openError() const noexcept { return openError_; }

    // Number of pages, or 0 when not open.
    int pageCount() const noexcept;

    // The page's size in PDF points, or false when not open or out of range.
    bool pageSize(int index, double& widthPoints,
                  double& heightPoints) const noexcept;

    // Renders page |index| into |out| at exactly |width| x |height| pixels
    // (both clamped into [1, kMaxRenderDimension] by the caller) with the given
    // |rotation| (0..3). Returns false on failure; |renderDurationMs| receives
    // the rasterization duration. Must run on the creator thread.
    bool renderPage(int index, int width, int height, int rotation, Bitmap& out,
                    double& renderDurationMs) const noexcept;

    // Renders page |index| directly to a Windows GDI HDC at (destX, destY) with
    // size (destWidth, destHeight) and rotation (0..3).
    // Serialized through the PDFium call gate.
    bool renderPageToDC(int index, void* hdc, int destX, int destY,
                        int destWidth, int destHeight, int rotation) const noexcept;

    // Returns the underlying PDFium document handle (FPDF_DOCUMENT) for callers
    // within the PDFium boundary that require direct PDFium editing/inspection APIs.
    // The handle is valid only while this PdfDocument instance remains open.
    fpdf_document_t__* handle() const noexcept { return document_; }

    // Searches page |pageIndex| for |query| (UTF-16) with case-sensitivity flag |matchCase|.
    // Returns matching character ranges [charIndex, charCount].
    // If the page has no text or is scanned, returns an empty vector.
    // Serialized through the PDFium call gate; must run on the creator thread.
    std::vector<PageTextRange> searchPage(int pageIndex,
                                         const std::wstring& query,
                                         bool matchCase) const noexcept;

    // Computes the bounding rectangles in PDF page coordinates (PDF points, bottom-left origin)
    // for a character range [charIndex, charCount] on |pageIndex|.
    // Serialized through the PDFium call gate; must run on the creator thread.
    std::vector<PdfRect> getTextRects(int pageIndex, int charIndex, int charCount) const noexcept;

private:
    fpdf_document_t__* document_ = nullptr;
    OpenError openError_ = OpenError::None;
    // Keeps the source bytes alive for the document's lifetime.
    // FPDF_LoadMemDocument64 references (does not copy) the buffer, so the
    // bytes must outlive the document - this reference guarantees that.
    std::shared_ptr<const std::vector<std::uint8_t>> source_;
};

}  // namespace fastpdf::pdfium
