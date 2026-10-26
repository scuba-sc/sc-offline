// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include "locale_strings.h"

namespace sco::launcher::locale {

#define SCO_MESSAGE_ENUM(id, key, text) id,
enum class Key { SCO_LAUNCHER_MESSAGES(SCO_MESSAGE_ENUM) Count };
#undef SCO_MESSAGE_ENUM
struct Message { const char* key; const char* english; };
inline constexpr size_t kMessageCount = static_cast<size_t>(Key::Count);
#define SCO_MESSAGE_ENTRY(id, key, text) Message{key, text},
inline constexpr std::array<Message, kMessageCount> kMessages{{
    SCO_LAUNCHER_MESSAGES(SCO_MESSAGE_ENTRY)
}};
#undef SCO_MESSAGE_ENTRY

inline bool NextCodepoint(std::string_view text, size_t& offset, uint32_t& point) {
    if (offset >= text.size()) return false;
    size_t at = offset;
    const auto lead = static_cast<unsigned char>(text[at++]);
    unsigned count = 0;
    uint32_t minimum = 0;
    if (lead < 0x80) point = lead;
    else if (lead >= 0xC2 && lead <= 0xDF) { point = lead & 0x1F; count = 1; minimum = 0x80; }
    else if (lead >= 0xE0 && lead <= 0xEF) { point = lead & 0x0F; count = 2; minimum = 0x800; }
    else if (lead >= 0xF0 && lead <= 0xF4) { point = lead & 0x07; count = 3; minimum = 0x10000; }
    else return false;
    if (count > text.size() - at) return false;
    for (unsigned i = 0; i < count; ++i) {
        const auto next = static_cast<unsigned char>(text[at++]);
        if ((next & 0xC0) != 0x80) return false;
        point = (point << 6) | (next & 0x3F);
    }
    if (point < minimum || point > 0x10FFFF || (point >= 0xD800 && point <= 0xDFFF)) return false;
    offset = at;
    return true;
}

inline bool ValidUtf8(std::string_view text, bool file_lines = false) {
    for (size_t at = 0; at < text.size();) {
        uint32_t point = 0;
        if (!NextCodepoint(text, at, point)) return false;
        if (point < 0x20 && !(file_lines && (point == '\n' || point == '\r' || point == '\t'))) return false;
        if (point == 0x7F || (point >= 0x80 && point <= 0x9F)) return false;
    }
    return true;
}

inline bool Placeholders(std::string_view text, std::vector<std::string>& names) {
    names.clear();
    for (size_t at = 0; at < text.size(); ++at) {
        if (text[at] == '}') return false;
        if (text[at] != '{') continue;
        const auto end = text.find('}', at + 1);
        if (end == text.npos || end == at + 1 || end - at > 33 || names.size() == 16) return false;
        const auto name = text.substr(at + 1, end - at - 1);
        if (name.front() < 'a' || name.front() > 'z') return false;
        for (char c : name)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
        names.emplace_back(name);
        at = end;
    }
    std::sort(names.begin(), names.end());
    return true;
}

inline bool SamePlaceholders(std::string_view english, std::string_view translated) {
    std::vector<std::string> expected, actual;
    return Placeholders(english, expected) && Placeholders(translated, actual) && expected == actual;
}

inline constexpr size_t kCatalogueLimit = 256 * 1024;
inline constexpr size_t kLineLimit = 8192;
inline constexpr size_t kValueLimit = 4096;
inline constexpr size_t kKeyLimit = 96;

enum class Error { None, Syntax, Duplicate, Utf8, Placeholders, Limit, Obsolete };
inline const char* ErrorName(Error error) {
    switch (error) {
    case Error::None: return "none";
    case Error::Syntax: return "invalid syntax or value";
    case Error::Duplicate: return "duplicate key";
    case Error::Utf8: return "invalid UTF-8 or control byte";
    case Error::Placeholders: return "placeholder mismatch";
    case Error::Limit: return "size limit";
    case Error::Obsolete: return "obsolete key";
    }
    return "unknown";
}
struct Diagnostics {
    Error error = Error::None;
    size_t line = 0;
    std::vector<std::string> missing;
    std::vector<std::string> obsolete;
};
using Argument = std::pair<std::string_view, std::string_view>;

class Catalogue {
public:
    bool Load(std::string_view bytes) {
        diagnostics_ = {};
        if (bytes.size() > kCatalogueLimit) return Refuse(Error::Limit, 0);
        if (bytes.starts_with("\xEF\xBB\xBF")) bytes.remove_prefix(3);
        if (!ValidUtf8(bytes, true)) return Refuse(Error::Utf8, 0);
        std::array<std::string, kMessageCount> next;
        std::array<bool, kMessageCount> seen{};
        size_t line_number = 0;
        for (size_t pos = 0; pos < bytes.size();) {
            ++line_number;
            auto end = bytes.find('\n', pos);
            if (end == bytes.npos) end = bytes.size();
            if (end - pos > kLineLimit) return Refuse(Error::Limit, line_number);
            const auto line = Trim(bytes.substr(pos, end - pos));
            pos = end + 1;
            if (line.empty() || line.front() == '#') continue;
            const auto equal = line.find('=');
            if (equal == line.npos) return Refuse(Error::Syntax, line_number);
            const auto key = Trim(line.substr(0, equal));
            if (key.empty() || key.size() > kKeyLimit || key.front() < 'a' || key.front() > 'z')
                return Refuse(Error::Syntax, line_number);
            for (char c : key)
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_'))
                    return Refuse(Error::Syntax, line_number);
            bool known = false;
            for (size_t i = 0; i < kMessageCount; ++i) {
                if (key != kMessages[i].key) continue;
                known = true;
                if (seen[i]) return Refuse(Error::Duplicate, line_number);
                seen[i] = true;
                const auto value = Trim(line.substr(equal + 1));
                if (!DecodeValue(value, next[i])) return Refuse(Error::Syntax, line_number);
                if (!SamePlaceholders(kMessages[i].english, next[i]))
                    return Refuse(Error::Placeholders, line_number);
                break;
            }
            if (!known) {
                diagnostics_.obsolete.emplace_back(key);
                return Refuse(Error::Obsolete, line_number);
            }
        }
        for (size_t i = 0; i < kMessageCount; ++i)
            if (!seen[i]) diagnostics_.missing.emplace_back(kMessages[i].key);
        values_ = std::move(next);
        return true;
    }
    const char* Text(Key key) const {
        const auto i = static_cast<size_t>(key);
        if (i >= kMessageCount) return kMessages[static_cast<size_t>(Key::FormatUnavailable)].english;
        return values_[i].empty() ? kMessages[i].english : values_[i].c_str();
    }
    const Diagnostics& Diagnostic() const { return diagnostics_; }
    std::string Format(Key key, std::initializer_list<Argument> arguments, bool* ok = nullptr) const {
        if (ok) *ok = false;
        const auto fail = [&]() { return std::string(Text(Key::FormatUnavailable)); };
        const auto index = static_cast<size_t>(key);
        if (index >= kMessageCount || arguments.size() > 16) return fail();
        std::vector<std::string> names, actual;
        if (!Placeholders(kMessages[index].english, names)) return fail();
        names.erase(std::unique(names.begin(), names.end()), names.end());
        for (const auto& argument : arguments) {
            if (argument.first.size() > 32 || argument.second.size() > kValueLimit ||
                !ValidUtf8(argument.second, true)) return fail();
            actual.emplace_back(argument.first);
        }
        std::sort(actual.begin(), actual.end());
        if (actual != names) return fail();
        const std::string_view text = Text(key);
        std::string out;
        for (size_t at = 0; at < text.size();) {
            if (text[at] != '{') {
                if (out.size() == kValueLimit) return fail();
                out += text[at++];
                continue;
            }
            const auto end = text.find('}', at + 1);
            if (end == text.npos) return fail();
            const auto name = text.substr(at + 1, end - at - 1);
            bool found = false;
            for (const auto& argument : arguments) {
                if (name != argument.first) continue;
                if (argument.second.size() > kValueLimit - out.size()) return fail();
                out += argument.second;
                found = true;
                break;
            }
            if (!found) return fail();
            at = end + 1;
        }
        if (ok) *ok = true;
        return out;
    }
private:
    static bool DecodeValue(std::string_view value, std::string& out) {
        if (value.empty()) return false;
        out.clear();
        for (size_t at = 0; at < value.size(); ++at) {
            char c = value[at];
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c == '\\') {
                if (++at == value.size()) return false;
                c = value[at];
                if (c == 'n') c = '\n';
                else if (c != '\\') return false;
            }
            out += c;
            if (out.size() > kValueLimit) return false;
        }
        return ValidUtf8(out, true);
    }
    bool Refuse(Error error, size_t line) {
        values_ = {};
        diagnostics_.error = error;
        diagnostics_.line = line;
        return false;
    }
    static std::string_view Trim(std::string_view text) {
        const auto begin = text.find_first_not_of(" \t\r");
        if (begin == text.npos) return {};
        return text.substr(begin, text.find_last_not_of(" \t\r") - begin + 1);
    }
    std::array<std::string, kMessageCount> values_;
    Diagnostics diagnostics_;
};

