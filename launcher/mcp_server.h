// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The read-only MCP server behind `sc-offline.exe mcp`: JSON-RPC 2.0 over stdio, one message per line.
// Portable and header-only: no Windows headers, no files, no sockets. launcher.cpp supplies the Platform
// (status fields, plugin folders, log tails, sc-offline.ini text) and the stdio loop.
//
// The server exposes four read-only tools (get_status, list_plugins, read_log, get_settings) and nothing
// else: no command, console, start/stop or settings write (docs/decisions.md, "Console and cvars").
// Every string a tool returns is made valid UTF-8 and redacted (Redact) first.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace sco::launcher::mcp {

inline constexpr size_t kMaxLine = 1u << 20;         // longest accepted request line
inline constexpr int kMaxDepth = 64;                 // deepest JSON nesting accepted
inline constexpr size_t kMaxLogBytes = 2u << 20;     // read_log scans at most this much of a file's end
inline constexpr size_t kMaxLogText = 64u << 10;     // read_log returns at most this much text
inline constexpr size_t kMaxPlugins = 256;
inline constexpr size_t kMaxFieldBytes = 256;        // a plugin.ini value
inline constexpr size_t kMaxSettings = 1000;
inline constexpr size_t kMaxSettingBytes = 1024;
inline constexpr int kDefaultLines = 100;

// --- JSON ---------------------------------------------------------------------------------------

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    static Value Bool(bool b) { Value v; v.type_ = Type::Bool; v.bool_ = b; return v; }
    static Value Int(int64_t n) {
        Value v; v.type_ = Type::Number; v.num_ = static_cast<double>(n); v.text_ = std::to_string(n); return v;
    }
    static Value Str(std::string s) { Value v; v.type_ = Type::String; v.text_ = std::move(s); return v; }
    static Value Array() { Value v; v.type_ = Type::Array; return v; }
    static Value Object() { Value v; v.type_ = Type::Object; return v; }
    // A number as the parser read it: the token is kept so an id is echoed exactly.
    static Value Number(double d, std::string raw) {
        Value v; v.type_ = Type::Number; v.num_ = d; v.text_ = std::move(raw); return v;
    }

    Type type() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    bool AsBool() const { return bool_; }
    double AsDouble() const { return num_; }
    const std::string& AsString() const { return text_; }       // string content, or a number's token
    const std::vector<Value>& Items() const { return items_; }
    const std::vector<std::pair<std::string, Value>>& Members() const { return members_; }

    // An integral number within +-2^53.
    bool AsInteger(int64_t& out) const {
        if (type_ != Type::Number || !std::isfinite(num_) || num_ != std::floor(num_) || std::fabs(num_) > 9007199254740992.0)
            return false;
        out = static_cast<int64_t>(num_);
        return true;
    }

    Value& Push(Value v) { items_.push_back(std::move(v)); return *this; }
    // Replaces an existing key (the last duplicate wins), else appends: members keep insertion order.
    Value& Set(const std::string& key, Value v) {
        for (auto& m : members_) if (m.first == key) { m.second = std::move(v); return *this; }
        members_.emplace_back(key, std::move(v));
        return *this;
    }
    const Value* Find(const std::string& key) const {
        for (const auto& m : members_) if (m.first == key) return &m.second;
        return nullptr;
    }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0;
    std::string text_;
    std::vector<Value> items_;
    std::vector<std::pair<std::string, Value>> members_;
};

// Length of the well-formed UTF-8 sequence at s[i] (no overlong forms, surrogates or > U+10FFFF), else 0.
inline size_t Utf8SequenceLength(const std::string& s, size_t i) {
    const auto b = [&](size_t k) { return i + k < s.size() ? static_cast<unsigned char>(s[i + k]) : 0u; };
    const auto cont = [&](size_t k) { return (b(k) & 0xC0u) == 0x80u; };
    const unsigned c = b(0);
    if (c < 0x80) return 1;
    if (c >= 0xC2 && c <= 0xDF) return cont(1) ? 2 : 0;
    if (c >= 0xE0 && c <= 0xEF) {
        if (!cont(1) || !cont(2)) return 0;
        if (c == 0xE0 && b(1) < 0xA0) return 0;
        if (c == 0xED && b(1) > 0x9F) return 0;
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (!cont(1) || !cont(2) || !cont(3)) return 0;
        if (c == 0xF0 && b(1) < 0x90) return 0;
        if (c == 0xF4 && b(1) > 0x8F) return 0;
        return 4;
    }
    return 0;
}

// Replaces every invalid byte with U+FFFD so the text is valid UTF-8 (and so valid inside JSON).
inline std::string SanitizeUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const size_t n = Utf8SequenceLength(s, i);
        if (n == 0) { out += "\xEF\xBF\xBD"; ++i; }
        else { out.append(s, i, n); i += n; }
    }
    return out;
}

// The longest prefix of at most `max` bytes that does not cut a UTF-8 sequence (s is valid UTF-8).
inline std::string ClipUtf8(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0u) == 0x80u) --n;
    return s.substr(0, n);
}

inline size_t CountCodePoints(const std::string& s) {
    size_t n = 0;
    for (char c : s) if ((static_cast<unsigned char>(c) & 0xC0u) != 0x80u) ++n;
    return n;
}

struct ParseResult {
    bool ok = false;
    Value value;
    std::string error;   // set when !ok
};

// Strict RFC 8259: one value and nothing after it, UTF-8 text, \uXXXX escapes with paired surrogates,
// no leading zeros, nesting at most kMaxDepth.
class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    ParseResult Run() {
        ParseResult r;
        Ws();
        if (!ParseValue(r.value, 0)) { r.error = error_; r.value = Value(); return r; }
        Ws();
        if (i_ != s_.size()) { r.error = Describe("unexpected text after the JSON value"); r.value = Value(); return r; }
        r.ok = true;
        return r;
    }

