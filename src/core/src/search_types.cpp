#include "fastpdf/core/search_types.h"

#include <algorithm>

namespace fastpdf::core::search {

std::wstring SanitizeSearchQuery(std::wstring_view query) noexcept {
    std::wstring result;
    result.reserve(std::min(query.size(), kMaxQueryLength));
    for (wchar_t ch : query) {
        if (result.size() >= kMaxQueryLength) {
            break;
        }
        // Exclude control characters (< 0x20) and DEL (0x7F)
        if (ch >= 0x20 && ch != 0x7F) {
            result.push_back(ch);
        }
    }
    return result;
}

std::wstring FormatSearchCount(size_t currentIndex, size_t totalCount) noexcept {
    if (totalCount == 0) {
        return L"0 / 0";
    }
    // currentIndex is 0-based; display 1-based (clamped)
    size_t displayIndex = std::min(currentIndex + 1, totalCount);
    return std::to_wstring(displayIndex) + L" / " + std::to_wstring(totalCount);
}

int NextMatchIndex(int currentIndex, int totalCount, bool forward) noexcept {
    if (totalCount <= 0) {
        return -1;
    }
    if (forward) {
        if (currentIndex < 0 || currentIndex >= totalCount - 1) {
            return 0;
        }
        return currentIndex + 1;
    } else {
        if (currentIndex <= 0 || currentIndex >= totalCount) {
            return totalCount - 1;
        }
        return currentIndex - 1;
    }
}

} // namespace fastpdf::core::search