enum class Language { English, Russian, Auto };

inline bool ParseLanguage(std::string_view value, Language& language) {
    language = Language::English;
    if (value == "en") return true;
    if (value == "ru") { language = Language::Russian; return true; }
    if (value == "auto") { language = Language::Auto; return true; }
    return false;
}

inline const char* LanguageName(Language language) {
    if (language == Language::Russian) return "ru";
    if (language == Language::Auto) return "auto";
    return "en";
}

inline bool UpdateLanguageSetting(std::string_view bytes, Language language, std::string& out) {
    if (bytes.size() > kCatalogueLimit || !ValidUtf8(bytes, true) ||
        (language != Language::English && language != Language::Russian && language != Language::Auto)) return false;
    const auto trim = [](std::string_view text) {
        const auto begin = text.find_first_not_of(" \t\r");
        if (begin == text.npos) return std::string_view{};
        return text.substr(begin, text.find_last_not_of(" \t\r") - begin + 1);
    };
    const size_t prefix = bytes.starts_with("\xEF\xBB\xBF") ? 3 : 0;
    const auto first_newline = bytes.find('\n');
    const std::string_view ending = first_newline == bytes.npos ||
        (first_newline && bytes[first_newline - 1] == '\r') ? "\r\n" : "\n";
    const std::string setting = std::string("language = ") + LanguageName(language);
    std::string next(bytes.substr(0, prefix));
    bool found = false;
    for (size_t pos = prefix; pos < bytes.size();) {
        const auto newline = bytes.find('\n', pos);
        const auto end = newline == bytes.npos ? bytes.size() : newline;
        auto content_end = end;
        if (content_end > pos && bytes[content_end - 1] == '\r') --content_end;
        const auto line = bytes.substr(pos, content_end - pos);
        const auto equal = line.find('=');
        const auto key = equal == line.npos ? std::string_view{} : trim(line.substr(0, equal));
        bool language_key = key.size() == 8;
        for (size_t i = 0; language_key && i < key.size(); ++i) {
            const char c = key[i] >= 'A' && key[i] <= 'Z' ? char(key[i] + ('a' - 'A')) : key[i];
            language_key = c == std::string_view("language")[i];
        }
        if (language_key) { next += setting; found = true; }
        else next += line;
        const auto after = newline == bytes.npos ? end : end + 1;
        next += bytes.substr(content_end, after - content_end);
        pos = after;
    }
    if (!found) {
        if (next.size() > prefix && next.back() != '\n') {
            if (next.back() == '\r') next += '\n';
            else next += ending;
        }
        next += setting;
        next += ending;
    }
    if (next.size() > kCatalogueLimit) return false;
    out = std::move(next);
    return true;
}

