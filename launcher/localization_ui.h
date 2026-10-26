// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "localization.h"
#include "../src/third_party/imgui/imgui.h"
#include <cmath>

namespace sco::launcher::locale {

inline std::string FitText(std::string text, float width, bool from_left) {
    if (!std::isfinite(width) || width <= 0 || !ValidUtf8(text, true)) return {};
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    if (ImGui::CalcTextSize("...").x > width) return {};
    while (!text.empty()) {
        const auto candidate = from_left ? "..." + text : text + "...";
        if (ImGui::CalcTextSize(candidate.c_str()).x <= width) return candidate;
        if (from_left) {
            size_t at = 0;
            uint32_t point = 0;
            if (!NextCodepoint(text, at, point)) return {};
            text.erase(0, at);
        } else {
            size_t at = text.size() - 1;
            while (at && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) --at;
            text.erase(at);
        }
    }
    return "...";
}

inline bool CatalogueGlyphs(const Catalogue& catalogue, ImFont* font) {
    if (!font) return false;
    for (size_t key = 0; key < kMessageCount; ++key) {
        const std::string_view text = catalogue.Text(static_cast<Key>(key));
        for (size_t at = 0; at < text.size();) {
            uint32_t point = 0;
            if (!NextCodepoint(text, at, point)) return false;
            if (point == '\n' || point == '\r' || point == '\t') continue;
            if (point > IM_UNICODE_CODEPOINT_MAX || !font->IsGlyphInFont(static_cast<ImWchar>(point))) return false;
        }
    }
    return true;
}

} // namespace sco::launcher::locale
