// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <sc_asop_selection.h>
#include <string>
#include <string_view>
#include <vector>

namespace offline::asop_selection {
// A failed parse leaves the caller's catalogue unchanged; input is a complete file snapshot.
inline bool ParseCatalogue(std::string_view bytes, std::vector<std::string>& out) {
    if (bytes.size() > SC_ASOP_SELECTION_MAX_FILE_BYTES || bytes.find('\0') != bytes.npos) return false;
    std::vector<std::string> result;
    size_t offset = 0;
    while (offset < bytes.size()) {
        const size_t end = bytes.find('\n', offset);
        auto line = bytes.substr(offset, end == bytes.npos ? bytes.size() - offset : end - offset);
        const size_t comment = line.find('#');
        if (comment != line.npos) line = line.substr(0, comment);
        auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
        while (!line.empty() && space(line.front())) line.remove_prefix(1);
        while (!line.empty() && space(line.back())) line.remove_suffix(1);
        if (!line.empty()) {
            if (line.size() > SC_ASOP_SELECTION_MAX_CLASS_BYTES || result.size() == SC_ASOP_SELECTION_MAX_CLASSES) return false;
            for (const char c : line)
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
            result.emplace_back(line);
        }
        if (end == bytes.npos) break;
        offset = end + 1;
    }
    out = std::move(result);
    return true;
}
}
