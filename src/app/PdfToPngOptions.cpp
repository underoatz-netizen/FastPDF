#include "PdfToPngOptions.h"

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <set>
#include <sstream>

namespace fastpdf::app::convert {

namespace {

std::wstring_view Trim(std::wstring_view s) noexcept {
    while (!s.empty() && std::iswspace(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::iswspace(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

// Helper to parse an unsigned integer from a wide string view.
bool ParseInt(std::wstring_view s, int& outVal) noexcept {
    s = Trim(s);
    if (s.empty()) {
        return false;
    }
    long long val = 0;
    for (wchar_t ch : s) {
        if (ch < L'0' || ch > L'9') {
            return false;
        }
        val = val * 10 + (ch - L'0');
        if (val > 1'000'000) { // arbitrary safe upper bound for page numbers
            return false;
        }
    }
    outVal = static_cast<int>(val);
    return true;
}

} // namespace

bool ParseCustomPageRange(std::wstring_view text, int totalPages,
                          std::vector<int>& outPageIndices) noexcept {
    outPageIndices.clear();
    if (totalPages <= 0) {
        return false;
    }

    std::set<int> uniquePages;
    std::size_t start = 0;

    while (start < text.size()) {
        const std::size_t commaPos = text.find(L',', start);
        const std::wstring_view token = (commaPos == std::wstring_view::npos)
                                            ? text.substr(start)
                                            : text.substr(start, commaPos - start);
        start = (commaPos == std::wstring_view::npos) ? text.size() : commaPos + 1;

        const std::wstring_view trimmedToken = Trim(token);
        if (trimmedToken.empty()) {
            continue;
        }

        const std::size_t dashPos = trimmedToken.find(L'-');
        if (dashPos != std::wstring_view::npos) {
            // Range: e.g. "1-5"
            const std::wstring_view p1Str = trimmedToken.substr(0, dashPos);
            const std::wstring_view p2Str = trimmedToken.substr(dashPos + 1);
            int p1 = 0;
            int p2 = 0;
            if (!ParseInt(p1Str, p1) || !ParseInt(p2Str, p2)) {
                return false; // Malformed range
            }
            if (p1 < 1 || p2 < 1) {
                return false;
            }
            if (p1 > p2) {
                std::swap(p1, p2);
            }
            // Clamp within [1, totalPages]
            const int clampedStart = std::max(1, p1);
            const int clampedEnd = std::min(totalPages, p2);
            for (int p = clampedStart; p <= clampedEnd; ++p) {
                uniquePages.insert(p - 1); // 0-based
            }
        } else {
            // Single page: e.g. "8"
            int p = 0;
            if (!ParseInt(trimmedToken, p)) {
                return false;
            }
            if (p >= 1 && p <= totalPages) {
                uniquePages.insert(p - 1); // 0-based
            }
        }
    }

    if (uniquePages.empty()) {
        return false;
    }

    outPageIndices.assign(uniquePages.begin(), uniquePages.end());
    return true;
}

bool ResolvePagesToConvert(PageSelectionMode mode, int currentPage,
                           std::wstring_view customRangeText, int totalPages,
                           std::vector<int>& outPageIndices) noexcept {
    outPageIndices.clear();
    if (totalPages <= 0) {
        return false;
    }

    switch (mode) {
        case PageSelectionMode::All: {
            outPageIndices.reserve(static_cast<std::size_t>(totalPages));
            for (int i = 0; i < totalPages; ++i) {
                outPageIndices.push_back(i);
            }
            return true;
        }
        case PageSelectionMode::Current: {
            if (currentPage >= 0 && currentPage < totalPages) {
                outPageIndices.push_back(currentPage);
                return true;
            }
            return false;
        }
        case PageSelectionMode::Custom: {
            return ParseCustomPageRange(customRangeText, totalPages, outPageIndices);
        }
        default:
            return false;
    }
}

std::wstring FormatPageFileName(std::wstring_view baseName, int pageIndex0,
                                int totalPages) noexcept {
    std::wstring base(baseName);
    if (base.empty()) {
        base = L"document";
    }

    // Determine padding width: standard is at least 3 digits.
    // If totalPages >= 1000, pad to match totalPages width (e.g. 4 for 1000).
    int width = 3;
    int temp = totalPages;
    int digits = 0;
    while (temp > 0) {
        digits++;
        temp /= 10;
    }
    if (digits > width) {
        width = digits;
    }

    // 1-based page number for human display / filename
    const int pageNum = pageIndex0 + 1;

    std::wostringstream oss;
    oss << base << L"_page_" << std::setw(width) << std::setfill(L'0') << pageNum << L".png";
    return oss.str();
}

std::wstring ExtractDocBaseName(const std::wstring& pdfPath) noexcept {
    if (pdfPath.empty()) {
        return L"document";
    }
    const std::size_t slash = pdfPath.find_last_of(L"\\/");
    std::wstring name = (slash == std::wstring::npos) ? pdfPath : pdfPath.substr(slash + 1);

    const std::size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        name = name.substr(0, dot);
    }
    if (name.empty()) {
        return L"document";
    }
    return name;
}

} // namespace fastpdf::app::convert
