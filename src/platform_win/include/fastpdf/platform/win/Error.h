#pragma once

#include <string>

#include <windows.h>

namespace fastpdf::platform::win {

// Formats an HRESULT into a human-readable wide string using the system
// message table. Never throws; returns a fallback on failure.
std::wstring FormatHresult(HRESULT hr) noexcept;

// Formats a Win32 error code (GetLastError) into a wide string.
std::wstring FormatWin32Error(DWORD error) noexcept;

} // namespace fastpdf::platform::win