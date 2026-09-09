#pragma once

// Pure presentation-mode decision logic for FastPDF. Kept free of PDFium and
// Windows so it can be unit-tested without a renderer or a window. The app
// layer (AppWindow) feeds it raw input and page counts and applies the
// resulting actions; the renderer's RenderPriority values are mirrored here as
// plain ints so this module stays decoupled from fastpdf_renderer.

#include <cstdint>
#include <utility>
#include <vector>

namespace fastpdf::app::presentation {

// A presentation input action.
enum class Action {
    None,   // not a presentation input (ignore)
    Next,   // advance to the next page
    Prev,   // go back to the previous page
    First,  // jump to the first page
    Last,   // jump to the last page
    Exit,   // leave presentation mode
    Toggle, // enter/exit presentation (F11)
};

// Maps a Windows virtual-key code to a presentation action. |ctrl| is whether
// Ctrl is held; Ctrl+combinations are reserved for the normal view (e.g.
// Ctrl+O) and never treated as presentation navigation.
Action MapKey(std::uint32_t vk, bool ctrl) noexcept;

// Clamps |page| into [0, pageCount-1]. Returns 0 when pageCount <= 0.
int ClampPage(int page, int pageCount) noexcept;

// The pages to pre-render for a presentation at |current| in a document with
// |pageCount| pages: previous, current, next, and next+1 (each clamped to the
// document). Returns {pageIndex, priority} pairs where priority mirrors
// fastpdf::renderer::RenderPriority (0 = Visible, 1 = Adjacent, 2 = Background):
// the current page is highest priority, prev/next are adjacent, next+1 is
// background. Duplicates (e.g. a 1-page document) are removed.
std::vector<std::pair<int, int>> PreRenderPages(int current, int pageCount) noexcept;

} // namespace fastpdf::app::presentation
