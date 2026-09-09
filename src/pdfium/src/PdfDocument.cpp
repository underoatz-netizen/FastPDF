// Real PDFium-backed implementation of the persistent-document primitive.
//
// This translation unit (with PdfiumRender.cpp and PdfiumLibrary.cpp) is the
// ONLY place that touches raw PDFium document/page/bitmap handles. Every raw
// FPDF_* call is serialized through the process-wide call gate
// (pdfium_gate.h); PdfDocument is creator-thread-affine so in practice all of
// its calls happen on the single render worker thread.

#include "fastpdf/pdfium/PdfDocument.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include <fpdfview.h>  // Raw PDFium header: confined to this boundary.
#include <fpdf_text.h>

#include "pdfium_gate.h"
#include "pdfium_internal.h"

// Opaque read-only memory-mapped PDF file. Defined in the global namespace to
// match the forward declaration in the public header. Holds the file/mapping
// handles and the mapped view; the destructor releases them in the correct
// order (view, then mapping, then file handle).
struct MappedPdfFile {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;

    ~MappedPdfFile() {
        if (data != nullptr) {
            UnmapViewOfFile(const_cast<std::uint8_t*>(data));
        }
        if (mapping != nullptr) {
            CloseHandle(mapping);
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
    }
};

namespace fastpdf::pdfium {

PdfSource PdfSource::Load(const std::wstring& path, OpenError& error) noexcept {
    error = OpenError::None;
    if (!internal::FileExists(path)) {
        error = OpenError::MissingFile;
        return {};
    }
    // Unicode (wide) open with read sharing plus write/delete sharing so the
    // file is never locked against other readers or replacers.
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = OpenError::Unreadable;
        return {};
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        CloseHandle(file);
        error = OpenError::Unreadable;
        return {};
    }

    auto mapped = std::make_shared<MappedPdfFile>();
    mapped->file = file;
    mapped->size = static_cast<std::size_t>(size.QuadPart);
    if (mapped->size == 0) {
        // Empty file: PDFium reports a format error on load; keep a valid
        // mapping with a null view (CreateFileMappingW fails on empty files).
        PdfSource source;
        source.mapping_ = std::move(mapped);
        return source;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0,
                                        nullptr);
    if (mapping == nullptr) {
        error = OpenError::Unreadable;
        return {};
    }
    mapped->mapping = mapping;

