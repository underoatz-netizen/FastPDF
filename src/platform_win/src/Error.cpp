#include "fastpdf/platform/win/Error.h"

namespace fastpdf::platform::win {

namespace {

std::wstring FormatMessageFromModule(DWORD error, HMODULE module) noexcept {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                        (module != nullptr ? FORMAT_MESSAGE_FROM_HMODULE : 0) |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD count = FormatMessageW(flags, module, error, 0,
                                       reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    if (count == 0 || buffer == nullptr) {
        return L"";
    }
    std::wstring message(buffer, count);
    LocalFree(buffer);
    // FormatMessage appends CR/LF; strip it.
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
    }
    return message;
}

} // namespace

std::wstring FormatHresult(HRESULT hr) noexcept {
    std::wstring message = FormatMessageFromModule(static_cast<DWORD>(hr), nullptr);
    if (message.empty()) {
        wchar_t fallback[64]{};
        swprintf_s(fallback, L"HRESULT 0x%08lX", static_cast<unsigned long>(hr));
        return fallback;
    }
    return message;
}

std::wstring FormatWin32Error(DWORD error) noexcept {
    std::wstring message = FormatMessageFromModule(error, nullptr);
    if (message.empty()) {
        wchar_t fallback[64]{};
        swprintf_s(fallback, L"Win32 error %lu", error);
        return fallback;
    }
    return message;
}

} // namespace fastpdf::platform::win