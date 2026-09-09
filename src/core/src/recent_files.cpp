#include "fastpdf/core/recent_files.h"

#include <algorithm>
#include <cwctype>
#include <sstream>
#include <iomanip>

namespace fastpdf::core::recent {

namespace {

bool PathEqualsCaseInsensitive(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) {
            return false;
        }
    }
    return true;
}

// Simple base64 encoding/decoding for path string to avoid escaping / newline issues
std::string Base64Encode(std::string_view input) {
    static const char kChars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0;
    int valb = -6;
    for (uint8_t c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(kChars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) {
        out.push_back(kChars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    while (out.size() % 4 != 0) {
        out.push_back('=');
    }
    return out;
}

std::string Base64Decode(std::string_view input) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) {
        T[static_cast<unsigned char>(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i])] = i;
    }

    int val = 0;
    int valb = -8;
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    std::string utf8;
    for (wchar_t wc : wide) {
        if (wc < 0x80) {
            utf8.push_back(static_cast<char>(wc));
        } else if (wc < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | (wc >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xE0 | (wc >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
        }
    }
    return utf8;
}

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    std::wstring wide;
    size_t i = 0;
    while (i < utf8.size()) {
        unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            wide.push_back(static_cast<wchar_t>(c));
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= utf8.size()) break;
            wchar_t val = (c & 0x1F) << 6;
            val |= (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
            wide.push_back(val);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= utf8.size()) break;
            wchar_t val = (c & 0x0F) << 12;
            val |= (static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 6;
            val |= (static_cast<unsigned char>(utf8[i + 2]) & 0x3F);
            wide.push_back(val);
            i += 3;
        } else {
            // Skip 4-byte or invalid sequence for safe wchar_t
            i += 1;
        }
    }
    return wide;
}

} // namespace

std::string SerializeRecentState(const RecentState& state) {
    std::ostringstream oss;
    oss << "version=" << state.version << "\n";
    oss << "count=" << state.entries.size() << "\n";
    for (size_t i = 0; i < state.entries.size(); ++i) {
        const auto& e = state.entries[i];
        std::string utf8Path = WideToUtf8(e.path);
        std::string b64Path = Base64Encode(utf8Path);
        oss << "entry." << i << ".path=" << b64Path << "\n";
        oss << "entry." << i << ".time=" << e.lastOpenedEpochSeconds << "\n";
        oss << "entry." << i << ".page=" << e.lastPage << "\n";
        oss << "entry." << i << ".anchorPage=" << e.anchor.page << "\n";
        oss << "entry." << i << ".anchorNx=" << std::setprecision(10) << e.anchor.nx << "\n";
        oss << "entry." << i << ".anchorNy=" << std::setprecision(10) << e.anchor.ny << "\n";
        oss << "entry." << i << ".anchorVpX=" << std::setprecision(10) << e.anchor.viewportX << "\n";
        oss << "entry." << i << ".anchorVpY=" << std::setprecision(10) << e.anchor.viewportY << "\n";
        oss << "entry." << i << ".zoomMode=" << static_cast<int>(e.zoomMode) << "\n";
        oss << "entry." << i << ".zoomPercent=" << std::setprecision(6) << e.zoomPercent << "\n";
    }
    return oss.str();
}

bool DeserializeRecentState(std::string_view utf8Text, RecentState& outState) {
    outState.entries.clear();
    outState.version = kCurrentStateVersion;

    std::istringstream iss{std::string(utf8Text)};
    std::string line;

    // Temporary map of key -> value
    std::vector<RecentEntry> tempEntries;
    uint32_t version = 1;
    size_t count = 0;

    struct EntryBuilder {
        RecentEntry entry;
        bool hasPath = false;
    };
    std::vector<EntryBuilder> builders;

    while (std::getline(iss, line)) {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "version") {
            try {
                version = static_cast<uint32_t>(std::stoul(val));
            } catch (...) {}
        } else if (key == "count") {
            try {
                count = std::stoul(val);
                if (count > kMaxRecentFiles) {
                    count = kMaxRecentFiles;
                }
                builders.resize(count);
            } catch (...) {}
        } else if (key.rfind("entry.", 0) == 0) {
            // entry.N.prop
            size_t dot1 = 5; // after "entry"
            size_t dot2 = key.find('.', dot1 + 1);
            if (dot2 != std::string::npos) {
                try {
                    size_t idx = std::stoul(key.substr(dot1 + 1, dot2 - dot1 - 1));
                    std::string prop = key.substr(dot2 + 1);
                    if (idx >= builders.size()) {
                        builders.resize(std::min(idx + 1, kMaxRecentFiles));
                    }
                    if (idx < builders.size()) {
                        auto& b = builders[idx];
                        if (prop == "path") {
                            std::string dec = Base64Decode(val);
                            b.entry.path = Utf8ToWide(dec);
                            b.hasPath = !b.entry.path.empty();
                        } else if (prop == "time") {
                            b.entry.lastOpenedEpochSeconds = std::stoll(val);
                        } else if (prop == "page") {
                            b.entry.lastPage = std::stoi(val);
                        } else if (prop == "anchorPage") {
                            b.entry.anchor.page = std::stoi(val);
                        } else if (prop == "anchorNx") {
                            b.entry.anchor.nx = std::stod(val);
                        } else if (prop == "anchorNy") {
                            b.entry.anchor.ny = std::stod(val);
                        } else if (prop == "anchorVpX") {
                            b.entry.anchor.viewportX = std::stod(val);
                        } else if (prop == "anchorVpY") {
                            b.entry.anchor.viewportY = std::stod(val);
                        } else if (prop == "zoomMode") {
                            int zm = std::stoi(val);
                            if (zm >= 0 && zm <= 2) {
                                b.entry.zoomMode = static_cast<fastpdf::core::layout::FitMode>(zm);
                            }
                        } else if (prop == "zoomPercent") {
                            b.entry.zoomPercent = std::stod(val);
                        }
                    }
                } catch (...) {}
            }
        }
    }

    outState.version = version;
    for (auto& b : builders) {
        if (b.hasPath && !b.entry.path.empty()) {
            outState.entries.push_back(std::move(b.entry));
            if (outState.entries.size() >= kMaxRecentFiles) {
                break;
            }
        }
    }
    return true;
}

void AddRecentEntry(RecentState& state, RecentEntry entry) {
    if (entry.path.empty()) {
        return;
    }
    // Remove if already present (case-insensitive)
    RemoveRecentEntry(state, entry.path);

    // Insert at front
    state.entries.insert(state.entries.begin(), std::move(entry));

    // Limit to kMaxRecentFiles
    if (state.entries.size() > kMaxRecentFiles) {
        state.entries.resize(kMaxRecentFiles);
    }
}

bool RemoveRecentEntry(RecentState& state, const std::wstring& path) {
    auto it = std::remove_if(state.entries.begin(), state.entries.end(),
        [&path](const RecentEntry& e) {
            return PathEqualsCaseInsensitive(e.path, path);
        });
    if (it != state.entries.end()) {
        state.entries.erase(it, state.entries.end());
        return true;
    }
    return false;
}

} // namespace fastpdf::core::recent
