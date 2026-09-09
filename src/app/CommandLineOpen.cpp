#include "CommandLineOpen.h"

namespace fastpdf::app {

std::wstring PdfPathFromCommandLine(int argc, const wchar_t* const* argv) noexcept {
    // argv[0] is the executable name (a full command line always starts with
    // it), so the explicit document path is argv[1] when present. A
    // no-argument launch has argc == 1 and must not open anything; an empty
    // argv[1] (e.g. FastPDF.exe "") is likewise treated as no document.
    if (argc >= 2 && argv != nullptr && argv[1] != nullptr && argv[1][0] != L'\0') {
        return std::wstring(argv[1]);
    }
    return std::wstring();
}

} // namespace fastpdf::app
