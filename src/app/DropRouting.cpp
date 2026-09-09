#include "DropRouting.h"

#include <windows.h>
#include <cwctype>

namespace fastpdf::app {

bool IsPdfPath(const std::wstring& path) noexcept {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return false;
    }
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return ext == L".pdf";
}

bool IsSupportedImagePath(const std::wstring& path) noexcept {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) {
        return false;
    }
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp";
}

std::vector<std::wstring> FilterSupportedImagePaths(
    const std::vector<std::wstring>& paths) noexcept {
    std::vector<std::wstring> result;
    result.reserve(paths.size());
    for (const auto& p : paths) {
        if (IsSupportedImagePath(p)) {
            result.push_back(p);
        }
    }
    return result;
}

} // namespace fastpdf::app