// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <filesystem>
#include <fstream>
#include <charconv>
#include <string_view>
#include <algorithm>
#include <cstdint>

namespace sc_offline::menu_window {
struct Size { int width = 600, height = 900; };
struct Bounds { int left, top, right, bottom; };
constexpr int kMinWidth = 400, kMinHeight = 300, kMaxDimension = 32768;
inline bool Valid(Size size) {
    return size.width > 0 && size.height > 0 && size.width <= kMaxDimension && size.height <= kMaxDimension;
}
inline Bounds Fit(Size size, int x, int y, Bounds work) {
    const int64_t availableW = static_cast<int64_t>(work.right) - work.left;
    const int64_t availableH = static_cast<int64_t>(work.bottom) - work.top;
    if (availableW <= 0 || availableH <= 0) return {0, 0, 600, 900};
    if (!Valid(size)) size = {};
    const auto width = std::min<int64_t>(std::max<int64_t>(size.width, kMinWidth), availableW);
    const auto height = std::min<int64_t>(std::max<int64_t>(size.height, kMinHeight), availableH);
    const auto left = std::clamp<int64_t>(x, work.left, static_cast<int64_t>(work.right) - width);
    const auto top = std::clamp<int64_t>(y, work.top, static_cast<int64_t>(work.bottom) - height);
    return {static_cast<int>(left), static_cast<int>(top), static_cast<int>(left + width), static_cast<int>(top + height)};
}
inline bool Save(const std::filesystem::path& path, Size size) {
    if (!Valid(size)) return false;
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
    return width && height && Valid(size) ? size : Size{};
}
} // namespace sc_offline::menu_window
