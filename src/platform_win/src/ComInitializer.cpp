#include "fastpdf/platform/win/ComInitializer.h"

namespace fastpdf::platform::win {

ComInitializer::ComInitializer() noexcept {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // S_OK: initialized here. S_FALSE: already initialized with a compatible
    // mode (reference count incremented; we still own a matching uninit).
    // RPC_E_CHANGED_MODE: initialized with a different mode - nothing to undo.
    initialized_ = SUCCEEDED(hr);
}

ComInitializer::~ComInitializer() noexcept {
    if (initialized_) {
        CoUninitialize();
    }
}

} // namespace fastpdf::platform::win