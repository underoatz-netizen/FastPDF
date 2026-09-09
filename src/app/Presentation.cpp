#include "Presentation.h"

#include <algorithm>

namespace fastpdf::app::presentation {

namespace {

// Mirrors fastpdf::renderer::RenderPriority so this module stays decoupled.
constexpr int kPriorityVisible = 0;
constexpr int kPriorityAdjacent = 1;
constexpr int kPriorityBackground = 2;

} // namespace

Action MapKey(std::uint32_t vk, bool ctrl) noexcept {
    if (ctrl) {
        // Ctrl+combinations are reserved for the normal view.
        return Action::None;
    }
    switch (vk) {
        case 0x27: // VK_RIGHT
        case 0x28: // VK_DOWN
        case 0x20: // VK_SPACE
            return Action::Next;
        case 0x25: // VK_LEFT
        case 0x26: // VK_UP
            return Action::Prev;
        case 0x24: // VK_HOME
            return Action::First;
        case 0x23: // VK_END
            return Action::Last;
        case 0x1B: // VK_ESCAPE
            return Action::Exit;
        case 0x7A: // VK_F11
            return Action::Toggle;
        default:
            return Action::None;
    }
}

int ClampPage(int page, int pageCount) noexcept {
    if (pageCount <= 0) {
        return 0;
    }
    return std::clamp(page, 0, pageCount - 1);
}

std::vector<std::pair<int, int>> PreRenderPages(int current, int pageCount) noexcept {
    std::vector<std::pair<int, int>> result;
    if (pageCount <= 0) {
        return result;
    }
    const int cur = ClampPage(current, pageCount);

    // Ordered by priority: current (visible), prev/next (adjacent), next+1
    // (background). Each is clamped to the document.
    const int prev = cur - 1;
    const int next = cur + 1;
    const int next2 = cur + 2;

    result.push_back({cur, kPriorityVisible});
    if (prev >= 0) {
        result.push_back({prev, kPriorityAdjacent});
    }
    if (next < pageCount) {
        result.push_back({next, kPriorityAdjacent});
    }
    if (next2 < pageCount) {
        result.push_back({next2, kPriorityBackground});
    }
    return result;
}

} // namespace fastpdf::app::presentation
