#pragma once

#include <cstdint>

namespace fastpdf::renderer {

// Tracks the "has the view been idle long enough to promote preview renders to
// final quality" decision. Pure logic (no clock, no threads): the owner feeds
// it a monotonic millisecond counter. The UI marks activity on every scroll /
// zoom / resize and polls ShouldPromote from a timer; once the view has been
// idle for |idleThresholdMs|, previews are promoted to final quality.
class IdleDebounce {
public:
    explicit IdleDebounce(std::uint64_t idleThresholdMs) noexcept;

    // Records that an interaction happened at monotonic time |nowMs|.
    void NoteActivity(std::uint64_t nowMs) noexcept;

    // True when |nowMs| - last activity >= threshold (i.e. a promotion to
    // final quality is due).
    bool ShouldPromote(std::uint64_t nowMs) const noexcept;

    std::uint64_t thresholdMs() const noexcept { return idleThresholdMs_; }

private:
    std::uint64_t idleThresholdMs_ = 0;
    std::uint64_t lastActivityMs_ = 0;
};

}  // namespace fastpdf::renderer
