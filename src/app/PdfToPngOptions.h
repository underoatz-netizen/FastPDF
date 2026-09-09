#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fastpdf::app::convert {

enum class QualityPreset {
    Standard, // 150 DPI
    High      // 300 DPI
};

enum class PageSelectionMode {
    All,
    Current,
    Custom
};

struct ConversionOptions {
    PageSelectionMode selectionMode = PageSelectionMode::All;
    int currentPage = 0;              // 0-indexed current page in viewer
    std::wstring customRangeText;     // e.g. "1-5, 8, 10"
    QualityPreset quality = QualityPreset::Standard;
    std::wstring outputFolder;
    std::wstring docBaseName = L"document";
};

// Returns standard DPI value for preset (Standard: 150, High: 300).
inline double DpiForPreset(QualityPreset quality) noexcept {
    switch (quality) {
        case QualityPreset::High:
            return 300.0;
        case QualityPreset::Standard:
        default:
            return 150.0;
    }
}

// Parses a 1-based page range string like "1-5, 8, 10" into sorted, deduplicated
// 0-based page indices clamped within [0, totalPages - 1].
// Returns true if parsing succeeded and at least one valid page was selected.
// If totalPages <= 0 or the string format is invalid or no pages match, returns false.
bool ParseCustomPageRange(std::wstring_view text, int totalPages,
                          std::vector<int>& outPageIndices) noexcept;

// Resolves the list of 0-based page indices according to the selection mode and totalPages.
// Returns true if at least one page is selected; false on invalid range or empty result.
bool ResolvePagesToConvert(PageSelectionMode mode, int currentPage,
                           std::wstring_view customRangeText, int totalPages,
                           std::vector<int>& outPageIndices) noexcept;

// Formats a zero-padded output PNG file name.
// Base name defaults to "document" if empty.
// Output format: "<baseName>_page_<paddedIndex>.png"
// Padded width is at least 3 digits, or wider if totalPages >= 1000.
std::wstring FormatPageFileName(std::wstring_view baseName, int pageIndex0,
                                int totalPages) noexcept;

// Extracts document base name without path and without extension.
// If empty or invalid, defaults to L"document".
std::wstring ExtractDocBaseName(const std::wstring& pdfPath) noexcept;

} // namespace fastpdf::app::convert
