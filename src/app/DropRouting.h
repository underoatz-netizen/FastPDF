#pragma once

#include <string>
#include <vector>

namespace fastpdf::app {

// True when |path| has a ".pdf" extension (case-insensitive). Used by the
// drag-and-drop routing so a single PDF file is opened through the drop
// path. Small pure helper, kept in the app boundary so it can be unit-tested
// without pulling in the window implementation.
bool IsPdfPath(const std::wstring& path) noexcept;

// True when |path| has a supported image extension (.png, .jpg, .jpeg, .bmp;
// case-insensitive). Used by Convert > Image to PDF and drag-and-drop routing.
bool IsSupportedImagePath(const std::wstring& path) noexcept;

// Filters a list of input file paths and returns only those with supported image extensions.
std::vector<std::wstring> FilterSupportedImagePaths(
    const std::vector<std::wstring>& paths) noexcept;

} // namespace fastpdf::app