// Focused tests for the manual update check (pure parsing/comparison logic).
// No network, no window, no PDFium: real HTTP cannot be unit-tested, so the
// WinHTTP fetch path is exercised only through code review and manual runs.

#include "UpdateCheck.h"

#include <string>

#include "test_harness.h"

using namespace fastpdf::app::update;

FASTPDF_TEST(update_parse_plain_version) {
    ReleaseVersion v{};
    FASTPDF_CHECK(TryParseReleaseVersion("0.2.0", v));
    FASTPDF_CHECK_EQ(v.major, 0);
    FASTPDF_CHECK_EQ(v.minor, 2);
    FASTPDF_CHECK_EQ(v.patch, 0);
}

FASTPDF_TEST(update_parse_leading_v_versions) {
    ReleaseVersion v{};
    FASTPDF_CHECK(TryParseReleaseVersion("v0.3.0", v));
    FASTPDF_CHECK_EQ(v.major, 0);
    FASTPDF_CHECK_EQ(v.minor, 3);
    FASTPDF_CHECK_EQ(v.patch, 0);
    FASTPDF_CHECK(TryParseReleaseVersion("V1.10.3", v));
    FASTPDF_CHECK_EQ(v.major, 1);
    FASTPDF_CHECK_EQ(v.minor, 10);
    FASTPDF_CHECK_EQ(v.patch, 3);
}

FASTPDF_TEST(update_parse_trims_whitespace) {
    ReleaseVersion v{};
    FASTPDF_CHECK(TryParseReleaseVersion("  v0.2.0 \r\n", v));
    FASTPDF_CHECK_EQ(v.major, 0);
    FASTPDF_CHECK_EQ(v.minor, 2);
    FASTPDF_CHECK_EQ(v.patch, 0);
}

FASTPDF_TEST(update_parse_short_versions_default_to_zero) {
    ReleaseVersion v{};
    FASTPDF_CHECK(TryParseReleaseVersion("1.2", v));
    FASTPDF_CHECK_EQ(v.major, 1);
    FASTPDF_CHECK_EQ(v.minor, 2);
    FASTPDF_CHECK_EQ(v.patch, 0);
    FASTPDF_CHECK(TryParseReleaseVersion("v3", v));
    FASTPDF_CHECK_EQ(v.major, 3);
    FASTPDF_CHECK_EQ(v.minor, 0);
    FASTPDF_CHECK_EQ(v.patch, 0);
}

FASTPDF_TEST(update_parse_rejects_malformed) {
    ReleaseVersion v{};
    FASTPDF_CHECK(!TryParseReleaseVersion("", v));
    FASTPDF_CHECK(!TryParseReleaseVersion("   ", v));
    FASTPDF_CHECK(!TryParseReleaseVersion("v", v));
    FASTPDF_CHECK(!TryParseReleaseVersion("abc", v));
    FASTPDF_CHECK(!TryParseReleaseVersion("1.2.3.4", v));
    FASTPDF_CHECK(!TryParseReleaseVersion("1..2", v));
    FASTPDF_CHECK(!TryParseReleaseVersion(".1.2", v));
    FASTPDF_CHECK(!TryParseReleaseVersion("1.2.", v));
    FASTPDF_CHECK(!TryParseReleaseVersion("v1.2-beta", v));
    FASTPDF_CHECK(!TryParseReleaseVersion("1.2.x", v));
    FASTPDF_CHECK(!TryParseReleaseVersion("1. 2.3", v));
}

FASTPDF_TEST(update_compare_orders_numerically) {
    const ReleaseVersion a{0, 2, 0};
    const ReleaseVersion b{0, 3, 0};
    const ReleaseVersion c{0, 2, 0};
    const ReleaseVersion d{1, 0, 0};
    const ReleaseVersion e{0, 10, 0};
    const ReleaseVersion f{0, 2, 0};
    const ReleaseVersion g{0, 2, 1};
    FASTPDF_CHECK(CompareVersions(a, b) < 0);
    FASTPDF_CHECK(CompareVersions(b, a) > 0);
    FASTPDF_CHECK_EQ(CompareVersions(a, c), 0);
    FASTPDF_CHECK(CompareVersions(b, d) < 0);
    FASTPDF_CHECK(CompareVersions(e, b) > 0);  // 10 > 3 numerically, not lexically.
    FASTPDF_CHECK(CompareVersions(f, g) < 0);
}

FASTPDF_TEST(update_compare_current_against_latest) {
    ReleaseVersion current{0, 2, 0};
    ReleaseVersion latest{0, 2, 0};
    FASTPDF_CHECK(!(CompareVersions(current, latest) < 0));  // Equal: current.
    FASTPDF_CHECK(TryParseReleaseVersion("v0.3.0", latest));
    FASTPDF_CHECK(CompareVersions(current, latest) < 0);  // Behind: update.
    FASTPDF_CHECK(TryParseReleaseVersion("v0.1.9", latest));
    FASTPDF_CHECK(!(CompareVersions(current, latest) < 0));  // Ahead: current.
}

FASTPDF_TEST(update_extract_tag_name_minimal) {
    std::string tag;
    FASTPDF_CHECK(TryExtractTagName(R"({"tag_name":"v0.3.0"})", tag));
    FASTPDF_CHECK_EQ(tag, std::string("v0.3.0"));
}

FASTPDF_TEST(update_extract_tag_name_with_surrounding_json) {
    std::string tag;
    const char* json =
        R"({"url":"https://api.github.com/repos/x/y/releases/1",)"
        R"("tag_name" : "v0.2.1", "name":"FastPDF 0.2.1", "draft":false})";
    FASTPDF_CHECK(TryExtractTagName(json, tag));
    FASTPDF_CHECK_EQ(tag, std::string("v0.2.1"));
}

FASTPDF_TEST(update_extract_tag_name_rejects_missing_or_bad) {
    std::string tag;
    FASTPDF_CHECK(!TryExtractTagName(R"({"name":"no tag here"})", tag));
    FASTPDF_CHECK(!TryExtractTagName("", tag));
    FASTPDF_CHECK(!TryExtractTagName(R"({"tag_name":""})", tag));
    FASTPDF_CHECK(!TryExtractTagName(R"({"tag_name":"v1.0.0)", tag));
    FASTPDF_CHECK(!TryExtractTagName(R"({"tag_name":42})", tag));
}

FASTPDF_TEST(update_format_version_round_trip) {
    ReleaseVersion v{};
    FASTPDF_CHECK(TryParseReleaseVersion("v1.10.3", v));
    FASTPDF_CHECK_EQ(FormatVersion(v), std::string("1.10.3"));
}

int main() { return fastpdf::test::RunAll(); }
