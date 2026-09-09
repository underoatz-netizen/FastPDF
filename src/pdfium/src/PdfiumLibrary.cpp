#include "fastpdf/pdfium/PdfiumLibrary.h"

#include <atomic>
#include <string_view>

#include <fpdfview.h>  // Raw PDFium header: confined to this boundary.

#include "pdfium_gate.h"

namespace fastpdf::pdfium {

namespace {
// FPDF_InitLibrary/FPDF_DestroyLibrary are process-wide and not reference
// counted by PDFium itself. A file-local counter guards the pairing so that
// multiple adapter instances (or early/late destruction) cannot corrupt the
// library state. Together with the call gate (pdfium_gate.h) this is the only
// mutable state in this boundary.
std::atomic<int> g_libraryRefCount{0};

// PDFium removed FPDF_GetSDKVersion() upstream (absent from public/fpdfview.h
// on chromium/8035 and from the DLL exports). The artifact is pinned and
// verified (see third_party/pdfium/PDFIUM_LOCK.md; the archive's VERSION file
// reports MAJOR=154 MINOR=0 BUILD=8035 PATCH=0), so the SDK version is a
// compile-time constant matching the pinned release.
constexpr std::string_view kPinnedSdkVersion = "154.0.8035.0";
} // namespace

PdfiumLibrary::PdfiumLibrary() noexcept {
    const int previous = g_libraryRefCount.fetch_add(1);
    if (previous == 0) {
        // Init/shutdown are also raw PDFium calls and go through the gate.
        detail::PdfiumCallGuard guard;
        FPDF_InitLibrary();
    }

    sdkVersion_ = kPinnedSdkVersion;
    available_ = true;
}

PdfiumLibrary::~PdfiumLibrary() noexcept {
    if (available_) {
        if (g_libraryRefCount.fetch_sub(1) == 1) {
            detail::PdfiumCallGuard guard;
            FPDF_DestroyLibrary();
        }
        available_ = false;
    }
}

bool PdfiumLibrary::isAvailable() const noexcept { return available_; }

std::string_view PdfiumLibrary::sdkVersion() const noexcept { return sdkVersion_; }

} // namespace fastpdf::pdfium
