// fastpdf_command_line_open_tests - exercises the direct command-line PDF open
// decision without needing a window or PDFium. The decision is a small pure
// function in the app boundary so it can be tested directly.
//
// |argc|/|argv| model the result of CommandLineToArgvW(GetCommandLineW(), ...):
// argv[0] is the executable and the explicit document path is argv[1]. A
// no-argument launch reports argc == 1 and must never open anything (in
// particular never the executable itself). The reported symptom (association
// launches not opening the PDF, and an empty pCmdLine being parsed as the
// executable path) is guarded here, including one end-to-end test that parses
// a realistic full command line with CommandLineToArgvW.

#include <windows.h>
#include <shellapi.h>

#include <string>

#include "test_harness.h"

#include "CommandLineOpen.h"

// No-argument launch: the full command line holds only the executable
// (argc == 1), so no document is selected and the window stays empty/ready.
FASTPDF_TEST(no_argument_returns_empty) {
    const wchar_t* argv[] = {L"C:\\Tools\\FastPDF\\FastPDF.exe"};
    FASTPDF_CHECK(fastpdf::app::PdfPathFromCommandLine(1, argv).empty());
}

// Defensive: a null argv array is treated as no document.
FASTPDF_TEST(null_argv_returns_empty) {
    FASTPDF_CHECK(fastpdf::app::PdfPathFromCommandLine(2, nullptr).empty());
}

// A single explicit path at argv[1] is returned verbatim.
FASTPDF_TEST(single_path_is_returned) {
    const wchar_t* argv[] = {L"C:\\Tools\\FastPDF\\FastPDF.exe", L"C:\\docs\\report.pdf"};
    FASTPDF_CHECK_EQ(fastpdf::app::PdfPathFromCommandLine(2, argv),
                     std::wstring(L"C:\\docs\\report.pdf"));
}

// The executable at argv[0] is never selected as the document.
FASTPDF_TEST(executable_is_not_a_document) {
    const wchar_t* argv[] = {L"C:\\Tools\\FastPDF\\FastPDF.exe"};
    FASTPDF_CHECK(fastpdf::app::PdfPathFromCommandLine(1, argv) !=
                  std::wstring(L"C:\\Tools\\FastPDF\\FastPDF.exe"));
}

// An empty argv[1] (e.g. FastPDF.exe "") is treated as no path.
FASTPDF_TEST(empty_path_argument_returns_empty) {
    const wchar_t* argv[] = {L"C:\\Tools\\FastPDF\\FastPDF.exe", L""};
    FASTPDF_CHECK(fastpdf::app::PdfPathFromCommandLine(2, argv).empty());
}

// A path with spaces and Unicode is preserved exactly (CommandLineToArgvW
// already handled quoting/Unicode before this decision runs).
FASTPDF_TEST(unicode_spaced_path_is_preserved) {
    const wchar_t* path = L"C:\\Users\\\u0e2d\u0e31\u0e15\u0e21\\My "
                          L"Folder\\\u0e23\u0e32\u0e22\u0e07\u0e32\u0e19.pdf";
    const wchar_t* argv[] = {L"C:\\Tools\\FastPDF\\FastPDF.exe", path};
    FASTPDF_CHECK_EQ(fastpdf::app::PdfPathFromCommandLine(2, argv),
                     std::wstring(path));
}

// Multiple arguments: only the first document argument is used.
FASTPDF_TEST(multiple_arguments_use_first) {
    const wchar_t* argv[] = {L"FastPDF.exe", L"C:\\a.pdf", L"C:\\b.pdf"};
    FASTPDF_CHECK_EQ(fastpdf::app::PdfPathFromCommandLine(3, argv),
                     std::wstring(L"C:\\a.pdf"));
}

// End-to-end: parse a realistic FULL command line (quoted executable path
// with a space, then a quoted Unicode/spaced document path). CommandLineToArgvW
// puts the executable at argv[0] and the unquoted document at argv[1]; the
// decision must select the document, never the executable.
FASTPDF_TEST(full_command_line_parse_selects_document) {
    const wchar_t* doc = L"C:\\Users\\\u0e2d\u0e31\u0e15\u0e21\\My Folder\\"
                         L"\u0e23\u0e32\u0e22\u0e07\u0e32\u0e19.pdf";
    const std::wstring cmdLine =
        L"\"C:\\Program Files\\FastPDF\\FastPDF.exe\" \"" + std::wstring(doc) + L"\"";
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine.c_str(), &argc);
    FASTPDF_CHECK(argv != nullptr);
    if (argv == nullptr) {
        return;
    }
    FASTPDF_CHECK_EQ(argc, 2);
    FASTPDF_CHECK_EQ(fastpdf::app::PdfPathFromCommandLine(argc, argv),
                     std::wstring(doc));
    LocalFree(argv);
}

// End-to-end: the EXACT command shape a real Windows file association builds
// from a "shell\open\command" value of "...FastPDF.exe" "%1". The executable
// path itself contains spaces (a "Program Files"/dist-style install directory)
// AND the document path contains spaces plus Thai Unicode. This is the genuine
// Explorer double-click case (verified against the live registry association on
// the affected machine). CommandLineToArgvW must yield argc == 2 with the fully
// unquoted document at argv[1]; the decision must select that document, never
// the executable and never a space-split fragment of the path.
FASTPDF_TEST(association_command_line_selects_document) {
    // "E:\Projects\OZ PDF Viewer\dist\FastPDF-0.1.1-win-x64\FastPDF.exe"
    const wchar_t* exe =
        L"E:\\Projects\\OZ PDF Viewer\\dist\\FastPDF-0.1.1-win-x64\\FastPDF.exe";
    // C:\Users\USER\AppData\Local\Temp\FastPDF Assoc Test\รายงาน ทดสอบ 1.pdf
    const wchar_t* doc =
        L"C:\\Users\\USER\\AppData\\Local\\Temp\\FastPDF Assoc Test\\"
        L"\u0e23\u0e32\u0e22\u0e07\u0e32\u0e19 \u0e17\u0e14\u0e2a\u0e2d\u0e1a 1.pdf";
    const std::wstring cmdLine =
        std::wstring(L"\"") + exe + L"\"" + L" \"" + doc + L"\"";
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine.c_str(), &argc);
    FASTPDF_CHECK(argv != nullptr);
    if (argv == nullptr) {
        return;
    }
    // The spaces inside both quoted tokens must NOT split into extra argv.
    FASTPDF_CHECK_EQ(argc, 2);
    FASTPDF_CHECK_EQ(std::wstring(argv[0]), std::wstring(exe));
    FASTPDF_CHECK_EQ(fastpdf::app::PdfPathFromCommandLine(argc, argv),
                     std::wstring(doc));
    LocalFree(argv);
}

// End-to-end: the no-argument shape of a full command line (executable only)
// selects no document. This is the case the old pCmdLine indexing got wrong:
// CommandLineToArgvW("") returns the executable as argv[0], but a full command
// line keeps argc == 1 and can never select a document.
FASTPDF_TEST(full_command_line_no_argument_selects_nothing) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(L"C:\\Tools\\FastPDF\\FastPDF.exe", &argc);
    FASTPDF_CHECK(argv != nullptr);
    if (argv == nullptr) {
        return;
    }
    FASTPDF_CHECK_EQ(argc, 1);
    FASTPDF_CHECK(fastpdf::app::PdfPathFromCommandLine(argc, argv).empty());
    LocalFree(argv);
}

int main() { return fastpdf::test::RunAll(); }
