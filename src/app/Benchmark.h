#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fastpdf::app {

// High-resolution startup/open/first-frame phase instrumentation.
//
// Active ONLY when the FASTPDF_BENCHMARK environment variable is set to a
// non-empty value. When active, records named phases with
// QueryPerformanceCounter timestamps (elapsed ms since construction) and
// writes a JSON object to the path in FASTPDF_BENCHMARK_OUT (default:
// "benchmark.json" next to the executable). Normal runs (env var unset) write
// nothing and add no measurable overhead.
class Benchmark {
public:
    Benchmark() noexcept;
    ~Benchmark();

    Benchmark(const Benchmark&) = delete;
    Benchmark& operator=(const Benchmark&) = delete;

    bool enabled() const noexcept { return enabled_; }

    // Records a named phase at the current high-resolution time (elapsed ms
    // since benchmark start). No-op when disabled.
    void Phase(const char* name) noexcept;

    // Writes the JSON object with the phases recorded so far. Safe to call
    // multiple times; the file is rewritten with the current phases.
    void Flush() noexcept;

private:
    struct PhaseRecord {
        std::string name;
        double elapsedMs = 0.0;
    };

    bool enabled_ = false;
    std::uint64_t startTicks_ = 0;
    std::uint64_t frequency_ = 0;
    std::vector<PhaseRecord> phases_;
    std::wstring outPath_;
};

} // namespace fastpdf::app