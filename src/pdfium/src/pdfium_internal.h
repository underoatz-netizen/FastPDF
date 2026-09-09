#pragma once

// Internal-only shared helpers for the real PDFium-backed implementation.
// These live behind the fastpdf_pdfium boundary: the public headers never
// include raw PDFium types. Both PdfDocument.cpp and PdfiumRender.cpp use
// these; keep them in one place so the file I/O, error mapping, and bitmap
// copy logic have a single definition.

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <fpdfview.h>  // Raw PDFium header: confined to this boundary.

#include "fastpdf/pdfium/PdfiumLibrary.h"

namespace fastpdf::pdfium {
namespace internal {

using Clock = std::chrono::steady_clock;

inline bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// Reads the entire file at |path| into |outBytes| using the wide (Unicode)
// Windows file API. Returns false if the file cannot be opened or read.
//
// The buffer is owned by the caller and must remain valid for the lifetime of
// any PDFium document loaded from it (FPDF_LoadMemDocument64 references, not
// copies, the buffer).
inline bool ReadFileBytes(const std::wstring& path,
                          std::vector<std::uint8_t>& outBytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    struct FileCloser {
        HANDLE handle;
        ~FileCloser() {
            if (handle != INVALID_HANDLE_VALUE) {
                CloseHandle(handle);
            }
        }
    } closer{file};

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        return false;
    }
    if (static_cast<unsigned long long>(size.QuadPart) >
        static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
        return false;  // File too large to hold in memory.
    }
    const size_t fileSize = static_cast<size_t>(size.QuadPart);
    outBytes.resize(fileSize);
    if (fileSize == 0) {
        return true;  // Empty file; PDFium reports a format error on load.
    }

    size_t totalRead = 0;
    while (totalRead < fileSize) {
        const DWORD toRead = static_cast<DWORD>(std::min<size_t>(
            fileSize - totalRead, std::numeric_limits<DWORD>::max()));
        DWORD bytesRead = 0;
        if (!ReadFile(file, outBytes.data() + totalRead, toRead, &bytesRead,
                      nullptr) ||
            bytesRead == 0) {
            return false;
        }
        totalRead += bytesRead;
    }
    return true;
}

inline OpenError MapLastError(unsigned long pdfiumError) {
    switch (pdfiumError) {
        case FPDF_ERR_SUCCESS:
            return OpenError::None;
        case FPDF_ERR_FILE:
            return OpenError::Unreadable;
        case FPDF_ERR_FORMAT:
            return OpenError::Corrupted;
        case FPDF_ERR_PASSWORD:
            return OpenError::PasswordRequired;
        case FPDF_ERR_SECURITY:
            return OpenError::UnsupportedSecurity;
        default:
            return OpenError::Unknown;
    }
}

// Copies a PDFium bitmap's buffer into an immutable value Bitmap. Returns
// false (leaving |out| empty) if the buffer is unusable.
inline bool CopyBitmap(FPDF_BITMAP bitmap, int width, int height, Bitmap& out) {
    const int stride = FPDFBitmap_GetStride(bitmap);
    const unsigned char* buffer =
        static_cast<const unsigned char*>(FPDFBitmap_GetBuffer(bitmap));
    if (buffer == nullptr || stride <= 0 || width <= 0 || height <= 0) {
        return false;
    }
    out.width = width;
    out.height = height;
    out.stride = stride;
    out.data.resize(static_cast<size_t>(stride) * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        std::copy_n(buffer + static_cast<size_t>(y) * static_cast<size_t>(stride),
                    static_cast<size_t>(stride),
                    out.data.begin() +
                        static_cast<size_t>(y) * static_cast<size_t>(stride));
    }
    return true;
}

// Fits a page of |pageWidth| x |pageHeight| points into a target box of
// |targetWidth| x |targetHeight| pixels, preserving aspect ratio. Returns the
// pixel dimensions to render at (>= 1 x >= 1).
inline void ComputeFitSize(double pageWidth, double pageHeight, int targetWidth,
                           int targetHeight, int& outWidth, int& outHeight) {
    if (pageWidth <= 0.0 || pageHeight <= 0.0 || targetWidth <= 0 ||
        targetHeight <= 0) {
        outWidth = 1;
        outHeight = 1;
        return;
    }
    const double scale = std::min(
        static_cast<double>(targetWidth) / pageWidth,
        static_cast<double>(targetHeight) / pageHeight);
    outWidth = std::max(1, static_cast<int>(std::lround(pageWidth * scale)));
    outHeight = std::max(1, static_cast<int>(std::lround(pageHeight * scale)));
}

}  // namespace internal
}  // namespace fastpdf::pdfium
