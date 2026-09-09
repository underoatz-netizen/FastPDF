#include <windows.h>
#include <string>
#include <vector>

#include "test_harness.h"
#include "PdfToPngOptions.h"

using namespace fastpdf::app::convert;

FASTPDF_TEST(parse_custom_page_range_empty_or_invalid) {
    std::vector<int> pages;
    FASTPDF_CHECK(!ParseCustomPageRange(L"", 5, pages));
    FASTPDF_CHECK(!ParseCustomPageRange(L"   ", 5, pages));
    FASTPDF_CHECK(!ParseCustomPageRange(L"abc", 5, pages));
    FASTPDF_CHECK(!ParseCustomPageRange(L"1-", 5, pages));
    FASTPDF_CHECK(!ParseCustomPageRange(L"-5", 5, pages));
    FASTPDF_CHECK(!ParseCustomPageRange(L"0", 5, pages));
    FASTPDF_CHECK(!ParseCustomPageRange(L"1-0", 5, pages));
    FASTPDF_CHECK(!ParseCustomPageRange(L"10-20", 5, pages)); // outside totalPages
    FASTPDF_CHECK(!ParseCustomPageRange(L"1, 2, 3", 0, pages)); // totalPages 0
    FASTPDF_CHECK(!ParseCustomPageRange(L"1, 2, 3", -1, pages)); // negative totalPages
}

FASTPDF_TEST(parse_custom_page_range_valid_spec_example) {
    // Example from spec: "1-5, 8, 10"
    std::vector<int> pages;
    FASTPDF_CHECK(ParseCustomPageRange(L"1-5, 8, 10", 12, pages));
    // Expect 0-indexed: 0, 1, 2, 3, 4, 7, 9
    FASTPDF_CHECK_EQ(pages.size(), 7);
    FASTPDF_CHECK_EQ(pages[0], 0);
    FASTPDF_CHECK_EQ(pages[1], 1);
    FASTPDF_CHECK_EQ(pages[2], 2);
    FASTPDF_CHECK_EQ(pages[3], 3);
    FASTPDF_CHECK_EQ(pages[4], 4);
    FASTPDF_CHECK_EQ(pages[5], 7);
    FASTPDF_CHECK_EQ(pages[6], 9);
}

FASTPDF_TEST(parse_custom_page_range_overlap_and_ordering) {
    // Out of order and overlapping ranges: "5-3, 4, 1, 2-4"
    std::vector<int> pages;
    FASTPDF_CHECK(ParseCustomPageRange(L"5-3, 4, 1, 2-4", 10, pages));
    // Unique sorted 0-indexed: 0, 1, 2, 3, 4
    FASTPDF_CHECK_EQ(pages.size(), 5);
    FASTPDF_CHECK_EQ(pages[0], 0);
    FASTPDF_CHECK_EQ(pages[1], 1);
    FASTPDF_CHECK_EQ(pages[2], 2);
    FASTPDF_CHECK_EQ(pages[3], 3);
    FASTPDF_CHECK_EQ(pages[4], 4);
}

FASTPDF_TEST(parse_custom_page_range_clamping) {
    // Range extends beyond total pages: "3-15" with totalPages=5 -> should clamp to 3-5
    std::vector<int> pages;
    FASTPDF_CHECK(ParseCustomPageRange(L"3-15", 5, pages));
    FASTPDF_CHECK_EQ(pages.size(), 3);
    FASTPDF_CHECK_EQ(pages[0], 2);
    FASTPDF_CHECK_EQ(pages[1], 3);
    FASTPDF_CHECK_EQ(pages[2], 4);
}

FASTPDF_TEST(resolve_pages_to_convert_modes) {
    std::vector<int> pages;

    // Mode All
    FASTPDF_CHECK(ResolvePagesToConvert(PageSelectionMode::All, 2, L"", 3, pages));
    FASTPDF_CHECK_EQ(pages.size(), 3);
    FASTPDF_CHECK_EQ(pages[0], 0);
    FASTPDF_CHECK_EQ(pages[1], 1);
    FASTPDF_CHECK_EQ(pages[2], 2);

    // Mode Current
    FASTPDF_CHECK(ResolvePagesToConvert(PageSelectionMode::Current, 1, L"", 3, pages));
    FASTPDF_CHECK_EQ(pages.size(), 1);
    FASTPDF_CHECK_EQ(pages[0], 1);

    // Mode Current invalid page
    FASTPDF_CHECK(!ResolvePagesToConvert(PageSelectionMode::Current, 5, L"", 3, pages));

    // Mode Custom
    FASTPDF_CHECK(ResolvePagesToConvert(PageSelectionMode::Custom, 0, L"2", 3, pages));
    FASTPDF_CHECK_EQ(pages.size(), 1);
    FASTPDF_CHECK_EQ(pages[0], 1);
}

FASTPDF_TEST(format_page_file_name_padding) {
    // 3 digits padding standard
    FASTPDF_CHECK_EQ(FormatPageFileName(L"doc", 0, 10), L"doc_page_001.png");
    FASTPDF_CHECK_EQ(FormatPageFileName(L"doc", 9, 10), L"doc_page_010.png");
    FASTPDF_CHECK_EQ(FormatPageFileName(L"doc", 99, 100), L"doc_page_100.png");

    // >= 1000 pages: 4 digits padding
    FASTPDF_CHECK_EQ(FormatPageFileName(L"doc", 0, 1000), L"doc_page_0001.png");
    FASTPDF_CHECK_EQ(FormatPageFileName(L"doc", 999, 1000), L"doc_page_1000.png");

    // Empty base name defaults to "document"
    FASTPDF_CHECK_EQ(FormatPageFileName(L"", 2, 5), L"document_page_003.png");
}

FASTPDF_TEST(extract_doc_base_name) {
    FASTPDF_CHECK_EQ(ExtractDocBaseName(L"C:\\files\\report.pdf"), L"report");
    FASTPDF_CHECK_EQ(ExtractDocBaseName(L"/home/user/document.name.pdf"), L"document.name");
    FASTPDF_CHECK_EQ(ExtractDocBaseName(L"sample.pdf"), L"sample");
    FASTPDF_CHECK_EQ(ExtractDocBaseName(L""), L"document");
}

FASTPDF_TEST(dpi_for_preset) {
    FASTPDF_CHECK_EQ(DpiForPreset(QualityPreset::Standard), 150.0);
    FASTPDF_CHECK_EQ(DpiForPreset(QualityPreset::High), 300.0);
}

int main() {
    return fastpdf::test::RunAll();
}
