#include "fastpdf/core/version.h"

#ifndef FASTPDF_VERSION_STRING
#define FASTPDF_VERSION_STRING "0.0.0"
#endif

namespace fastpdf::core {

namespace {
constexpr std::string_view kVersionString = FASTPDF_VERSION_STRING;
} // namespace

Version version() noexcept {
    Version v{};
    int component = 0;
    int current = 0;
    for (const char c : kVersionString) {
        if (c == '.') {
            ++component;
            current = 0;
            continue;
        }
        if (c >= '0' && c <= '9') {
            current = current * 10 + (c - '0');
            if (component == 0) {
                v.major = current;
            } else if (component == 1) {
                v.minor = current;
            } else if (component == 2) {
                v.patch = current;
            }
        }
    }
    return v;
}

std::string_view versionString() noexcept { return kVersionString; }

} // namespace fastpdf::core