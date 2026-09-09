#pragma once

#include <string_view>

namespace fastpdf::core {

// Semantic version of the FastPDF application, compiled from the CMake
// project version (see FASTPDF_VERSION_STRING).
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// Returns the application version compiled into this binary.
Version version() noexcept;

// Returns the version as "major.minor.patch".
std::string_view versionString() noexcept;

} // namespace fastpdf::core