private:
    const std::string& s_;
    size_t i_ = 0;
    std::string error_;

    std::string Describe(const std::string& what) const { return what + " at byte " + std::to_string(i_); }
    bool Fail(const std::string& what) { error_ = Describe(what); return false; }
    void Ws() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_; }
    bool Literal(const char* word) {
        size_t n = 0;
        while (word[n]) ++n;
        if (s_.compare(i_, n, word) != 0) return false;
        i_ += n;
        return true;
    }

    bool ParseValue(Value& out, int depth) {
        if (i_ >= s_.size()) return Fail("unexpected end of input");
        const char c = s_[i_];
        if (c == '{') return ParseObject(out, depth + 1);
        if (c == '[') return ParseArray(out, depth + 1);
        if (c == '"') { std::string t; if (!ParseString(t)) return false; out = Value::Str(std::move(t)); return true; }
        if (c == 't') { if (!Literal("true")) return Fail("invalid literal"); out = Value::Bool(true); return true; }
        if (c == 'f') { if (!Literal("false")) return Fail("invalid literal"); out = Value::Bool(false); return true; }
        if (c == 'n') { if (!Literal("null")) return Fail("invalid literal"); out = Value(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
        return Fail("unexpected character");
    }

    bool ParseNumber(Value& out) {
        const size_t start = i_;
        const auto digit = [&] { return i_ < s_.size() && s_[i_] >= '0' && s_[i_] <= '9'; };
        if (s_[i_] == '-') ++i_;
        if (!digit()) return Fail("invalid number");
        if (s_[i_] == '0') ++i_;
        else while (digit()) ++i_;
        if (i_ < s_.size() && s_[i_] == '.') {
            ++i_;
            if (!digit()) return Fail("invalid number");
            while (digit()) ++i_;
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            ++i_;
            if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            if (!digit()) return Fail("invalid number");
            while (digit()) ++i_;
        }
        const std::string raw = s_.substr(start, i_ - start);
        out = Value::Number(std::strtod(raw.c_str(), nullptr), raw);
        return true;
    }

    bool Hex4(unsigned& out) {
        if (i_ + 4 > s_.size()) return Fail("bad \\u escape");
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_ + k];
            unsigned d;
            if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
            else return Fail("bad \\u escape");
            v = v * 16 + d;
        }
        i_ += 4;
        out = v;
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool ParseString(std::string& out) {
        ++i_;   // opening quote
        for (;;) {
            if (i_ >= s_.size()) return Fail("unterminated string");
            const unsigned char c = static_cast<unsigned char>(s_[i_]);
            if (c == '"') { ++i_; return true; }
            if (c < 0x20) return Fail("control character in string");
            if (c != '\\') {
                const size_t n = Utf8SequenceLength(s_, i_);
                if (n == 0) return Fail("invalid UTF-8");
                out.append(s_, i_, n);
                i_ += n;
                continue;
            }
            ++i_;
            if (i_ >= s_.size()) return Fail("unterminated string");
            const char e = s_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xDC00 && cp <= 0xDFFF) return Fail("lone low surrogate");
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        unsigned lo;
                        if (i_ + 2 > s_.size() || s_[i_] != '\\' || s_[i_ + 1] != 'u') return Fail("lone high surrogate");
                        i_ += 2;
                        if (!Hex4(lo)) return false;
                        if (lo < 0xDC00 || lo > 0xDFFF) return Fail("lone high surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: --i_; return Fail("bad escape");
            }
        }
    }

    bool ParseArray(Value& out, int depth) {
        if (depth > kMaxDepth) return Fail("nesting too deep");
        ++i_;
        out = Value::Array();
        Ws();
        if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
        for (;;) {
            Value item;
            Ws();
            if (!ParseValue(item, depth)) return false;
            out.Push(std::move(item));
            Ws();
            if (i_ >= s_.size()) return Fail("unterminated array");
            if (s_[i_] == ',') { ++i_; continue; }
            if (s_[i_] == ']') { ++i_; return true; }
            return Fail("expected , or ] in array");
        }
    }

    bool ParseObject(Value& out, int depth) {
        if (depth > kMaxDepth) return Fail("nesting too deep");
        ++i_;
        out = Value::Object();
        Ws();
        if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
        for (;;) {
            Ws();
            if (i_ >= s_.size() || s_[i_] != '"') return Fail("expected a string key");
            std::string key;
            if (!ParseString(key)) return false;
            Ws();
            if (i_ >= s_.size() || s_[i_] != ':') return Fail("expected : after key");
            ++i_;
            Ws();
            Value v;
            if (!ParseValue(v, depth)) return false;
            out.Set(key, std::move(v));
            Ws();
            if (i_ >= s_.size()) return Fail("unterminated object");
            if (s_[i_] == ',') { ++i_; continue; }
            if (s_[i_] == '}') { ++i_; return true; }
            return Fail("expected , or } in object");
        }
    }
};

inline ParseResult Parse(const std::string& text) { return Parser(text).Run(); }

