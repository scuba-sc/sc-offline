// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <fstream>
#include <charconv>
#include <string_view>
#include <algorithm>
#include <cstdint>

namespace sc_offline::menu_window {
// Owned by the menu thread. The close callback must perform the real hide path.
struct EscapeInput {
    bool visible = false;
    bool pending = false;
    void Reset() { pending = false; }
    // TranslateMessage can queue this after keydown, including after the hide.
    bool Character(bool escape) const { return escape; }
    template<class Close>
    bool Key(bool escape, bool down, bool repeated, bool focused, Close close) {
        if (!visible || !focused) { Reset(); return false; }
        if (!escape) return false;
        if (down) { if (!repeated) pending = true; }
        else if (pending) { pending = false; visible = false; close(); }
        return true;
    }
};
// The saved size is the OUTER window rect (what GetWindowRect returns and SetWindowPos takes),
// normalised to 96 dpi, so frame thickness and monitor scale never feed back into it.
struct Size { int width = 1000, height = 800; };
struct Bounds { int left, top, right, bottom; };
constexpr int kMinWidth = 720, kMinHeight = 520, kMaxDimension = 32768;
constexpr int kMaxSavedDimension = 8192;   // a saved file beyond this is not a size this menu wrote
constexpr int kBaseDpi = 96;
constexpr int kMaxPercent = 90;            // of the work area of the monitor the window is on
inline bool Valid(Size size) {
    return size.width > 0 && size.height > 0 && size.width <= kMaxDimension && size.height <= kMaxDimension;
}
inline bool Sane(Size size) {
    return size.width >= kMinWidth && size.height >= kMinHeight
        && size.width <= kMaxSavedDimension && size.height <= kMaxSavedDimension;
}
inline int ScaleDpi(int value, int from, int to) {
    if (from <= 0 || to <= 0) { from = to = kBaseDpi; }
    const int64_t scaled = (static_cast<int64_t>(value) * to + from / 2) / from;
    return static_cast<int>(std::clamp<int64_t>(scaled, 0, kMaxDimension));
}
// Outer pixels at `dpi` -> the 96 dpi form that is saved; and back.
inline Size ToLogical(Size outer, int dpi) { return {ScaleDpi(outer.width, dpi, kBaseDpi), ScaleDpi(outer.height, dpi, kBaseDpi)}; }
inline Size ToOuter(Size logical, int dpi) { return {ScaleDpi(logical.width, kBaseDpi, dpi), ScaleDpi(logical.height, kBaseDpi, dpi)}; }
// Outer size clamped to [96 dpi minimum at this scale, 90% of the work area]; the minimum yields to
// a small work area.
inline Size Clamp(Size size, Bounds work, int dpi = kBaseDpi) {
    const int64_t availableW = static_cast<int64_t>(work.right) - work.left;
    const int64_t availableH = static_cast<int64_t>(work.bottom) - work.top;
    if (availableW <= 0 || availableH <= 0) return {};
    if (!Valid(size)) size = {};
    const auto maxW = std::max<int64_t>(availableW * kMaxPercent / 100, 1);
    const auto maxH = std::max<int64_t>(availableH * kMaxPercent / 100, 1);
    const auto minW = std::min<int64_t>(ScaleDpi(kMinWidth, kBaseDpi, dpi), maxW);
    const auto minH = std::min<int64_t>(ScaleDpi(kMinHeight, kBaseDpi, dpi), maxH);
    return {static_cast<int>(std::min<int64_t>(std::max<int64_t>(size.width, minW), maxW)),
            static_cast<int>(std::min<int64_t>(std::max<int64_t>(size.height, minH), maxH))};
}
inline Bounds Fit(Size size, int x, int y, Bounds work, int dpi = kBaseDpi) {
    const int64_t availableW = static_cast<int64_t>(work.right) - work.left;
    const int64_t availableH = static_cast<int64_t>(work.bottom) - work.top;
    if (availableW <= 0 || availableH <= 0) return {0, 0, kMinWidth, kMinHeight};
    size = Clamp(size, work, dpi);
    const int64_t width = size.width, height = size.height;
    const auto left = std::clamp<int64_t>(x, work.left, static_cast<int64_t>(work.right) - width);
    const auto top = std::clamp<int64_t>(y, work.top, static_cast<int64_t>(work.bottom) - height);
    return {static_cast<int>(left), static_cast<int>(top), static_cast<int>(left + width), static_cast<int>(top + height)};
}
// What is written when a resize ends: the window's outer size, clamped, at 96 dpi (never below the
// 96 dpi minimum, so a small monitor cannot save a size Load would reject).
inline Size Remember(Size outer, int dpi, Bounds work) {
    Size logical = ToLogical(Clamp(outer, work, dpi), dpi);
    logical.width = std::clamp(logical.width, kMinWidth, kMaxSavedDimension);
    logical.height = std::clamp(logical.height, kMinHeight, kMaxSavedDimension);
    return logical;
}
// What the window is given when shown: the saved 96 dpi size at this monitor's scale, clamped.
inline Size Restore(Size logical, int dpi, Bounds work) {
    if (!Valid(logical) || !Sane(logical)) logical = {};
    return Clamp(ToOuter(logical, dpi), work, dpi);
}
inline bool Save(const std::filesystem::path& path, Size size) {
    if (!Valid(size) || !Sane(size)) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << "[window]\nwidth=" << size.width << "\nheight=" << size.height << '\n';
    file.close();
    return !file.fail();
}
inline std::string_view Trim(std::string_view text) {
    const auto begin = text.find_first_not_of(" \t\r");
    if (begin == text.npos) return {};
    return text.substr(begin, text.find_last_not_of(" \t\r") - begin + 1);
}
inline Size Load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    char bytes[257];
    file.read(bytes, sizeof(bytes));
    if (file.bad() || file.gcount() > 256) return {};
    std::string_view text(bytes, static_cast<size_t>(file.gcount()));
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    Size size;
    bool section = false, width = false, height = false;
    while (!text.empty()) {
        const auto end = text.find('\n');
        const auto line = Trim(text.substr(0, end));
        text = end == text.npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line == "[window]") { section = true; continue; }
        const auto eq = line.find('=');
        if (!section || eq == line.npos) return {};
        const auto key = Trim(line.substr(0, eq)), value = Trim(line.substr(eq + 1));
        if (value.empty()) return {};
        int parsed = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) return {};
        if (key == "width" && !width) { size.width = parsed; width = true; }
        else if (key == "height" && !height) { size.height = parsed; height = true; }
        else return {};
    }
    return width && height && Valid(size) && Sane(size) ? size : Size{};
}
} // namespace sc_offline::menu_window
