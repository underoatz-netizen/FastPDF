#include "fastpdf/core/error.h"

namespace fastpdf::core {

std::string_view toString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::None:
            return "none";
        case ErrorCode::InvalidArgument:
            return "invalid argument";
        case ErrorCode::PdfiumUnavailable:
            return "pdfium unavailable";
        case ErrorCode::PdfiumInitFailed:
            return "pdfium initialization failed";
        case ErrorCode::PlatformError:
            return "platform error";
        case ErrorCode::InternalError:
            return "internal error";
    }
    return "unknown error";
}

} // namespace fastpdf::core