inline void SerializeTo(const Value& v, std::string& out) {
    switch (v.type()) {
        case Value::Type::Null: out += "null"; break;
        case Value::Type::Bool: out += v.AsBool() ? "true" : "false"; break;
        case Value::Type::Number: out += v.AsString(); break;
        case Value::Type::String: {
            static const char* hex = "0123456789abcdef";
            out += '"';
            for (unsigned char c : SanitizeUtf8(v.AsString())) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
                        else out += static_cast<char>(c);
                }
            }
            out += '"';
            break;
        }
        case Value::Type::Array: {
            out += '[';
            bool first = true;
            for (const Value& item : v.Items()) { if (!first) out += ','; first = false; SerializeTo(item, out); }
            out += ']';
            break;
        }
        case Value::Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& m : v.Members()) {
                if (!first) out += ',';
                first = false;
                SerializeTo(Value::Str(m.first), out);
                out += ':';
                SerializeTo(m.second, out);
            }
            out += '}';
            break;
        }
    }
}

// One line: control characters are escaped, UTF-8 is kept as is.
inline std::string Serialize(const Value& v) { std::string s; SerializeTo(v, s); return s; }

// --- schema check -------------------------------------------------------------------------------

// The subset of JSON Schema the tool schemas use: type (object, string, integer, boolean, array),
// properties, required, additionalProperties:false, enum, minimum, maximum, minLength, maxLength, items.
// Returns "" when the value fits, else a sentence naming the problem and where (`path` e.g. "arguments").
inline std::string CheckSchema(const Value& schema, const Value& v, const std::string& path) {
    const Value* type = schema.Find("type");
    const std::string t = type && type->IsString() ? type->AsString() : "";
    if (t == "object") {
        if (!v.IsObject()) return path + " must be an object";
        const Value* props = schema.Find("properties");
        for (const auto& m : v.Members()) {
            const Value* sub = props ? props->Find(m.first) : nullptr;
            if (!sub) {
                const Value* extra = schema.Find("additionalProperties");
                if (extra && extra->IsBool() && !extra->AsBool()) return path + " has an unknown property '" + m.first + "'";
                continue;
            }
            const std::string e = CheckSchema(*sub, m.second, path + "." + m.first);
            if (!e.empty()) return e;
        }
        if (const Value* req = schema.Find("required"))
            for (const Value& name : req->Items())
                if (!v.Find(name.AsString())) return path + " is missing the required property '" + name.AsString() + "'";
        return "";
    }
    if (t == "string") {
        if (!v.IsString()) return path + " must be a string";
        if (const Value* en = schema.Find("enum")) {
            bool hit = false;
            std::string list;
            for (const Value& item : en->Items()) {
                hit = hit || item.AsString() == v.AsString();
                list += (list.empty() ? "" : ", ") + item.AsString();
            }
            if (!hit) return path + " must be one of: " + list;
        }
        int64_t n = 0;
        if (const Value* m = schema.Find("minLength"); m && m->AsInteger(n) && static_cast<int64_t>(CountCodePoints(v.AsString())) < n)
            return path + " must be at least " + std::to_string(n) + " characters";
        if (const Value* m = schema.Find("maxLength"); m && m->AsInteger(n) && static_cast<int64_t>(CountCodePoints(v.AsString())) > n)
            return path + " must be at most " + std::to_string(n) + " characters";
        return "";
    }
    if (t == "integer") {
        int64_t n = 0;
        if (!v.AsInteger(n)) return path + " must be an integer";
        int64_t bound = 0;
        if (const Value* m = schema.Find("minimum"); m && m->AsInteger(bound) && n < bound)
            return path + " must be at least " + std::to_string(bound);
        if (const Value* m = schema.Find("maximum"); m && m->AsInteger(bound) && n > bound)
            return path + " must be at most " + std::to_string(bound);
        return "";
    }
    if (t == "boolean") return v.IsBool() ? "" : path + " must be true or false";
    if (t == "array") {
        if (!v.IsArray()) return path + " must be an array";
        if (const Value* items = schema.Find("items")) {
            size_t k = 0;
            for (const Value& item : v.Items()) {
                const std::string e = CheckSchema(*items, item, path + "[" + std::to_string(k++) + "]");
                if (!e.empty()) return e;
            }
        }
        return "";
    }
    return "";
}

// --- redaction ----------------------------------------------------------------------------------

inline char LowerAscii(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
inline bool IsSlash(char c) { return c == '\\' || c == '/'; }

inline std::string LowerAscii(std::string s) {
    for (char& c : s) c = LowerAscii(c);
    return s;
}

inline bool EqualsNoCase(const std::string& s, size_t at, const std::string& word) {
    if (at + word.size() > s.size()) return false;
    for (size_t k = 0; k < word.size(); ++k) if (LowerAscii(s[at + k]) != LowerAscii(word[k])) return false;
    return true;
}

// Paths under the user's profile must not carry the account name out of the PC. First every
// case-insensitive occurrence of a profile root (USERPROFILE) becomes %USERPROFILE%; then any remaining
// <letter>:\Users\<name> or <letter>:/Users/<name> has its <name> replaced by <user> (the slash style is kept).
// A name may hold spaces when a path separator follows it; otherwise it ends at the first space.
inline std::string Redact(const std::string& text, const std::vector<std::string>& profileRoots) {
    std::string s = text;
    for (std::string root : profileRoots) {
        while (root.size() > 3 && IsSlash(root.back())) root.pop_back();
        if (root.size() < 3) continue;
        std::string out;
        size_t i = 0;
        while (i < s.size()) {
            const size_t end = i + root.size();
            const bool boundary = end >= s.size() ||
                !(std::isalnum(static_cast<unsigned char>(s[end])) || s[end] == '_' || s[end] == '-' || s[end] == '.' ||
                  static_cast<unsigned char>(s[end]) >= 0x80);
            if (boundary && EqualsNoCase(s, i, root)) { out += "%USERPROFILE%"; i = end; }
            else out += s[i++];
        }
        s = std::move(out);
    }
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        const bool drive = i + 9 <= s.size() && std::isalpha(static_cast<unsigned char>(s[i])) && s[i + 1] == ':' &&
                           IsSlash(s[i + 2]) && EqualsNoCase(s, i + 3, "users") && IsSlash(s[i + 8]);
        if (!drive) { out += s[i++]; continue; }
        const size_t nameStart = i + 9;
        size_t e = nameStart;
        const auto stops = [&](char c) {
            return IsSlash(c) || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
                   static_cast<unsigned char>(c) < 0x20;
        };
        while (e < s.size() && !stops(s[e])) ++e;
        size_t nameEnd = e;
        if (!(e < s.size() && IsSlash(s[e]))) {
            nameEnd = nameStart;
            while (nameEnd < e && s[nameEnd] != ' ') ++nameEnd;
        }
        if (nameEnd == nameStart) { out.append(s, i, 9); i += 9; continue; }
        out.append(s, i, 9);
        out += "<user>";
        i = nameEnd;
    }
    return out;
}

