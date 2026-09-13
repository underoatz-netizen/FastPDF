// Pure, platform-independent tests for fastpdf_core. No PDFium required.

#include <fastpdf/core/error.h>
#include <fastpdf/core/version.h>

#include <string_view>

#include "test_harness.h"

using namespace fastpdf::core;

FASTPDF_TEST(version_string_matches_build_metadata) {
    FASTPDF_CHECK_EQ(versionString(), std::string_view(FASTPDF_VERSION_STRING));
}

FASTPDF_TEST(version_fields_are_consistent) {
    const Version v = version();
    FASTPDF_CHECK_EQ(v.major, 0);
    FASTPDF_CHECK_EQ(v.minor, 2);
    FASTPDF_CHECK_EQ(v.patch, 1);
}

FASTPDF_TEST(version_string_has_three_components) {
    int dots = 0;
    for (const char c : versionString()) {
        if (c == '.') {
            ++dots;
        }
    }
    FASTPDF_CHECK_EQ(dots, 2);
}

FASTPDF_TEST(error_to_string_is_never_empty) {
    for (int i = 0; i <= static_cast<int>(ErrorCode::InternalError); ++i) {
        const auto code = static_cast<ErrorCode>(i);
        FASTPDF_CHECK(!toString(code).empty());
    }
}

FASTPDF_TEST(error_to_string_none) {
    FASTPDF_CHECK_EQ(toString(ErrorCode::None), std::string_view("none"));
}

FASTPDF_TEST(error_to_string_pdfium_unavailable) {
    FASTPDF_CHECK_EQ(toString(ErrorCode::PdfiumUnavailable),
                     std::string_view("pdfium unavailable"));
}

int main() { return fastpdf::test::RunAll(); }