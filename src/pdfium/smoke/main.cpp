// fastpdf_pdfium_smoke - exercises PDFium initialization through the
// fastpdf_pdfium RAII adapter. Requires a valid pinned PDFium artifact
// (see third_party/pdfium/PDFIUM_LOCK.md). Exits 0 on success, 2 when the
// adapter reports PDFium unavailable.

#include <fastpdf/pdfium/PdfiumLibrary.h>

#include <cstdio>

int main() {
    fastpdf::pdfium::PdfiumLibrary library;
    if (!library.isAvailable()) {
        std::printf("fastpdf_pdfium_smoke: PDFium is not available.\n");
        return 2;
    }
    std::printf("fastpdf_pdfium_smoke: PDFium initialized, SDK version %.*s\n",
                static_cast<int>(library.sdkVersion().size()),
                library.sdkVersion().data());
    return 0;
}