// --- platform -----------------------------------------------------------------------------------

struct StatusInfo {
    std::string launcherVersion;
    bool gameFolderFound = false;
    std::string gameFolder;     // "" when not found
    std::string gameBuild;      // "" when unknown
    bool modInstalled = false;
    std::string eac;            // active | disabled | not_installed
    bool networkBlocked = false;
    bool eacRename = false;
    bool pluginsSettingOn = true;
    int pluginsEnabled = 0;
    int pluginsTotal = 0;
    std::string pcChangesLeft;  // "" when none
    bool wine = false;
    bool gameRunning = false;
};

struct PluginFolder {
    std::string folder;
    std::string iniText;        // the plugin.ini bytes
    bool disabled = false;      // a `disabled` file is in the folder
};

struct LogRead {
    std::string path;
    bool exists = false;
    std::string error;          // set when the file exists but could not be read
    std::string bytes;          // the file's last bytes (at most the requested amount)
    bool skippedOlder = false;  // the file is longer than `bytes`
};

struct IniRead {
    std::string path;
    bool exists = false;
    std::string error;
    std::string text;
};

// What the launcher provides. Every function only reads; an unset one reads as "nothing there".
struct Platform {
    std::string version;                                           // SCO_VERSION
    std::vector<std::string> profileRoots;                         // USERPROFILE, for Redact
    std::function<StatusInfo()> status;
    std::function<std::vector<PluginFolder>()> plugins;            // folders that have a plugin.ini
    std::function<LogRead(const std::string& log, size_t maxBytes)> readLog;   // mod | launcher | launcher_previous | game
    std::function<IniRead()> settings;                             // sc-offline.ini next to the exe
};

// --- tools --------------------------------------------------------------------------------------

struct ToolResult {
    bool isError = false;
    Value structured;           // matches the tool's outputSchema (when !isError)
    std::string errorText;      // when isError
};

inline constexpr const char* kInstructions =
    "This server gives a read-only view of one sc-offline install: its status, plugins, logs and settings. "
    "It never starts or stops the game, installs, updates, or changes settings or plugins. "
    "Paths under the user's profile are shown as %USERPROFILE%. "
    "To write plugins, use the sco-core SDK MCP server (sco-core-mcp); to test plugins out of game, use the scuba-engine MCP server.";

struct ToolDef {
    const char* name;
    const char* title;
    const char* description;
    const char* inputSchema;
    const char* outputSchema;
};

