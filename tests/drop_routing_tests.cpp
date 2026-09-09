// fastpdf_drop_routing_tests - exercises the drag-and-drop routing decision
// (single PDF opens; non-PDF and multi-file drops are ignored) without
// needing a window or PDFium. The routing predicate is a small pure function
// in the app boundary so it can be tested directly.

#include <windows.h>

#include <string>

#include "test_harness.h"

#include "DropRouting.h"

FASTPDF_TEST(drop_routing_accepts_single_pdf) {
    FASTPDF_CHECK(fastpdf::app::IsPdfPath(L"C:\\docs\\report.pdf"));
    FASTPDF_CHECK(fastpdf::app::IsPdfPath(L"report.PDF"));
    FASTPDF_CHECK(fastpdf::app::IsPdfPath(L"report.Pdf"));
    FASTPDF_CHECK(fastpdf::app::IsPdfPath(L"report.pdf"));
}

FASTPDF_TEST(drop_routing_rejects_non_pdf) {
    FASTPDF_CHECK(!fastpdf::app::IsPdfPath(L"C:\\docs\\report.txt"));
    FASTPDF_CHECK(!fastpdf::app::IsPdfPath(L"C:\\docs\\report"));
    FASTPDF_CHECK(!fastpdf::app::IsPdfPath(L"C:\\docs\\report.pdf.exe"));
    FASTPDF_CHECK(!fastpdf::app::IsPdfPath(L""));
}

int main() { return fastpdf::test::RunAll(); }