#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>

#include "fastpdf/core/layout.h"

namespace fastpdf::core::recent {

inline constexpr size_t kMaxRecentFiles = 10;
inline constexpr uint32_t kCurrentStateVersion = 1;

struct RecentEntry {
    std::wstring path;
    int64_t lastOpenedEpochSeconds = 0;
    int lastPage = 0;
    fastpdf::core::layout::ViewAnchor anchor;
    fastpdf::core::layout::FitMode zoomMode = fastpdf::core::layout::FitMode::FitWidth;
    double zoomPercent = 100.0;
};

struct RecentState {
    uint32_t version = kCurrentStateVersion;
    std::vector<RecentEntry> entries;
};

// Pure serialization: converts RecentState to UTF-8 formatted text
std::string SerializeRecentState(const RecentState& state);

// Pure deserialization: parses UTF-8 text into RecentState, handling unknown/invalid keys safely
bool DeserializeRecentState(std::string_view utf8Text, RecentState& outState);

// Pure list mutation: adds or updates |entry| at the front of the list,
// preserves at most kMaxRecentFiles, removes duplicates by case-insensitive path match.
void AddRecentEntry(RecentState& state, RecentEntry entry);

// Pure list mutation: removes entry matching |path| (case-insensitive)
bool RemoveRecentEntry(RecentState& state, const std::wstring& path);

} // namespace fastpdf::core::recent
