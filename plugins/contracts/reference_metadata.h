// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sco::features::contracts::metadata {
constexpr uint32_t kMaxRows = 16384, kMaxReferences = 32;
constexpr size_t kMaxLineBytes = 8191, kMaxNameBytes = 255, kMaxReferenceBytes = 255;
struct Row {
    std::string_view guid, name;
    std::array<std::string_view, kMaxReferences> references{};
    uint32_t count = 0;
};
enum class Result { Ok, NotFound, Malformed, Limit };
inline bool Hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
inline bool Printable(std::string_view text) {
    for (unsigned char c : text) if (c < 32 || c == 127) return false;
    return true;
}
inline Result Parse(std::string_view bytes, uint32_t selected, Row& out) {
    if (bytes.find('\0') != bytes.npos) return Result::Malformed;
    uint32_t rows = 0;
    Row found{};
    while (!bytes.empty()) {
        const auto newline = bytes.find('\n');
        auto line = bytes.substr(0, newline);
        bytes.remove_prefix(newline == bytes.npos ? bytes.size() : newline + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.size() > kMaxLineBytes) return Result::Limit;
        if (line.empty() || line.front() == '#') continue;
        if (rows == kMaxRows) return Result::Limit;
        const auto nameEnd = line.find(' ', 33);
        if (line.size() < 35 || line[32] != ' ' || nameEnd == line.npos || nameEnd == 33)
            return Result::Malformed;
        Row row{};
        row.guid = line.substr(0, 32);
        for (char c : row.guid) if (!Hex(c)) return Result::Malformed;
        row.name = line.substr(33, nameEnd - 33);
        if (row.name.size() > kMaxNameBytes) return Result::Limit;
        if (!Printable(row.name)) return Result::Malformed;
        auto refs = line.substr(nameEnd + 1);
        if (refs.empty()) return Result::Malformed;
        for (;;) {
            const auto separator = refs.find('|');
            const auto ref = refs.substr(0, separator);
            if (ref.empty() || !Printable(ref)) return Result::Malformed;
            if (ref.size() > kMaxReferenceBytes) return Result::Limit;
            if (ref != "-") {
                if (row.count == kMaxReferences) return Result::Limit;
                row.references[row.count++] = ref;
            }
            if (separator == refs.npos) break;
            refs.remove_prefix(separator + 1);
        }
        if (rows++ == selected) found = row;
    }
    if (selected >= rows) return Result::NotFound;
    out = found;
    return Result::Ok;
}
}
