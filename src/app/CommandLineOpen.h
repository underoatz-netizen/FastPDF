#pragma once

#include <string>

namespace fastpdf::app {

// Direct command-line PDF open decision.
//
// A file-association or direct launch passes the document as the first
// argument after the executable name. |argc|/|argv| must come from parsing the
// FULL command line (CommandLineToArgvW(GetCommandLineW(), &argc)): argv[0]
// is then always the executable, and argv[1] (when argc >= 2) is the explicit
// PDF path, already unquoted by CommandLineToArgvW (spaces and Unicode are
// preserved by that API). This helper returns that first document argument,
// or an empty string when there is none.
//
// wWinMain's pCmdLine is deliberately NOT used for this: it excludes the
// program name, and CommandLineToArgvW("") returns the executable path itself
// as argv[0]. Parsing the full command line instead means a no-argument launch
// simply reports argc == 1, so the app keeps its normal empty/ready state and
// can never open the executable as a PDF. Only the first document argument is
// used; any further arguments are ignored.
std::wstring PdfPathFromCommandLine(int argc, const wchar_t* const* argv) noexcept;

} // namespace fastpdf::app
