#include "pdfium_gate.h"

namespace fastpdf::pdfium::detail {

// Function-local static guarantees deterministic construction before first use
// (C++11 magic statics) and destruction at process exit, with no static
// initialization-order concerns. The gate is only ever destroyed after every
// PdfiumLibrary instance has run FPDF_DestroyLibrary (library shutdown happens
// before static destructors run).
std::mutex& pdfiumGate() noexcept {
    static std::mutex gate;
    return gate;
}

}  // namespace fastpdf::pdfium::detail