inline const ToolDef (&Tools())[4] {
    static const ToolDef tools[4] = {
        {"get_status", "Install status",
         "Returns the state of this sc-offline install: the launcher version, whether the Star Citizen folder was found and its build, "
         "whether the mod is installed, the Easy Anti-Cheat state, the launcher's network-block, EAC-rename and plugins settings, "
         "plugin counts, PC changes still to be undone, whether Wine is in use and whether the game is running. "
         "These are the read-only checks behind `sc-offline.exe status`; the tool takes no arguments and never changes anything or uses the network. "
         "Paths are redacted.",
         R"json({"type":"object","properties":{},"additionalProperties":false})json",
         R"json({"type":"object","properties":{
"launcher_version":{"type":"string","description":"Version of this sc-offline launcher."},
"game_folder_found":{"type":"boolean","description":"Whether the Star Citizen Bin64 folder was found."},
"game_folder":{"type":"string","description":"The game's Bin64 folder with the user profile redacted; empty when not found."},
"game_build":{"type":"string","description":"The game build the launcher read; empty when unknown."},
"mod_installed":{"type":"boolean","description":"Whether sc-offline's dinput8.dll or its marker is in the game folder."},
"eac":{"type":"string","enum":["active","disabled","not_installed"],"description":"Easy Anti-Cheat state: active (EasyAntiCheat_EOS.exe present), disabled (renamed to .bak) or not_installed."},
"network_blocked":{"type":"boolean","description":"The block_network setting: the launcher blocks the game in Windows Firewall while you play."},
"eac_rename":{"type":"boolean","description":"The eac_rename setting."},
"plugins_setting_on":{"type":"boolean","description":"The plugins setting in sc-offline.ini."},
"plugins_enabled":{"type":"integer","minimum":0,"description":"Plugin folders without a disabled file."},
"plugins_total":{"type":"integer","minimum":0,"description":"Plugin folders that have a plugin.ini."},
"pc_changes_left":{"type":"string","description":"The record of PC changes not yet undone; empty when none."},
"wine":{"type":"boolean","description":"Whether the launcher runs under Wine or Proton."},
"game_running":{"type":"boolean","description":"Whether this install's StarCitizen.exe is running."}},
"required":["launcher_version","game_folder_found","game_folder","game_build","mod_installed","eac","network_blocked","eac_rename","plugins_setting_on","plugins_enabled","plugins_total","pc_changes_left","wine","game_running"],
"additionalProperties":false})json"},
        {"list_plugins", "Installed plugins",
         "Lists the plugin folders under data\\plugins that have a plugin.ini, sorted by folder name and capped at 256. "
         "Each entry has the folder, whether it is enabled (no disabled file), the plugin.ini fields id, name, version, author, api, kind, entry and description "
         "(empty when absent, each shortened to 256 bytes) and warnings such as an id that does not match the folder or a missing id, name or version. "
         "The tool takes no arguments and is read-only: it does not load or run any plugin.",
         R"json({"type":"object","properties":{},"additionalProperties":false})json",
         R"json({"type":"object","properties":{
"count":{"type":"integer","minimum":0,"description":"Number of plugins returned."},
"truncated":{"type":"boolean","description":"True when more than 256 plugin folders exist and the list was cut."},
"plugins":{"type":"array","description":"Plugin folders sorted by folder name.","items":{"type":"object","properties":{
"folder":{"type":"string","description":"Folder name under data\\plugins."},
"enabled":{"type":"boolean","description":"False when the folder has a disabled file."},
"id":{"type":"string","description":"plugin.ini id; empty when absent."},
"name":{"type":"string","description":"plugin.ini name; empty when absent."},
"version":{"type":"string","description":"plugin.ini version; empty when absent."},
"author":{"type":"string","description":"plugin.ini author; empty when absent."},
"api":{"type":"string","description":"plugin.ini api; empty when absent."},
"kind":{"type":"string","description":"plugin.ini kind; empty when absent."},
"entry":{"type":"string","description":"plugin.ini entry; empty when absent."},
"description":{"type":"string","description":"plugin.ini description; empty when absent."},
"warnings":{"type":"array","description":"Problems found in this plugin's plugin.ini.","items":{"type":"string"}}},
"required":["folder","enabled","id","name","version","author","api","kind","entry","description","warnings"],"additionalProperties":false}}},
"required":["count","truncated","plugins"],"additionalProperties":false})json"},
        {"read_log", "Read a log",
         "Returns the last lines of one log: the mod's data\\mod.log, the launcher's data\\launcher.log or its previous run launcher.log.1, or the game's Game.log. "
         "It scans at most the last 2 MiB of the file, can keep only the lines containing a case-insensitive ASCII text, and returns at most 500 lines and 64 KiB of text, dropping the oldest first. "
         "Paths under the user's profile are redacted and invalid UTF-8 is replaced. The tool is read-only: it never writes, rotates or deletes a log.",
         R"json({"type":"object","properties":{
"log":{"type":"string","enum":["mod","launcher","launcher_previous","game"],"description":"Which log: mod (data\\mod.log), launcher (data\\launcher.log), launcher_previous (data\\launcher.log.1) or game (the game's Game.log next to the channel folder)."},
"lines":{"type":"integer","minimum":1,"maximum":500,"default":100,"description":"How many of the last matching lines to return."},
"contains":{"type":"string","minLength":1,"maxLength":200,"description":"Keep only lines containing this text, compared case-insensitively for ASCII letters, before the last lines are taken."}},
"required":["log"],"additionalProperties":false})json",
         R"json({"type":"object","properties":{
"log":{"type":"string","enum":["mod","launcher","launcher_previous","game"],"description":"The log that was read."},
"path":{"type":"string","description":"The file's path with the user profile redacted; empty when unknown."},
"exists":{"type":"boolean","description":"Whether the file exists."},
"lines":{"type":"array","description":"The returned lines, oldest first, without line endings.","items":{"type":"string"}},
"matched_lines":{"type":"integer","minimum":0,"description":"Lines that matched in the scanned part of the file, before the last N were taken."},
"truncated":{"type":"boolean","description":"True when older content was not scanned or more lines matched than were returned."}},
"required":["log","path","exists","lines","matched_lines","truncated"],"additionalProperties":false})json"},
        {"get_settings", "sc-offline.ini settings",
         "Returns the settings in the sc-offline.ini next to the launcher as section, key and value entries in file order, skipping comments and blank lines. "
         "Values are redacted for the user profile and shortened to 1 KiB, and at most 1000 entries are returned. "
         "The tool takes no arguments and is read-only: it never writes or migrates the file.",
         R"json({"type":"object","properties":{},"additionalProperties":false})json",
         R"json({"type":"object","properties":{
"path":{"type":"string","description":"The ini file's path with the user profile redacted."},
"exists":{"type":"boolean","description":"Whether the file exists."},
"truncated":{"type":"boolean","description":"True when the file has more settings than were returned."},
"settings":{"type":"array","description":"Settings in file order.","items":{"type":"object","properties":{
"section":{"type":"string","description":"The [section] the key is under; empty before any section."},
"key":{"type":"string","description":"The setting's key."},
"value":{"type":"string","description":"The setting's value as written, redacted."}},
"required":["section","key","value"],"additionalProperties":false}}},
"required":["path","exists","truncated","settings"],"additionalProperties":false})json"},
    };
    return tools;
}

inline std::string TrimSpace(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(" \t\r\n") + 1 - b);
}

// Lines of an ini-style text: BOM dropped, "\n" or "\r\n" ends, trimmed.
inline std::vector<std::string> IniLines(std::string text) {
    if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        lines.push_back(TrimSpace(text.substr(pos, nl - pos)));
        pos = nl + 1;
    }
    return lines;
}

// --- server -------------------------------------------------------------------------------------

class Server {
public:
    explicit Server(Platform platform) : platform_(std::move(platform)) {}

