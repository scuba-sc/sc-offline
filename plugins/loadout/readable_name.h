// SPDX-License-Identifier: GPL-3.0-only
// Player-facing spelling of game class names and list headings. Display only: the raw
// name stays the identity that commands and requests use.
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace sco::features::loadout {
inline bool ReadableLower(char c) { return (c>='a' && c<='z') || (c>='0' && c<='9'); }
inline bool ReadableUpper(char c) { return c>='A' && c<='Z'; }
// Drops internal prefixes, splits on '-' and '_' and between camel-case words, and starts with a capital:
// "NPC_Archetypes-Male-Human-MilesEckhart" -> "Male Human Miles Eckhart", "primary_optic" -> "Primary optic".
inline std::string Readable(std::string_view raw) {
    static constexpr std::string_view prefixes[]{"NPC_Archetypes-","AIShip_CrewProfiles-","PU_"};
    for (bool again=true; again;) {
        again=false;
        for (const auto prefix:prefixes) {
            if (raw.size()<=prefix.size()) continue;
            bool same=true;
            for (size_t i=0;i<prefix.size() && same;++i) {
                const auto lower=[](char c) { return ReadableUpper(c) ? static_cast<char>(c+('a'-'A')) : c; };
                same=lower(raw[i])==lower(prefix[i]);
            }
            if (same) { raw.remove_prefix(prefix.size()); again=true; }
        }
    }
    std::string out;
    for (size_t i=0;i<raw.size();++i) {
        const char c=raw[i];
        if (c=='-' || c=='_' || c==' ') { if (!out.empty() && out.back()!=' ') out+=' '; continue; }
        if (!out.empty() && out.back()!=' ' && ReadableUpper(c)) {
            const char before=raw[i-1];
            const bool word_start=ReadableLower(before) || (ReadableUpper(before) && i+1<raw.size() && ReadableLower(raw[i+1]) && !(raw[i+1]>='0' && raw[i+1]<='9'));
            if (word_start) out+=' ';
        }
        out+=c;
    }
    while (!out.empty() && out.back()==' ') out.pop_back();
    if (!out.empty() && out[0]>='a' && out[0]<='z') out[0]=static_cast<char>(out[0]-('a'-'A'));
    return out.empty() ? std::string(raw) : out;
}
}
