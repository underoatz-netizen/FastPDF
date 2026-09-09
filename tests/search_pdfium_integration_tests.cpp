#include <windows.h>
#include <vector>
#include <string>

#include "test_harness.h"
#include <fastpdf/pdfium/PdfDocument.h>
#include <fastpdf/pdfium/PdfiumLibrary.h>
#include <fastpdf/pdfium/SearchTypes.h>

namespace {

// Minimal valid single-page PDF containing a Text object:
// Stream: BT /F1 24 Tf 100 700 Td (Hello Searchable World) Tj ET
std::vector<std::uint8_t> CreateSearchablePdf() {
    std::string pdf =
        "%PDF-1.4\n"
        "1 0 obj\n"
        "<< /Type /Catalog /Pages 2 0 R >>\n"
        "endobj\n"
        "2 0 obj\n"
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n"
        "endobj\n"
        "3 0 obj\n"
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792]\n"
        "   /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\n"
        "endobj\n"
        "4 0 obj\n"
        "<< /Length 52 >>\n"
        "stream\n"
        "BT\n"
        "/F1 24 Tf\n"
        "100 700 Td\n"
        "(Hello Searchable World) Tj\n"
        "ET\n"
        "endstream\n"
        "endobj\n"
        "5 0 obj\n"
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n"
        "endobj\n"
        "xref\n"
        "0 6\n"
        "0000000000 65535 f \n"
        "0000000009 00000 n \n"
        "0000000058 00000 n \n"
        "0000000115 00000 n \n"
        "0000000254 00000 n \n"
        "0000000356 00000 n \n"
        "trailer\n"
        "<< /Size 6 /Root 1 0 R >>\n"
        "startxref\n"
        "435\n"
        "%%EOF\n";

    return std::vector<std::uint8_t>(pdf.begin(), pdf.end());
}

// Scanned / blank PDF with zero text characters
std::vector<std::uint8_t> CreateEmptyPagePdf() {
    std::string pdf =
        "%PDF-1.4\n"
        "1 0 obj\n"
        "<< /Type /Catalog /Pages 2 0 R >>\n"
        "endobj\n"
        "2 0 obj\n"
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n"
        "endobj\n"
        "3 0 obj\n"
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] >>\n"
        "endobj\n"
        "xref\n"
        "0 4\n"
        "0000000000 65535 f \n"
        "0000000009 00000 n \n"
        "0000000058 00000 n \n"
        "0000000115 00000 n \n"
        "trailer\n"
        "<< /Size 4 /Root 1 0 R >>\n"
        "startxref\n"
        "193\n"
        "%%EOF\n";

    return std::vector<std::uint8_t>(pdf.begin(), pdf.end());
}

} // namespace

FASTPDF_TEST(search_pdf_incremental_match_and_rects) {
    fastpdf::pdfium::PdfiumLibrary lib;
    FASTPDF_CHECK(lib.isAvailable());

    auto pdfBytes = std::make_shared<std::vector<std::uint8_t>>(CreateSearchablePdf());
    fastpdf::pdfium::PdfDocument doc(pdfBytes);
    FASTPDF_CHECK(doc.isOpen());
    FASTPDF_CHECK_EQ(doc.pageCount(), 1);

    // Search for "Searchable"
    auto matches = doc.searchPage(0, L"Searchable", /*matchCase=*/false);
    FASTPDF_CHECK_EQ(matches.size(), static_cast<size_t>(1));
    FASTPDF_CHECK_EQ(matches[0].pageIndex, 0);
    FASTPDF_CHECK_EQ(matches[0].charCount, 10);

    // Case-sensitive test
    auto matchesCaseMatch = doc.searchPage(0, L"Searchable", /*matchCase=*/true);
    FASTPDF_CHECK_EQ(matchesCaseMatch.size(), static_cast<size_t>(1));

    auto matchesCaseMismatch = doc.searchPage(0, L"searchable", /*matchCase=*/true);
    FASTPDF_CHECK_EQ(matchesCaseMismatch.size(), static_cast<size_t>(0));

    // Non-existent term
    auto noMatches = doc.searchPage(0, L"NotFoundString", /*matchCase=*/false);
    FASTPDF_CHECK_EQ(noMatches.size(), static_cast<size_t>(0));

    // Get bounding rects for the match
    auto rects = doc.getTextRects(0, matches[0].charIndex, matches[0].charCount);
    FASTPDF_CHECK(!rects.empty());
    for (const auto& r : rects) {
        FASTPDF_CHECK(r.right > r.left);
        FASTPDF_CHECK(r.top > r.bottom);
    }
}

FASTPDF_TEST(search_scanned_pdf_yields_zero_results) {
    fastpdf::pdfium::PdfiumLibrary lib;
    FASTPDF_CHECK(lib.isAvailable());

    auto pdfBytes = std::make_shared<std::vector<std::uint8_t>>(CreateEmptyPagePdf());
    fastpdf::pdfium::PdfDocument doc(pdfBytes);
    FASTPDF_CHECK(doc.isOpen());

    // Searching a blank / scanned page returns 0 results cleanly without error
    auto matches = doc.searchPage(0, L"Hello", false);
    FASTPDF_CHECK_EQ(matches.size(), static_cast<size_t>(0));

    auto rects = doc.getTextRects(0, 0, 5);
    FASTPDF_CHECK_EQ(rects.size(), static_cast<size_t>(0));
}

int main() {
    return fastpdf::test::RunAll();
}