    // One request line in. True when a reply line (no newline) was produced.
    bool HandleLine(std::string line, std::string& reply) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (TrimSpace(line).empty()) return false;
        if (line.size() > kMaxLine) { reply = ErrorLine(Value(), -32600, "Request line is longer than 1 MiB"); return true; }
        const ParseResult p = Parse(line);
        if (!p.ok) { reply = ErrorLine(Value(), -32700, "Parse error: " + p.error); return true; }
        const Value& msg = p.value;
        if (msg.IsArray()) { reply = ErrorLine(Value(), -32600, "Batch requests are not supported"); return true; }
        if (!msg.IsObject()) { reply = ErrorLine(Value(), -32600, "A request must be a JSON object"); return true; }

        const Value* id = msg.Find("id");
        const bool hasId = id != nullptr;
        if (hasId && !(id->IsString() || id->IsNumber() || id->IsNull())) {
            reply = ErrorLine(Value(), -32600, "The id must be a string, a number or null");
            return true;
        }
        const Value* method = msg.Find("method");
        if (!method && (msg.Find("result") || msg.Find("error"))) return false;   // a response from the client: nothing is awaiting it
        const Value* version = msg.Find("jsonrpc");
        if (!version || !version->IsString() || version->AsString() != "2.0" || !method || !method->IsString()) {
            reply = ErrorLine(hasId ? *id : Value(), -32600, "Invalid request: jsonrpc must be \"2.0\" and method a string");
            return true;
        }
        if (!hasId) return false;   // a notification (notifications/initialized, notifications/cancelled, ...) gets no reply
        const Value* params = msg.Find("params");
        if (params && !params->IsObject()) { reply = ErrorLine(*id, -32602, "params must be an object"); return true; }
        reply = Dispatch(*id, method->AsString(), params);
        return true;
    }

    std::string OversizeReply() const { return ErrorLine(Value(), -32600, "Request line is longer than 1 MiB"); }

    // Runs one tool; the same result tools/call wraps. Exposed for the launcher's own checks.
    ToolResult RunTool(const std::string& name, const Value& args) const {
        if (name == "get_status") return GetStatus();
        if (name == "list_plugins") return ListPlugins();
        if (name == "read_log") return ReadLog(args);
        return GetSettings();
    }

