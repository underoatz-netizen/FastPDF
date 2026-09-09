#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fastpdf::core::search {

// Hard cap on search hits to bound memory allocation
inline constexpr size_t kMaxSearchResults = 5000;

// Maximum length of search query to prevent unbounded operations
inline constexpr size_t kMaxQueryLength = 256;

// Representation of a matched character range on a specific PDF page
struct SearchHit {
    int pageIndex = 0;       // 0-based page index
    int charIndex = 0;       // 0-based start character index on the page
    int charCount = 0;       // number of characters matched
};

// Search options / flags
struct SearchOptions {
    bool matchCase = false;
    bool matchWholeWord = false;
};

// Pure helper: normalize query (truncate to kMaxQueryLength, strip control characters except space)
std::wstring SanitizeSearchQuery(std::wstring_view query) noexcept;

// Pure helper: compute current/total match label, e.g. "12 / 48", "0 / 0", "1 / 1"
std::wstring FormatSearchCount(size_t currentIndex, size_t totalCount) noexcept;

// Pure helper: advance active result index (0-based) wrapping forwards or backwards
// Returns -1 if totalCount == 0.
int NextMatchIndex(int currentIndex, int totalCount, bool forward) noexcept;

} // namespace fastpdf::core::search
