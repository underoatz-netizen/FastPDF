#pragma once

#include <vector>

namespace fastpdf::pdfium {

struct PdfRect {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
};

// Character range on a specific PDF page
struct PageTextRange {
    int pageIndex = 0;
    int charIndex = 0;
    int charCount = 0;
};

// Batch of search matches returned from searching a page
struct PageSearchResults {
    int pageIndex = 0;
    std::vector<PageTextRange> matches;
};

} // namespace fastpdf::pdfium
