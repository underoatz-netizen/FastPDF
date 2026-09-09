#include <string>
#include <vector>

#include "test_harness.h"
#include <fastpdf/core/search_types.h>
#include <fastpdf/core/recent_files.h>

using namespace fastpdf::core::search;
using namespace fastpdf::core::recent;

FASTPDF_TEST(search_sanitize_query) {
    std::wstring query = L"Hello\r\n\tWorld! \x01\x1F";
    std::wstring sanitized = SanitizeSearchQuery(query);
    FASTPDF_CHECK_EQ(sanitized, L"HelloWorld! ");
}

FASTPDF_TEST(search_format_count) {
    FASTPDF_CHECK_EQ(FormatSearchCount(0, 0), L"0 / 0");
    FASTPDF_CHECK_EQ(FormatSearchCount(0, 10), L"1 / 10");
    FASTPDF_CHECK_EQ(FormatSearchCount(4, 10), L"5 / 10");
    FASTPDF_CHECK_EQ(FormatSearchCount(9, 10), L"10 / 10");
}

FASTPDF_TEST(search_next_prev_index) {
    FASTPDF_CHECK_EQ(NextMatchIndex(0, 0, true), -1);
    FASTPDF_CHECK_EQ(NextMatchIndex(0, 1, true), 0);
    FASTPDF_CHECK_EQ(NextMatchIndex(0, 1, false), 0);

    // Forward wrapping
    FASTPDF_CHECK_EQ(NextMatchIndex(0, 3, true), 1);
    FASTPDF_CHECK_EQ(NextMatchIndex(1, 3, true), 2);
    FASTPDF_CHECK_EQ(NextMatchIndex(2, 3, true), 0);

    // Backward wrapping
    FASTPDF_CHECK_EQ(NextMatchIndex(0, 3, false), 2);
    FASTPDF_CHECK_EQ(NextMatchIndex(2, 3, false), 1);
    FASTPDF_CHECK_EQ(NextMatchIndex(1, 3, false), 0);
}

FASTPDF_TEST(recent_files_add_order_and_limit) {
    RecentState state;
    state.version = 1;

    for (int i = 0; i < 15; ++i) {
        RecentEntry e;
        e.path = L"C:\\doc_" + std::to_wstring(i) + L".pdf";
        e.lastOpenedEpochSeconds = 1000 + i;
        e.lastPage = i;
        AddRecentEntry(state, e);
    }

    // Limit to 10
    FASTPDF_CHECK_EQ(state.entries.size(), static_cast<size_t>(10));
    // Most recent is doc_14
    FASTPDF_CHECK_EQ(state.entries[0].path, L"C:\\doc_14.pdf");
    FASTPDF_CHECK_EQ(state.entries[0].lastPage, 14);
    // Oldest in list is doc_5
    FASTPDF_CHECK_EQ(state.entries[9].path, L"C:\\doc_5.pdf");

    // Re-adding existing entry moves it to front
    RecentEntry readd;
    readd.path = L"c:\\doc_5.pdf"; // test case-insensitivity
    readd.lastOpenedEpochSeconds = 2000;
    readd.lastPage = 99;
    AddRecentEntry(state, readd);

    FASTPDF_CHECK_EQ(state.entries.size(), static_cast<size_t>(10));
    FASTPDF_CHECK_EQ(state.entries[0].path, L"c:\\doc_5.pdf");
    FASTPDF_CHECK_EQ(state.entries[0].lastPage, 99);
}

FASTPDF_TEST(recent_files_serialize_deserialize_roundtrip) {
    RecentState state;
    state.version = 1;

    RecentEntry e1;
    e1.path = L"C:\\Folder\\Doc With Spaces & Special \u00E9\u4E2D\u6587.pdf";
    e1.lastOpenedEpochSeconds = 1700000000;
    e1.lastPage = 42;
    e1.anchor.page = 42;
    e1.anchor.nx = 0.25;
    e1.anchor.ny = 0.75;
    e1.anchor.viewportX = 100.0;
    e1.anchor.viewportY = 150.0;
    e1.zoomMode = fastpdf::core::layout::FitMode::FitPage;
    e1.zoomPercent = 125.5;
    AddRecentEntry(state, e1);

    RecentEntry e2;
    e2.path = L"D:\\Simple.pdf";
    e2.lastOpenedEpochSeconds = 1700000100;
    e2.lastPage = 1;
    e2.zoomMode = fastpdf::core::layout::FitMode::Custom;
    e2.zoomPercent = 200.0;
    AddRecentEntry(state, e2);

    std::string text = SerializeRecentState(state);
    FASTPDF_CHECK(!text.empty());

    RecentState restored;
    FASTPDF_CHECK(DeserializeRecentState(text, restored));
    FASTPDF_CHECK_EQ(restored.version, 1u);
    FASTPDF_CHECK_EQ(restored.entries.size(), static_cast<size_t>(2));

    // Order: e2 was added second so it is at index 0, e1 at index 1
    FASTPDF_CHECK_EQ(restored.entries[0].path, e2.path);
    FASTPDF_CHECK_EQ(restored.entries[0].lastPage, 1);
    FASTPDF_CHECK_EQ(static_cast<int>(restored.entries[0].zoomMode), static_cast<int>(fastpdf::core::layout::FitMode::Custom));

    FASTPDF_CHECK_EQ(restored.entries[1].path, e1.path);
    FASTPDF_CHECK_EQ(restored.entries[1].lastPage, 42);
    FASTPDF_CHECK_EQ(restored.entries[1].anchor.page, 42);
    FASTPDF_CHECK(std::abs(restored.entries[1].anchor.nx - 0.25) < 1e-4);
    FASTPDF_CHECK(std::abs(restored.entries[1].anchor.ny - 0.75) < 1e-4);
    FASTPDF_CHECK(std::abs(restored.entries[1].anchor.viewportX - 100.0) < 1e-4);
    FASTPDF_CHECK(std::abs(restored.entries[1].anchor.viewportY - 150.0) < 1e-4);
}

FASTPDF_TEST(recent_files_remove_entry) {
    RecentState state;
    RecentEntry e;
    e.path = L"C:\\Test.pdf";
    AddRecentEntry(state, e);
    FASTPDF_CHECK_EQ(state.entries.size(), static_cast<size_t>(1));

    FASTPDF_CHECK(RemoveRecentEntry(state, L"c:\\test.pdf"));
    FASTPDF_CHECK_EQ(state.entries.size(), static_cast<size_t>(0));
    FASTPDF_CHECK(!RemoveRecentEntry(state, L"c:\\test.pdf"));
}

int main() {
    return fastpdf::test::RunAll();
}
