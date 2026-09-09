#pragma once

#include <objbase.h>  // CoInitializeEx / CoUninitialize

namespace fastpdf::platform::win {

// RAII wrapper for COM initialization on the calling thread.
// Uses COINIT_APARTMENTTHREADED, which is what Direct2D/DirectWrite need for
// a single-threaded UI.
class ComInitializer {
public:
    ComInitializer() noexcept;
    ~ComInitializer() noexcept;

    ComInitializer(const ComInitializer&) = delete;
    ComInitializer& operator=(const ComInitializer&) = delete;

    // True when this instance performed (or balanced) a successful
    // CoInitializeEx call and therefore owns a matching CoUninitialize.
    bool initialized() const noexcept { return initialized_; }

private:
    bool initialized_ = false;
};

} // namespace fastpdf::platform::win