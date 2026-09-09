#pragma once

#include <string_view>

namespace fastpdf::core {

// Coarse error categories shared across the FastPDF boundaries. Each boundary
// maps its own detailed failures onto these codes; detailed diagnostics stay
// inside the boundary that produced them.
enum class ErrorCode {
    None = 0,
    InvalidArgument,
    PdfiumUnavailable,
    PdfiumInitFailed,
    PlatformError,
    InternalError,
};

// Human-readable (English) label for an ErrorCode. Never returns null.
std::string_view toString(ErrorCode code) noexcept;

} // namespace fastpdf::core