#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <fastpdf/renderer/bitmap.h>

namespace fastpdf::pdfium {

// Immutable CPU-side BGRA bitmap value. Defined in fastpdf_renderer (a
// PDFium-free boundary) so the render cache can store bitmaps without
// depending on PDFium; re-exported here for callers of this boundary.
using Bitmap = fastpdf::renderer::Bitmap;

// User-facing open outcomes. These map to simple, non-technical messages in
// the UI; raw PDFium error codes never cross this boundary.
enum class OpenError {
    None,
    MissingFile,        // The file does not exist.
    Unreadable,         // The file exists but could not be read.
    Corrupted,          // Not a PDF, or the PDF is damaged.
    PasswordRequired,   // The PDF is encrypted and needs a password.
    UnsupportedSecurity,// The PDF uses an unsupported security scheme.
    PdfiumUnavailable,  // PDFium is not linked/initialized.
    Unknown,
};

// A single render request. The worker thread owns all PDFium access for the
// duration of the call; the UI only ever receives a RenderResult value.
struct RenderRequest {
    std::wstring path;
    int targetWidth = 0;   // available client area, physical pixels
    int targetHeight = 0;  // available client area, physical pixels
};

// Result of opening a document and rendering its first page. Contains only
// value types (Bitmap, strings, numbers) - no PDFium or Direct2D resources.
struct RenderResult {
    bool ok = false;
    OpenError error = OpenError::None;
    Bitmap bitmap;
    int pageCount = 0;
    double pageWidthPoints = 0.0;   // first page, in PDF points
    double pageHeightPoints = 0.0;  // first page, in PDF points
    double openDurationMs = 0.0;    // PDF open (parse) duration
    double renderDurationMs = 0.0;  // first-page render duration
};

// Opens the PDF at |request.path| and renders its first page to a CPU BGRA
// bitmap, fitted to the requested target size while preserving aspect ratio.
//
// This is the ONLY entry point that touches raw PDFium document/page/bitmap
// handles, and it does so entirely on the calling (worker) thread. All
// PDFium handles are created and destroyed within this call; nothing crosses
// the fastpdf_pdfium boundary. Returns a value result; never throws.
RenderResult OpenAndRenderFirstPage(const RenderRequest& request) noexcept;

// A single page's geometry in PDF points, as reported by PDFium.
struct PageInfo {
    double widthPoints = 0.0;
    double heightPoints = 0.0;
};

// A request to open a document and report its page count and per-page
// dimensions (in PDF points). No rasterization is performed.
struct DocumentInfoRequest {
    std::wstring path;
};

// Result of opening a document and reading its page geometry. Contains only
// value types; no PDFium resources cross the boundary.
struct DocumentInfoResult {
    bool ok = false;
    OpenError error = OpenError::None;
    int pageCount = 0;
    std::vector<PageInfo> pages;  // one entry per page, in document order
    double openDurationMs = 0.0;  // PDF open (parse) duration
};

// Opens the PDF at |request.path| and returns its page count and per-page
// dimensions in PDF points. Runs entirely on the calling (worker) thread;
// all PDFium handles are created and destroyed within this call.
DocumentInfoResult OpenDocumentInfo(const DocumentInfoRequest& request) noexcept;

// A request to render one specific page of a document to a CPU BGRA bitmap at
// an explicit pixel size (the page's laid-out size at the current zoom).
struct PageRenderRequest {
    std::wstring path;
    int pageIndex = 0;      // 0-based page to render
    int width = 0;          // exact pixel width of the output bitmap
    int height = 0;         // exact pixel height of the output bitmap
};

// Result of rendering one page. Contains only value types.
struct PageRenderResult {
    bool ok = false;
    OpenError error = OpenError::None;
    int pageIndex = 0;
    Bitmap bitmap;
    double renderDurationMs = 0.0;
};

// Opens the PDF at |request.path| and renders page |request.pageIndex| into a
// CPU BGRA bitmap of exactly |request.width| x |request.height| pixels (the
// page's laid-out size at the current zoom). Runs entirely on the calling
// (worker) thread; all PDFium handles are created and destroyed within this
// call. Returns a value result; never throws.
PageRenderResult RenderPage(const PageRenderRequest& request) noexcept;

// RAII adapter that owns PDFium's process-wide initialization.
//
// Construction initializes the PDFium library (FPDF_InitLibrary) when the
// pinned PDFium artifact is linked in; destruction shuts it down
// (FPDF_DestroyLibrary). All raw PDFium headers and symbols stay inside the
// fastpdf_pdfium boundary - consumers only ever see this adapter.
//
// Exactly one live instance per process is supported; the adapter guards the
// init/shutdown pairing with an internal reference count.
class PdfiumLibrary {
public:
    PdfiumLibrary() noexcept;
    ~PdfiumLibrary() noexcept;

    PdfiumLibrary(const PdfiumLibrary&) = delete;
    PdfiumLibrary& operator=(const PdfiumLibrary&) = delete;

    // True when the pinned PDFium artifact is linked and initialized.
    bool isAvailable() const noexcept;

    // PDFium SDK version string (e.g. "154.0.8035.0"); empty when unavailable.
    std::string_view sdkVersion() const noexcept;

private:
    bool available_ = false;
    std::string sdkVersion_;
};

} // namespace fastpdf::pdfium