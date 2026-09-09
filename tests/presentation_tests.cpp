// fastpdf_presentation_tests - focused, PDFium-free tests for the Phase-4
// presentation decision logic (fastpdf::app::presentation):
//   * keyboard shortcut -> action mapping (Next/Prev/First/Last/Exit/Toggle)
//     including that Ctrl+combinations are reserved for the normal view
//   * page-advance clamping at the document bounds
//   * the pre-render page set (previous/current/next/next+1) with the correct
//     priorities and no duplicates, including edge cases (first/last page,
//     single-page document)
//
// This module is pure (no PDFium, no Windows), so the test links only the
// Presentation sources.

#include <cstdint>
#include <utility>
#include <vector>

#include "Presentation.h"
#include "test_harness.h"

namespace {

using fastpdf::app::presentation::Action;
using fastpdf::app::presentation::ClampPage;
using fastpdf::app::presentation::MapKey;
using fastpdf::app::presentation::PreRenderPages;

// Windows virtual-key codes (kept local so the test stays self-contained).
constexpr std::uint32_t kVkRight = 0x27;
constexpr std::uint32_t kVkDown = 0x28;
constexpr std::uint32_t kVkSpace = 0x20;
constexpr std::uint32_t kVkLeft = 0x25;
constexpr std::uint32_t kVkUp = 0x26;
constexpr std::uint32_t kVkHome = 0x24;
constexpr std::uint32_t kVkEnd = 0x23;
constexpr std::uint32_t kVkEscape = 0x1B;
constexpr std::uint32_t kVkF11 = 0x7A;

} // namespace

FASTPDF_TEST(presentation_next_keys_map_to_next) {
    FASTPDF_CHECK(MapKey(kVkRight, false) == Action::Next);
    FASTPDF_CHECK(MapKey(kVkDown, false) == Action::Next);
    FASTPDF_CHECK(MapKey(kVkSpace, false) == Action::Next);
}

FASTPDF_TEST(presentation_prev_keys_map_to_prev) {
    FASTPDF_CHECK(MapKey(kVkLeft, false) == Action::Prev);
    FASTPDF_CHECK(MapKey(kVkUp, false) == Action::Prev);
}

FASTPDF_TEST(presentation_home_end_escape_f11) {
    FASTPDF_CHECK(MapKey(kVkHome, false) == Action::First);
    FASTPDF_CHECK(MapKey(kVkEnd, false) == Action::Last);
    FASTPDF_CHECK(MapKey(kVkEscape, false) == Action::Exit);
    FASTPDF_CHECK(MapKey(kVkF11, false) == Action::Toggle);
}

FASTPDF_TEST(presentation_unknown_key_is_none) {
    FASTPDF_CHECK(MapKey('A', false) == Action::None);
    FASTPDF_CHECK(MapKey(0x00, false) == Action::None);
}

FASTPDF_TEST(presentation_ctrl_combinations_are_reserved) {
    // Ctrl+combinations are reserved for the normal view and never treated as
    // presentation navigation.
    FASTPDF_CHECK(MapKey(kVkRight, true) == Action::None);
    FASTPDF_CHECK(MapKey(kVkSpace, true) == Action::None);
    FASTPDF_CHECK(MapKey(kVkEscape, true) == Action::None);
    FASTPDF_CHECK(MapKey(kVkF11, true) == Action::None);
}

FASTPDF_TEST(presentation_clamp_page_bounds) {
    FASTPDF_CHECK_EQ(ClampPage(0, 4), 0);
    FASTPDF_CHECK_EQ(ClampPage(3, 4), 3);
    FASTPDF_CHECK_EQ(ClampPage(-1, 4), 0);
    FASTPDF_CHECK_EQ(ClampPage(4, 4), 3);
    FASTPDF_CHECK_EQ(ClampPage(100, 4), 3);
    FASTPDF_CHECK_EQ(ClampPage(0, 0), 0);
    FASTPDF_CHECK_EQ(ClampPage(5, 0), 0);
}

FASTPDF_TEST(presentation_pre_render_set_middle_page) {
    // current=1 in a 4-page document: prev(0), current(1), next(2), next+1(3).
    const auto pages = PreRenderPages(1, 4);
    FASTPDF_CHECK_EQ(pages.size(), 4u);
    // Ordered by priority: current first (Visible=0), then prev/next
    // (Adjacent=1), then next+1 (Background=2).
    FASTPDF_CHECK_EQ(pages[0].first, 1);
    FASTPDF_CHECK_EQ(pages[0].second, 0);
    FASTPDF_CHECK_EQ(pages[1].first, 0);
    FASTPDF_CHECK_EQ(pages[1].second, 1);
    FASTPDF_CHECK_EQ(pages[2].first, 2);
    FASTPDF_CHECK_EQ(pages[2].second, 1);
    FASTPDF_CHECK_EQ(pages[3].first, 3);
    FASTPDF_CHECK_EQ(pages[3].second, 2);
}

FASTPDF_TEST(presentation_pre_render_set_first_page) {
    // current=0: no previous page; prev/next/next+1 clamped to the document.
    const auto pages = PreRenderPages(0, 4);
    FASTPDF_CHECK_EQ(pages.size(), 3u);
    FASTPDF_CHECK_EQ(pages[0].first, 0);
    FASTPDF_CHECK_EQ(pages[0].second, 0);
    FASTPDF_CHECK_EQ(pages[1].first, 1);
    FASTPDF_CHECK_EQ(pages[1].second, 1);
    FASTPDF_CHECK_EQ(pages[2].first, 2);
    FASTPDF_CHECK_EQ(pages[2].second, 2);
}

FASTPDF_TEST(presentation_pre_render_set_last_page) {
    // current=3 (last): no next/next+1; only prev and current.
    const auto pages = PreRenderPages(3, 4);
    FASTPDF_CHECK_EQ(pages.size(), 2u);
    FASTPDF_CHECK_EQ(pages[0].first, 3);
    FASTPDF_CHECK_EQ(pages[0].second, 0);
    FASTPDF_CHECK_EQ(pages[1].first, 2);
    FASTPDF_CHECK_EQ(pages[1].second, 1);
}

FASTPDF_TEST(presentation_pre_render_set_single_page) {
    // A single-page document: only the current page, no duplicates.
    const auto pages = PreRenderPages(0, 1);
    FASTPDF_CHECK_EQ(pages.size(), 1u);
    FASTPDF_CHECK_EQ(pages[0].first, 0);
    FASTPDF_CHECK_EQ(pages[0].second, 0);
}

FASTPDF_TEST(presentation_pre_render_set_empty_document) {
    FASTPDF_CHECK(PreRenderPages(0, 0).empty());
}

int main() {
    return fastpdf::test::RunAll();
}
