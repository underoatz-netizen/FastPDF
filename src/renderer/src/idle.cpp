#include "fastpdf/renderer/idle.h"

namespace fastpdf::renderer {

IdleDebounce::IdleDebounce(std::uint64_t idleThresholdMs) noexcept
    : idleThresholdMs_(idleThresholdMs) {}

void IdleDebounce::NoteActivity(std::uint64_t nowMs) noexcept {
    lastActivityMs_ = nowMs;
}

bool IdleDebounce::ShouldPromote(std::uint64_t nowMs) const noexcept {
    return nowMs - lastActivityMs_ >= idleThresholdMs_;
}

}  // namespace fastpdf::renderer
