// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace sc_offline::menu_sdk {
inline size_t Utf8Width(const std::string& text, size_t at) {
    const auto first = static_cast<unsigned char>(text[at]);
    if (first == 0) return 0;
    if (first < 0x80) return 1;
    const size_t count = first >= 0xc2 && first <= 0xdf ? 2 : first >= 0xe0 && first <= 0xef ? 3 :
                         first >= 0xf0 && first <= 0xf4 ? 4 : 0;
    if (!count || at + count > text.size()) return 0;
    for (size_t i = 1; i < count; ++i)
        if ((static_cast<unsigned char>(text[at + i]) & 0xc0) != 0x80) return 0;
    const auto second = static_cast<unsigned char>(text[at + 1]);
    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
        (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) return 0;
    return count;
}
inline bool ValidUtf8(const std::string& text) {
    for (size_t at = 0; at < text.size();) {
        const auto width = Utf8Width(text, at);
        if (!width) return false;
        at += width;
    }
    return true;
}
// The producer's bounded UTF-8 buffer may end inside its final code point.
inline std::string CleanStatus(const std::string& text) {
    std::string result;
    for (size_t at = 0; at < text.size();) {
        const auto width = Utf8Width(text, at);
        if (width) { result.append(text, at, width); at += width; }
        else {
            result += "\xef\xbf\xbd";
            ++at;
            while (at < text.size() && (static_cast<unsigned char>(text[at]) & 0xc0) == 0x80) ++at;
        }
    }
    return result;
}
inline std::vector<std::string> TextChunks(const std::string& text, size_t limit) {
    std::vector<std::string> result;
    if (limit < 4 || !ValidUtf8(text)) return result;
    size_t begin = 0, at = 0;
    while (at < text.size()) {
        const auto width = Utf8Width(text, at);
        if (at + width - begin > limit) { result.push_back(text.substr(begin, at - begin)); begin = at; }
        at += width;
    }
    if (at > begin || result.empty()) result.push_back(text.substr(begin));
    return result;
}
struct Settings {
    bool show = true;
    int darkness = 74;
    double position = 0.5;
};
struct Snapshot {
    bool imageAvailable = false;
    uint32_t width = 0, height = 0;
    std::string pathUtf8;
    std::string statusUtf8;
    bool operator==(const Snapshot&) const = default;
};
enum class Control { Show, Darkness, Position };
struct Model {
    Settings settings;
    Snapshot snapshot;
    bool ImageAvailable() const { return snapshot.imageAvailable && snapshot.width && snapshot.height; }
    bool PositionAvailable() const {
        return ImageAvailable() && static_cast<double>(snapshot.width) / snapshot.height > 0.7;
    }
    bool Apply(Control control, double value) {
        if (!ImageAvailable() || !std::isfinite(value)) return false;
        if (control == Control::Show) {
            if (value != 0 && value != 1) return false;
            settings.show = value == 1;
            return true;
        }
        if (!settings.show) return false;
        if (control == Control::Darkness) {
            if (value < 20 || value > 95 || std::trunc(value) != value) return false;
            settings.darkness = static_cast<int>(value);
            return true;
        }
        if (control != Control::Position || !PositionAvailable() || value < 0 || value > 1) return false;
        settings.position = value;
        return true;
    }
};
} // namespace sc_offline::menu_sdk
