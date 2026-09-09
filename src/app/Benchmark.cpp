#include "Benchmark.h"

#include <windows.h>

#include <cstdint>
#include <string>

namespace fastpdf::app {

namespace {

std::wstring ExecutableDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    std::wstring path(buffer, length);
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return {};
    }
    return path.substr(0, slash);
}

} // namespace

Benchmark::Benchmark() noexcept {
    wchar_t env[16]{};
    const DWORD len = GetEnvironmentVariableW(L"FASTPDF_BENCHMARK", env, 16);
    if (len == 0 || len >= 16) {
        return; // Not explicitly enabled.
    }
    LARGE_INTEGER freq{};
    LARGE_INTEGER start{};
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&start) ||
        freq.QuadPart == 0) {
        return;
    }
    enabled_ = true;
    frequency_ = static_cast<std::uint64_t>(freq.QuadPart);
    startTicks_ = static_cast<std::uint64_t>(start.QuadPart);

    wchar_t out[MAX_PATH]{};
    const DWORD outLen =
        GetEnvironmentVariableW(L"FASTPDF_BENCHMARK_OUT", out, MAX_PATH);
    if (outLen > 0 && outLen < MAX_PATH) {
        outPath_ = out;
    } else {
        outPath_ = ExecutableDirectory() + L"\\benchmark.json";
    }
}

Benchmark::~Benchmark() { Flush(); }

void Benchmark::Phase(const char* name) noexcept {
    if (!enabled_) {
        return;
    }
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) {
        return;
    }
    const double ms =
        static_cast<double>(now.QuadPart - startTicks_) * 1000.0 /
        static_cast<double>(frequency_);
    phases_.push_back({name, ms});
}

void Benchmark::Flush() noexcept {
    if (!enabled_) {
        return;
    }

    std::string json = "{\"phases\":[";
    for (size_t i = 0; i < phases_.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        json += "{\"name\":\"" + phases_[i].name + "\",\"ms\":" +
                std::to_string(phases_[i].elapsedMs) + "}";
    }
    json += "]}";

    HANDLE file = CreateFileW(outPath_.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, json.data(), static_cast<DWORD>(json.size()), &written,
              nullptr);
    CloseHandle(file);
}

} // namespace fastpdf::app