    const std::uint8_t* data = static_cast<const std::uint8_t*>(
        MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
    if (data == nullptr) {
        error = OpenError::Unreadable;
        return {};
    }
    mapped->data = data;

    PdfSource source;
    source.mapping_ = std::move(mapped);
    return source;
}

const std::uint8_t* PdfSource::data() const noexcept {
    return mapping_ ? mapping_->data : nullptr;
}

std::size_t PdfSource::size() const noexcept {
    return mapping_ ? mapping_->size : 0;
}

PdfDocument::PdfDocument(PdfSource source) noexcept
    : source_(std::move(source)) {
    if (!source_.isValid()) {
        openError_ = OpenError::Unreadable;
        return;
    }
    {
        detail::PdfiumCallGuard guard;
        document_ = FPDF_LoadMemDocument64(source_.data(), source_.size(),
                                           nullptr);
        if (document_ == nullptr) {
            openError_ = internal::MapLastError(FPDF_GetLastError());
        }
    }
}

PdfDocument::PdfDocument(
    std::shared_ptr<const std::vector<std::uint8_t>> source) noexcept
    : bytes_(std::move(source)) {
    if (!bytes_) {
        openError_ = OpenError::Unreadable;
        return;
    }
    {
        detail::PdfiumCallGuard guard;
        document_ = FPDF_LoadMemDocument64(bytes_->data(), bytes_->size(),
                                           nullptr);
        if (document_ == nullptr) {
            openError_ = internal::MapLastError(FPDF_GetLastError());
        }
    }
}

PdfDocument::~PdfDocument() {
    if (document_ != nullptr) {
        detail::PdfiumCallGuard guard;
        FPDF_CloseDocument(document_);
        document_ = nullptr;
    }
}

int PdfDocument::pageCount() const noexcept {
    if (document_ == nullptr) {
        return 0;
    }
    detail::PdfiumCallGuard guard;
    return FPDF_GetPageCount(document_);
}

bool PdfDocument::pageSize(int index, double& widthPoints,
                           double& heightPoints) const noexcept {
    if (document_ == nullptr || index < 0) {
        return false;
    }
    detail::PdfiumCallGuard guard;
    const int count = FPDF_GetPageCount(document_);
    if (index >= count) {
        return false;
    }
    // FPDF_GetPageSizeByIndexF reads the page geometry without loading the
    // page object, avoiding a per-page FPDF_LoadPage/ClosePage round trip.
    FS_SIZEF size{};
    if (!FPDF_GetPageSizeByIndexF(document_, index, &size)) {
        return false;
    }
    widthPoints = static_cast<double>(size.width);
    heightPoints = static_cast<double>(size.height);
    return true;
}

bool PdfDocument::renderPage(int index, int width, int height, int rotation,
                             Bitmap& out,
                             double& renderDurationMs) const noexcept {
    renderDurationMs = 0.0;
    if (document_ == nullptr || index < 0 || width <= 0 || height <= 0) {
        return false;
    }

    detail::PdfiumCallGuard guard;

    const int count = FPDF_GetPageCount(document_);
    if (index >= count) {
        return false;
    }
    FPDF_PAGE page = FPDF_LoadPage(document_, index);
    if (page == nullptr) {
        return false;
    }

    const auto renderStart = internal::Clock::now();

    // Create a BGRx (alpha unused) bitmap and render the page into it.
    FPDF_BITMAP bitmap = FPDFBitmap_Create(width, height, 0);
    if (bitmap == nullptr) {
        FPDF_ClosePage(page);
        return false;
    }

    // White background so blank pages are visible. |rotation| is clamped into
    // PDFium's 0..3 range (the viewer always renders at rotation 0; the key
    // still carries the field for future rotated views).
    const int clampedRotation = std::clamp(rotation, 0, 3);
    FPDFBitmap_FillRect(bitmap, 0, 0, width, height, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, page, 0, 0, width, height, clampedRotation,
                          0);

    const auto renderEnd = internal::Clock::now();
    renderDurationMs = std::chrono::duration<double, std::milli>(renderEnd -
                                                                 renderStart)
                           .count();

    const bool ok = internal::CopyBitmap(bitmap, width, height, out);

    FPDFBitmap_Destroy(bitmap);
    FPDF_ClosePage(page);
    return ok;
}

bool PdfDocument::renderPageToDC(int index, void* hdc, int destX, int destY,
                                int destWidth, int destHeight, int rotation) const noexcept {
    if (document_ == nullptr || index < 0 || hdc == nullptr ||
        destWidth <= 0 || destHeight <= 0) {
        return false;
    }

    detail::PdfiumCallGuard guard;

    const int count = FPDF_GetPageCount(document_);
    if (index >= count) {
        return false;
    }
    FPDF_PAGE page = FPDF_LoadPage(document_, index);
    if (page == nullptr) {
        return false;
    }

    const int clampedRotation = std::clamp(rotation, 0, 3);
    // FPDF_PRINTING flag (0x800) enables vector rendering to printer device contexts
    const FPDF_BOOL ok = FPDF_RenderPage(static_cast<HDC>(hdc), page,
                                         destX, destY, destWidth, destHeight,
                                         clampedRotation, FPDF_PRINTING);

    FPDF_ClosePage(page);
    return ok != 0;
}

std::vector<PageTextRange> PdfDocument::searchPage(int pageIndex,
                                                   const std::wstring& query,
                                                   bool matchCase) const noexcept {
    if (document_ == nullptr || pageIndex < 0 || query.empty()) {
        return {};
    }

    detail::PdfiumCallGuard guard;

    const int count = FPDF_GetPageCount(document_);
    if (pageIndex >= count) {
        return {};
    }

    FPDF_PAGE page = FPDF_LoadPage(document_, pageIndex);
    if (page == nullptr) {
        return {};
    }

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (textPage == nullptr) {
        FPDF_ClosePage(page);
        return {};
    }

    const int charCount = FPDFText_CountChars(textPage);
    if (charCount <= 0) {
        FPDFText_ClosePage(textPage);
        FPDF_ClosePage(page);
        return {};
    }

    // Prepare search query as null-terminated UTF-16
    unsigned long flags = 0;
    if (matchCase) {
        flags |= FPDF_MATCHCASE;
    }

    std::vector<unsigned short> u16Query;
    u16Query.reserve(query.size() + 1);
    for (wchar_t wc : query) {
        u16Query.push_back(static_cast<unsigned short>(wc));
    }
    u16Query.push_back(0);

    FPDF_SCHHANDLE searchHandle = FPDFText_FindStart(
        textPage,
        reinterpret_cast<FPDF_WIDESTRING>(u16Query.data()),
        flags,
        0);

    std::vector<PageTextRange> results;
    if (searchHandle != nullptr) {
        while (FPDFText_FindNext(searchHandle)) {
            const int matchStart = FPDFText_GetSchResultIndex(searchHandle);
            const int matchLen = FPDFText_GetSchCount(searchHandle);
            if (matchStart >= 0 && matchLen > 0) {
                results.push_back(PageTextRange{pageIndex, matchStart, matchLen});
            }
        }
        FPDFText_FindClose(searchHandle);
    }

    FPDFText_ClosePage(textPage);
    FPDF_ClosePage(page);
    return results;
}

std::vector<PdfRect> PdfDocument::getTextRects(int pageIndex,
                                               int charIndex,
                                               int charCount) const noexcept {
    if (document_ == nullptr || pageIndex < 0 || charIndex < 0 || charCount <= 0) {
        return {};
    }

    detail::PdfiumCallGuard guard;

    const int count = FPDF_GetPageCount(document_);
    if (pageIndex >= count) {
        return {};
    }

    FPDF_PAGE page = FPDF_LoadPage(document_, pageIndex);
    if (page == nullptr) {
        return {};
    }

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
    if (textPage == nullptr) {
        FPDF_ClosePage(page);
        return {};
    }

    const int totalChars = FPDFText_CountChars(textPage);
    if (charIndex >= totalChars) {
        FPDFText_ClosePage(textPage);
        FPDF_ClosePage(page);
        return {};
    }

    const int rectCount = FPDFText_CountRects(textPage, charIndex, charCount);
    std::vector<PdfRect> rects;
    if (rectCount > 0) {
        rects.reserve(static_cast<size_t>(rectCount));
        for (int r = 0; r < rectCount; ++r) {
            double left = 0.0, top = 0.0, right = 0.0, bottom = 0.0;
            if (FPDFText_GetRect(textPage, r, &left, &top, &right, &bottom)) {
                rects.push_back(PdfRect{left, top, right, bottom});
            }
        }
    }

    FPDFText_ClosePage(textPage);
    FPDF_ClosePage(page);
    return rects;
}

}  // namespace fastpdf::pdfium