class Localizer {
public:
    explicit Localizer(bool system_russian = false) : system_russian_(system_russian) {}
    bool LoadRussian(std::string_view bytes) {
        if (load_attempted_) return false;
        load_attempted_ = true;
        loaded_ = russian_.Load(bytes);
        return loaded_;
    }
    void SetGlyphCoverage(bool available) { glyphs_ = available; }
    void Select(Language language) {
        choice_ = language == Language::Russian || language == Language::Auto ? language : Language::English;
    }
    Language Choice() const { return choice_; }
    bool RequestsRussian() const {
        return choice_ == Language::Russian || (choice_ == Language::Auto && system_russian_);
    }
    bool RussianActive() const { return RequestsRussian() && loaded_ && glyphs_; }
    bool RussianLoaded() const { return loaded_; }
    const Catalogue& RussianCatalogue() const { return russian_; }
    const char* Text(Key key) const { return ActiveCatalogue().Text(key); }
    std::string Format(Key key, std::initializer_list<Argument> arguments, bool* ok = nullptr) const {
        return ActiveCatalogue().Format(key, arguments, ok);
    }
private:
    const Catalogue& ActiveCatalogue() const { return RussianActive() ? russian_ : english_; }
    Catalogue english_, russian_;
    Language choice_ = Language::English;
    bool system_russian_ = false, glyphs_ = false, loaded_ = false, load_attempted_ = false;
};

} // namespace sco::launcher::locale