private:
    Platform platform_;

    static Value Envelope(const Value& id) {
        Value v = Value::Object();
        v.Set("jsonrpc", Value::Str("2.0"));
        v.Set("id", id);
        return v;
    }
    static std::string ErrorLine(const Value& id, int code, const std::string& message) {
        Value v = Envelope(id), e = Value::Object();
        e.Set("code", Value::Int(code));
        e.Set("message", Value::Str(message));
        v.Set("error", std::move(e));
        return Serialize(v);
    }
    static std::string ResultLine(const Value& id, Value result) {
        Value v = Envelope(id);
        v.Set("result", std::move(result));
        return Serialize(v);
    }

    std::string Clean(const std::string& s) const { return Redact(SanitizeUtf8(s), platform_.profileRoots); }

    static bool Supported(const std::string& v) {
        return v == "2025-11-25" || v == "2025-06-18" || v == "2025-03-26" || v == "2024-11-05";
    }

    std::string Dispatch(const Value& id, const std::string& method, const Value* params) const {
        if (method == "initialize") {
            std::string chosen = "2025-11-25";
            if (const Value* asked = params ? params->Find("protocolVersion") : nullptr) {
                if (!asked->IsString()) return ErrorLine(id, -32602, "protocolVersion must be a string");
                if (Supported(asked->AsString())) chosen = asked->AsString();
            }
            Value result = Value::Object(), caps = Value::Object(), tools = Value::Object(), info = Value::Object();
            tools.Set("listChanged", Value::Bool(false));
            caps.Set("tools", std::move(tools));
            info.Set("name", Value::Str("sc-offline"));
            info.Set("title", Value::Str("sc-offline"));
            info.Set("version", Value::Str(platform_.version));
            result.Set("protocolVersion", Value::Str(chosen));
            result.Set("capabilities", std::move(caps));
            result.Set("serverInfo", std::move(info));
            result.Set("instructions", Value::Str(kInstructions));
            return ResultLine(id, std::move(result));
        }
        if (method == "ping") return ResultLine(id, Value::Object());
        if (method == "tools/list") return ResultLine(id, ListTools());
        if (method == "tools/call") return CallTool(id, params);
        return ErrorLine(id, -32601, "Method not found: " + method);
    }

    static Value ParseOrNull(const char* json) {
        const ParseResult r = Parse(json);
        return r.ok ? r.value : Value();
    }

    static Value ListTools() {
        Value list = Value::Array();
        for (const ToolDef& t : Tools()) {
            Value tool = Value::Object(), ann = Value::Object();
            ann.Set("readOnlyHint", Value::Bool(true));
            ann.Set("destructiveHint", Value::Bool(false));
            ann.Set("idempotentHint", Value::Bool(true));
            ann.Set("openWorldHint", Value::Bool(false));
            tool.Set("name", Value::Str(t.name));
            tool.Set("title", Value::Str(t.title));
            tool.Set("description", Value::Str(t.description));
            tool.Set("inputSchema", ParseOrNull(t.inputSchema));
            tool.Set("outputSchema", ParseOrNull(t.outputSchema));
            tool.Set("annotations", std::move(ann));
            list.Push(std::move(tool));
        }
        Value result = Value::Object();
        result.Set("tools", std::move(list));
        return result;
    }

    std::string CallTool(const Value& id, const Value* params) const {
        const Value* name = params ? params->Find("name") : nullptr;
        if (!name || !name->IsString()) return ErrorLine(id, -32602, "tools/call needs a string 'name'");
        const ToolDef* def = nullptr;
        for (const ToolDef& t : Tools()) if (name->AsString() == t.name) def = &t;
        if (!def) return ErrorLine(id, -32602, "Unknown tool '" + Clean(name->AsString()) + "'");
        Value args = Value::Object();
        if (const Value* given = params->Find("arguments")) {
            if (!given->IsObject()) return ErrorLine(id, -32602, "Invalid arguments for " + std::string(def->name) + ": arguments must be an object");
            args = *given;
        }
        const std::string problem = CheckSchema(ParseOrNull(def->inputSchema), args, "arguments");
        if (!problem.empty()) return ErrorLine(id, -32602, "Invalid arguments for " + std::string(def->name) + ": " + Clean(problem));

        const ToolResult r = RunTool(def->name, args);
        Value result = Value::Object(), content = Value::Array(), text = Value::Object();
        text.Set("type", Value::Str("text"));
        text.Set("text", Value::Str(r.isError ? r.errorText : Serialize(r.structured)));
        content.Push(std::move(text));
        result.Set("content", std::move(content));
        if (!r.isError) result.Set("structuredContent", r.structured);
        result.Set("isError", Value::Bool(r.isError));
        return ResultLine(id, std::move(result));
    }

    static ToolResult Failure(const std::string& text) { ToolResult r; r.isError = true; r.errorText = text; return r; }

    ToolResult GetStatus() const {
        const StatusInfo s = platform_.status ? platform_.status() : StatusInfo{};
        Value o = Value::Object();
        o.Set("launcher_version", Value::Str(Clean(s.launcherVersion.empty() ? platform_.version : s.launcherVersion)));
        o.Set("game_folder_found", Value::Bool(s.gameFolderFound));
        o.Set("game_folder", Value::Str(s.gameFolderFound ? Clean(s.gameFolder) : ""));
        o.Set("game_build", Value::Str(Clean(s.gameBuild)));
        o.Set("mod_installed", Value::Bool(s.modInstalled));
        const std::string eac = s.eac == "active" || s.eac == "disabled" ? s.eac : "not_installed";
        o.Set("eac", Value::Str(eac));
        o.Set("network_blocked", Value::Bool(s.networkBlocked));
        o.Set("eac_rename", Value::Bool(s.eacRename));
        o.Set("plugins_setting_on", Value::Bool(s.pluginsSettingOn));
        o.Set("plugins_enabled", Value::Int(s.pluginsEnabled > 0 ? s.pluginsEnabled : 0));
        o.Set("plugins_total", Value::Int(s.pluginsTotal > 0 ? s.pluginsTotal : 0));
        o.Set("pc_changes_left", Value::Str(Clean(s.pcChangesLeft)));
        o.Set("wine", Value::Bool(s.wine));
        o.Set("game_running", Value::Bool(s.gameRunning));
        ToolResult r; r.structured = std::move(o);
        return r;
    }

    // Top-level `key = value` lines of a plugin.ini (a [section] ends the part that describes the plugin).
    static std::vector<std::pair<std::string, std::string>> PluginFields(const std::string& ini) {
        std::vector<std::pair<std::string, std::string>> fields;
        for (const std::string& line : IniLines(ini)) {
            if (line.empty() || line[0] == ';' || line[0] == '#') continue;
            if (line[0] == '[') break;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            fields.emplace_back(LowerAscii(TrimSpace(line.substr(0, eq))), TrimSpace(line.substr(eq + 1)));
        }
        return fields;
    }

    ToolResult ListPlugins() const {
        std::vector<PluginFolder> folders = platform_.plugins ? platform_.plugins() : std::vector<PluginFolder>{};
        std::stable_sort(folders.begin(), folders.end(), [](const PluginFolder& a, const PluginFolder& b) {
            const std::string x = LowerAscii(a.folder), y = LowerAscii(b.folder);
            return x != y ? x < y : a.folder < b.folder;
        });
        const bool cut = folders.size() > kMaxPlugins;
        if (cut) folders.resize(kMaxPlugins);
        static const char* const keys[] = {"id", "name", "version", "author", "api", "kind", "entry", "description"};
        Value list = Value::Array();
        for (const PluginFolder& f : folders) {
            const auto fields = PluginFields(f.iniText);
            Value p = Value::Object(), warnings = Value::Array();
            const std::string folder = Clean(f.folder);
            p.Set("folder", Value::Str(folder));
            p.Set("enabled", Value::Bool(!f.disabled));
            std::string id;
            for (const char* key : keys) {
                std::string value;
                for (const auto& kv : fields) if (kv.first == key) value = kv.second;   // the last one wins
                value = Clean(value);
                if (value.size() > kMaxFieldBytes) {
                    value = ClipUtf8(value, kMaxFieldBytes);
                    warnings.Push(Value::Str(std::string("plugin.ini ") + key + " is longer than 256 bytes and was shortened"));
                }
                if (std::string(key) == "id") id = value;
                p.Set(key, Value::Str(value));
            }
            if (id.empty()) warnings.Push(Value::Str("plugin.ini has no id"));
            else if (id != folder) warnings.Push(Value::Str("plugin.ini id '" + id + "' does not match the folder name '" + folder + "'"));
            for (const char* key : {"name", "version"})
                if (p.Find(key)->AsString().empty()) warnings.Push(Value::Str(std::string("plugin.ini has no ") + key));
            p.Set("warnings", std::move(warnings));
            list.Push(std::move(p));
        }
        Value o = Value::Object();
        o.Set("count", Value::Int(static_cast<int64_t>(list.Items().size())));
        o.Set("truncated", Value::Bool(cut));
        o.Set("plugins", std::move(list));
        ToolResult r; r.structured = std::move(o);
        return r;
    }

    ToolResult ReadLog(const Value& args) const {
        const std::string log = args.Find("log")->AsString();
        int64_t want = kDefaultLines;
        if (const Value* n = args.Find("lines")) n->AsInteger(want);
        std::string needle;
        const Value* contains = args.Find("contains");
        if (contains) needle = LowerAscii(contains->AsString());

        const LogRead read = platform_.readLog ? platform_.readLog(log, kMaxLogBytes) : LogRead{};
        if (!read.error.empty()) return Failure("Could not read the " + log + " log: " + Clean(read.error));

        // Split the tail into lines. When older bytes were skipped the first piece may be cut: drop it.
        std::vector<std::string> matched;
        size_t pos = 0;
        const std::string& bytes = read.bytes;
        if (read.skippedOlder) {
            const size_t nl = bytes.find('\n');
            pos = nl == std::string::npos ? bytes.size() : nl + 1;
        }
        while (pos < bytes.size()) {
            size_t nl = bytes.find('\n', pos);
            const size_t end = nl == std::string::npos ? bytes.size() : nl;
            std::string line = bytes.substr(pos, end - pos);
            pos = end + 1;
            while (!line.empty() && line.back() == '\r') line.pop_back();
            line = Clean(line);
            // The filter runs on the redacted text, so it cannot be used to probe for an account name.
            if (!needle.empty() && LowerAscii(line).find(needle) == std::string::npos) continue;
            matched.push_back(std::move(line));
        }

        const size_t matchedCount = matched.size();
        size_t first = matchedCount > static_cast<size_t>(want) ? matchedCount - static_cast<size_t>(want) : 0;
        bool truncated = read.skippedOlder || first > 0;
        // 64 KiB of text in total (each line counts its newline): the oldest lines go first.
        size_t used = 0, keepFrom = matchedCount;
        while (keepFrom > first) {
            std::string& line = matched[keepFrom - 1];
            if (line.size() + 1 > kMaxLogText) { line = ClipUtf8(line, kMaxLogText - 1); truncated = true; }
            if (used + line.size() + 1 > kMaxLogText) break;
            used += line.size() + 1;
            --keepFrom;
        }
        if (keepFrom > first) { first = keepFrom; truncated = true; }

        Value lines = Value::Array();
        for (size_t k = first; k < matchedCount; ++k) lines.Push(Value::Str(matched[k]));
        Value o = Value::Object();
        o.Set("log", Value::Str(log));
        o.Set("path", Value::Str(Clean(read.path)));
        o.Set("exists", Value::Bool(read.exists));
        o.Set("lines", std::move(lines));
        o.Set("matched_lines", Value::Int(static_cast<int64_t>(matchedCount)));
        o.Set("truncated", Value::Bool(truncated));
        ToolResult r; r.structured = std::move(o);
        return r;
    }

    ToolResult GetSettings() const {
        const IniRead ini = platform_.settings ? platform_.settings() : IniRead{};
        if (!ini.error.empty()) return Failure("Could not read sc-offline.ini: " + Clean(ini.error));
        Value list = Value::Array();
        bool cut = false;
        std::string section;
        for (const std::string& line : IniLines(ini.text)) {
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            if (line[0] == '[') {
                const size_t close = line.find(']');
                section = TrimSpace(line.substr(1, close == std::string::npos ? std::string::npos : close - 1));
                continue;
            }
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            if (list.Items().size() >= kMaxSettings) { cut = true; break; }
            Value s = Value::Object();
            s.Set("section", Value::Str(ClipUtf8(Clean(section), kMaxFieldBytes)));
            s.Set("key", Value::Str(ClipUtf8(Clean(TrimSpace(line.substr(0, eq))), kMaxFieldBytes)));
            s.Set("value", Value::Str(ClipUtf8(Clean(TrimSpace(line.substr(eq + 1))), kMaxSettingBytes)));
            list.Push(std::move(s));
        }
        Value o = Value::Object();
        o.Set("path", Value::Str(Clean(ini.path)));
        o.Set("exists", Value::Bool(ini.exists));
        o.Set("truncated", Value::Bool(cut));
        o.Set("settings", std::move(list));
        ToolResult r; r.structured = std::move(o);
        return r;
    }
};

