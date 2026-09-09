#include "fastpdf/renderer/bitmap.h"

#include <limits>

namespace fastpdf::renderer {

std::size_t Bitmap::sizeBytes() const noexcept {
    if (data.empty() || width <= 0 || height <= 0 || stride <= 0) {
        return 0;
    }
    // Overflow-safe stride * height.
    const std::size_t strideBytes = static_cast<std::size_t>(stride);
    const std::size_t rows = static_cast<std::size_t>(height);
    if (strideBytes != 0 &&
        strideBytes > std::numeric_limits<std::size_t>::max() / rows) {
        return 0;
    }
    return strideBytes * rows;
}

}  // namespace fastpdf::renderer
