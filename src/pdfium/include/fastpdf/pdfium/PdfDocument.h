#pragma once

// Low-level persistent-document render primitive for FastPDF.
//
// PdfSource loads a PDF's bytes exactly once into an opaque, read-only,
// reference-counted memory mapping (no eager whole-file read). PdfDocument
// opens that source as a PDFium document and owns the open document for its
// lifetime. Together they give the render worker a document that stays open
// across many page renders (the Phase-2 design re-opened the document on every
// render; Phase 3 keeps it open).
//
// Threading contract (enforced by design):
//   * PdfSource is an immutable value: it may be created on any thread and
//     shared freely. Its mapping outlives any document that referenced it
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

// Opaque read-only memory-mapped PDF file. Defined in the .cpp; PdfSource
// holds a shared reference so the mapping outlives any document opened from it.
struct MappedPdfFile;

namespace fastpdf::pdfium {

// Immutable, shared, loaded-once source for a PDF: an opaque read-only file
// mapping (no eager whole-file read into a heap buffer).
class PdfSource {
public:
    // Loads the file at |path| into an opaque read-only memory mapping. On
    // failure returns an invalid source (isValid() == false) and sets |error|
    // to the mapped user-facing outcome. Performs only file I/O - no PDF
    // parsing - so it is safe to call on any thread. The mapping is created
    // with FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE so the file
    // is not locked against other readers or replacers.
    static PdfSource Load(const std::wstring& path, OpenError& error) noexcept;

    PdfSource() = default;

    bool isValid() const noexcept { return mapping_ != nullptr; }

    // The mapped bytes (nullptr / 0 when invalid). The caller must keep this
    // PdfSource (or a copy) alive for as long as any document opened from it
    // remains open.
    const std::uint8_t* data() const noexcept;
    std::size_t size() const noexcept;

private:
    std::shared_ptr<const MappedPdfFile> mapping_;
};

// An open PDFium document. Noncopyable and nonmovable; bound to the thread
// that constructed it.
class PdfDocument {
public:
    // Opens |source| (an opaque read-only file mapping) as a PDFium document
    // on the calling (creator) thread. The mapping is retained for the
    // document's lifetime. On failure isOpen() is false and openError()
    // reports the mapped outcome.
    explicit PdfDocument(PdfSource source) noexcept;

    // Opens an in-memory byte buffer as a PDFium document. The buffer must
    // outlive the document (FPDF_LoadMemDocument64 references, not copies, the
    // buffer). Kept for callers that already hold the bytes in memory.
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
    // Keeps the source alive for the document's lifetime: either the opaque
    // file mapping or the in-memory byte buffer. FPDF_LoadMemDocument64
    // references (does not copy) the buffer, so the source must outlive the
    // document - these references guarantee that.
    PdfSource source_;
    std::shared_ptr<const std::vector<std::uint8_t>> bytes_;
};

}  // namespace fastpdf::pdfium