// Reads request lines with `read` (returns 0 at the end of input), answers each through `write` (one line,
// the newline is the writer's to add; false stops the loop). A line over kMaxLine is dropped unbuffered
// and answered with -32600. Returns at the end of input.
inline void Serve(Server& server, const std::function<size_t(char*, size_t)>& read,
                  const std::function<bool(const std::string&)>& write) {
    std::string line;
    bool over = false;
    char chunk[65536];
    const auto finish = [&]() {
        std::string reply;
        const bool answer = over ? (reply = server.OversizeReply(), true) : server.HandleLine(std::move(line), reply);
        line.clear();
        over = false;
        return !answer || write(reply);
    };
    for (size_t got; (got = read(chunk, sizeof(chunk))) > 0;) {
        size_t at = 0;
        while (at < got) {
            const char* nl = static_cast<const char*>(std::memchr(chunk + at, '\n', got - at));
            const size_t end = nl ? static_cast<size_t>(nl - chunk) : got;
            if (!over) {
                line.append(chunk + at, end - at);
                if (line.size() > kMaxLine) { over = true; line.clear(); line.shrink_to_fit(); }
            }
            at = end;
            if (nl) { ++at; if (!finish()) return; }
        }
    }
    if (over || !line.empty()) finish();   // a last line without a newline
}

} // namespace sco::launcher::mcp
