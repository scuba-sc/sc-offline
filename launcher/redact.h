// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Redaction shared by the crash-report bundle (strict mode) and the read-only MCP server (launcher/mcp_server.h,
// relaxed mode). Portable and header-only: no Windows headers, no files. Text goes through five steps, in this order:
//   1. %5C, %2F and %40 are decoded when that exposes a path or an address (PathsAndEmails), then paths under the
//      user's profile and any `Users` path (RedactPaths) and e-mail addresses (RedactEmails)
//   2. the digits after geid, accountid, account_id and playerid, as the crash redactor before this one hid them
//      (RedactLegacyIds): both modes hide the same id digits
//   3. the Windows account name (RedactAccountNames)
//   4. identity values learned from the whole file being read, login lines first (Identities, see Learn)
//   5. values of identity keys: handle, nickname, character name, GEID, account and player ids (RedactIdentityKeys)
// Learn scans the text first, so a handle that a login line shows once is also hidden on every other line.
// Redaction is a best effort for known shapes. Free text can still hold a name that no key or line announced, so a
// person should read what they share.

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sco::launcher::redact {

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

// The length of a redaction marker (<redacted>, <user>, %USERPROFILE%) that starts at s[i], or 0. The matcher never
// matches inside one, so a learned value such as `red` or `use` cannot damage a marker written earlier.
inline size_t MarkerLen(const std::string& s, size_t i) {
    if (s[i] == '<') {
        if (EqualsNoCase(s, i, "<redacted>")) return 10;
        if (EqualsNoCase(s, i, "<user>")) return 6;
    } else if (s[i] == '%' && EqualsNoCase(s, i, "%userprofile%")) return 13;
    return 0;
}

// Where a profile root or a `Users` path ends: the end of the run of slashes starting at i.
inline size_t SlashRunEnd(const std::string& s, size_t i) {
    while (i < s.size() && IsSlash(s[i])) ++i;
    return i;
}

// The end of `root` matched at s[at], or npos. A run of slashes in either matches a run in the other (so
// `C:\\Users\\x` in an escaped log still matches). Letters compare ASCII-case-insensitively only, so a
// non-ASCII account name matches byte for byte.
inline size_t MatchRoot(const std::string& s, size_t at, const std::string& root) {
    size_t i = at;
    for (size_t k = 0; k < root.size();) {
        if (IsSlash(root[k])) {
            const size_t e = SlashRunEnd(s, i);
            if (e == i) return std::string::npos;
            i = e;
            k = SlashRunEnd(root, k);
        } else {
            if (i >= s.size() || LowerAscii(s[i]) != LowerAscii(root[k])) return std::string::npos;
            ++i; ++k;
        }
    }
    return i;
}

// Paths under the user's profile must not carry the account name out of the PC. First every
// case-insensitive (ASCII only) occurrence of a profile root (USERPROFILE) becomes %USERPROFILE%; then the name in
// any `Users` path component that follows a separator (C:\Users\x, /c/Users/x, \\?\C:\Users\x,
// \Device\HarddiskVolume3\Users\x, \\host\c$\Users\x) is replaced by <user>. A run of \ or / counts as one
// separator and is kept as written. A name may hold spaces when a separator follows it; otherwise it ends at the
// next character that cannot be part of a path in a log (quote, < > |, comma, semicolon, closing bracket) or the
// end of the line, which can hide more than the name.
inline std::string RedactPaths(const std::string& text, const std::vector<std::string>& profileRoots) {
    std::string s = text;
    for (std::string root : profileRoots) {
        while (root.size() > 3 && IsSlash(root.back())) root.pop_back();
        if (root.size() < 3) continue;
        std::string out;
        size_t i = 0;
        while (i < s.size()) {
            const size_t end = MatchRoot(s, i, root);
            bool boundary = false;
            if (end != std::string::npos) {
                // The root ends the account name unless the name goes on: a letter, digit or non-ASCII byte, or
                // '-' / '_' followed by one. '.' always ends it, so `bobsecret.` and `bobsecret.log` match.
                const auto cont = [&](size_t k) {
                    return k < s.size() && (std::isalnum(static_cast<unsigned char>(s[k])) || static_cast<unsigned char>(s[k]) >= 0x80);
                };
                boundary = end >= s.size() || !(cont(end) || ((s[end] == '-' || s[end] == '_') && cont(end + 1)));
            }
            if (boundary) { out += "%USERPROFILE%"; i = end; }
            else out += s[i++];
        }
        s = std::move(out);
    }
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (!IsSlash(s[i])) { out += s[i++]; continue; }
        const size_t run = SlashRunEnd(s, i);
        const size_t wl = EqualsNoCase(s, run, "users") ? 5 : EqualsNoCase(s, run, "home") ? 4 : 0;
        const bool users = wl != 0 && run + wl < s.size() && IsSlash(s[run + wl]);
        if (!users) { out.append(s, i, run - i); i = run; continue; }
        const size_t nameStart = SlashRunEnd(s, run + wl);
        out.append(s, i, nameStart - i);
        const auto stops = [&](char c) {
            return IsSlash(c) || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
                   static_cast<unsigned char>(c) < 0x20;
        };
        size_t e = nameStart;
        while (e < s.size() && !stops(s[e])) ++e;
        size_t nameEnd = e;
        if (!(e < s.size() && IsSlash(s[e]))) {
            nameEnd = nameStart;
            while (nameEnd < e && !std::strchr("',;)]}", s[nameEnd])) ++nameEnd;
        }
        i = nameEnd;
        if (nameEnd > nameStart) out += "<user>";
    }
    return out;
}

// Account names shorter than this are not searched for in free text: a 1 or 2 letter name would hide parts of
// ordinary words and numbers. Paths and identity keys are still redacted for them.
inline constexpr size_t kMinAccountNameLength = 3;

// Names this long or longer are also found inside other words and joined by a dash or underscore (DESKTOP-ZORBAX,
// zorbax_saves), where a false hit is unlikely.
inline constexpr size_t kSubstringNameLength = 6;

// Words that name the machine, not a person. They are never taken as an account name or learned as a value, so
// `user=SYSTEM` does not make `filesystem` or `SystemTime` unreadable.
inline bool SystemToken(const std::string& low) {
    static const char* const words[] = {"system", "local", "none", "null", "default", "localsystem", "localservice",
                                        "networkservice", "anonymous", "nobody", "unknown"};
    for (const char* w : words) if (low == w) return true;
    return false;
}

// A letter, digit or non-ASCII byte joins a token; a dash or underscore does not (a 3 to 5 character name is
// found in ALEX-PC and alex_saves).
inline bool IsAlnumByte(char c) { return std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80; }

// Every case-insensitive (ASCII only) occurrence of a Windows account name becomes <user>. A name of 3 to 5
// characters is replaced as a whole word: not preceded or followed by a letter, digit or non-ASCII byte, so ALEX-PC,
// DESKTOP-ALEX and alex_saves are hidden and Alexa is not. A name of kSubstringNameLength or more is matched
// inside other words too. Names shorter than kMinAccountNameLength are skipped, and so are the words this function
// writes itself and the machine words of SystemToken. Trade-off: an account named `admin` or `guest` also hides
// the ordinary word in a log line.
inline std::string RedactAccountNames(const std::string& text, std::vector<std::string> names) {
    // Longest first, so `zorbax.CORP` goes whole before `zorbax` is looked for.
    std::stable_sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    std::string s = text;
    for (const std::string& name : names) {
        if (name.size() < kMinAccountNameLength) continue;
        const std::string low = LowerAscii(name);
        if (low == "user" || low == "users" || low == "redacted" || low == "userprofile" || SystemToken(low)) continue;
        const bool anywhere = name.size() >= kSubstringNameLength;
        std::string out;
        size_t i = 0;
        while (i < s.size()) {
            if (const size_t m = MarkerLen(s, i)) { out.append(s, i, m); i += m; continue; }   // never inside a marker
            const bool starts = i + name.size() <= s.size() && EqualsNoCase(s, i, name.c_str()) &&
                                (anywhere || ((i == 0 || !IsAlnumByte(s[i - 1])) &&
                                              (i + name.size() >= s.size() || !IsAlnumByte(s[i + name.size()]))));
            if (starts) { out += "<user>"; i += name.size(); }
            else out += s[i++];
        }
        s = std::move(out);
    }
    return s;
}

// E-mail addresses anywhere in the text become <redacted>. Every scan stays inside the one `@` it belongs to, so a
// hostile line cannot make this quadratic.
inline std::string RedactEmails(const std::string& s) {
    const auto hi = [](char c) { return static_cast<unsigned char>(c) >= 0x80; };
    const auto local = [&](char c) { return hi(c) || std::isalnum(static_cast<unsigned char>(c)) || std::strchr("._%+-'!#$&*^`{|}~", c) != nullptr; };
    const auto domain = [&](char c) { return hi(c) || std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_'; };
    const auto alpha = [&](char c) { return hi(c) || std::isalpha(static_cast<unsigned char>(c)); };
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        // `@`, or its percent-encoded form %40
        size_t atLen = 0;
        if (s[i] == '@') atLen = 1;
        else if (s[i] == '%' && i + 2 < s.size() && s[i + 1] == '4' && s[i + 2] == '0') atLen = 3;
        if (!atLen) { out += s[i++]; continue; }
        size_t b = out.size();
        if (b > 0 && out[b - 1] == '"') {   // a quoted local part: "pat q"@example.com
            size_t q = b - 1;
            const size_t lo = b > 66 ? b - 66 : 0;
            while (q > lo && out[q - 1] != '"') --q;
            if (q > lo) b = q - 1;
        }
        for (size_t n = 0; b > 0 && n < 128 && local(out[b - 1]); ++n) --b;   // a local part is at most 64 characters
        const size_t ds = i + atLen;
        size_t e = ds;
        while (e < s.size() && domain(s[e])) ++e;
        while (e > ds && (s[e - 1] == '.' || s[e - 1] == '-')) --e;
        size_t dot = std::string::npos;   // the last dot inside the domain span only
        for (size_t k = e; k > ds; --k) if (s[k - 1] == '.') { dot = k - 1; break; }
        const bool tld = dot != std::string::npos && dot > ds && e - dot - 1 >= 2 && alpha(s[dot + 1]) && alpha(s[e - 1]);
        if (b < out.size() && tld) { out.resize(b); out += "<redacted>"; i = e; }
        else out += s[i++];
    }
    return out;
}

// %5C, %2F and %40 (any case of the hex digits) become a backslash, / and @; the other spellings of @ that logs and
// JSON use (\u0040, \x40, &#64;, &#x40;, &commat;) become @. Nothing else is decoded.
inline std::string DecodePathEscapes(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const char a = s[i + 1], b = LowerAscii(s[i + 2]);
            const char c = a == '5' && b == 'c' ? '\\' : a == '2' && b == 'f' ? '/' : a == '4' && b == '0' ? '@' : 0;
            if (c) { out += c; i += 2; continue; }
        } else if (s[i] == '\\' || s[i] == '&') {
            size_t n = 0;
            for (const char* at : {"\\u0040", "\\x40", "&#64;", "&#x40;", "&commat;"}) {
                const size_t len = std::strlen(at);
                if (at[0] == s[i] && EqualsNoCase(s, i, at)) { n = len; break; }
            }
            if (n) { out += '@'; i += n - 1; continue; }
        }
        out += s[i];
    }
    return out;
}

// Windows account SIDs (S-1-5-21-<n>-<n>-<n>-<rid>): a stable id of the account. The whole run of digits and dashes
// after S-1-5-21- becomes <redacted>.
inline std::string RedactSids(const std::string& s) {
    static const char prefix[] = "S-1-5-21-";
    const size_t pl = sizeof(prefix) - 1;
    std::string out;
    size_t i = 0, copied = 0;
    while (i < s.size()) {
        if (LowerAscii(s[i]) != 's' || !EqualsNoCase(s, i, prefix) || (i > 0 && std::isalnum(static_cast<unsigned char>(s[i - 1])))) { ++i; continue; }
        size_t e = i + pl;
        while (e < s.size() && (std::isdigit(static_cast<unsigned char>(s[e])) || s[e] == '-')) ++e;
        while (e > i + pl && s[e - 1] == '-') --e;
        if (e == i + pl) { i += pl; continue; }
        out.append(s, copied, i - copied);
        out += "<redacted>";
        copied = i = e;
    }
    if (!copied) return s;
    out.append(s, copied, std::string::npos);
    return out;
}

// Step 1 of the pipeline: paths, then e-mail addresses, on the text with %5C, %2F and %40 decoded, so
// C%3A%5CUsers%5Cname and name%40example.com are found. When nothing was found in the decoded text the original
// bytes are kept, so an ordinary %2F in a URL is not rewritten.
inline std::string PathsAndEmails(const std::string& text, const std::vector<std::string>& profileRoots) {
    const std::string decoded = DecodePathEscapes(text);
    const std::string redacted = RedactSids(RedactEmails(RedactPaths(decoded, profileRoots)));
    return redacted == decoded ? text : redacted;
}

inline constexpr size_t kMinLearnedLength = 3;    // shorter values would hide parts of ordinary words
inline constexpr size_t kMaxLearnedLength = 64;
inline constexpr size_t kMaxPriority = 384;       // values from real login lines: the user's own, never evicted (5 sources x 64)
inline constexpr size_t kMaxLearned = 128;        // values from every other line (relaxed mode); a full list takes no more
inline constexpr size_t kMaxLearnedStrict = 1024; // the same in strict mode (crash reports)
inline constexpr size_t kMaxLegacy = 16384;       // the crash redactor's own handle shapes (strict mode): a separate list, so noise cannot starve them
inline constexpr size_t kMaxLoginPerLine = 32;    // entries (a value and its words each count) one login line can add
inline constexpr size_t kMaxOtherPerLine = 8;     // entries one other line can add (strict mode: kMaxLoginPerLine)
inline constexpr size_t kMaxPriorityPerSource = 64;   // priority values one log file, or one text given to Learn, can add
inline constexpr size_t kMaxScanLoginLines = 4096;    // login lines parsed per scanned file
inline constexpr size_t kMaxScanLoginBytes = 4u << 20;   // bytes of login lines parsed per scanned file
inline constexpr size_t kMaxLearnLine = 16u << 10;   // Learn reads at most this much of a line
inline constexpr size_t kMinDigitsLearned = 7;    // a shorter number would hide ports, versions and times
inline constexpr size_t kSubstringLearnedLength = 6;   // a shorter value is replaced only as a whole token

inline bool CommonWord(const std::string& low) {
    static const char* const common[] = {
        "true", "false", "null", "none", "empty", "default", "unknown", "invalid", "missing", "name", "user", "users", "handle",
        "nickname", "account", "guest", "player", "players", "yes", "not", "set", "redacted", "profile", "userprofile", "the",
        "and", "for", "with", "from", "was", "were", "has", "have", "are", "you", "your", "this", "that", "star", "citizen",
        "server", "client", "game", "loading", "loaded", "started", "starting", "error", "warning", "info", "debug", "spawn",
        "killed", "entered", "left", "login", "logout", "success", "failed", "failure", "status", "character", "session", "admin",
        "local", "remote", "online", "offline", "test", "tester", "main", "list", "item", "items", "data", "value", "type", "mode"};
    for (const char* w : common) if (low == w) return true;
    return false;
}

// How far past a key the closing quote or bracket of a value is searched for. A value with no close inside the
// window is treated as unterminated and hidden up to the end of the window (or the end of the line).
inline constexpr size_t kMaxValueScan = 256;

inline bool ContainsNoCase(const std::string& s, size_t a, size_t b, const char* word) {
    const size_t n = std::strlen(word);
    if (n == 0) return true;
    const char first = LowerAscii(word[0]);
    for (size_t i = a; i + n <= b; ++i) {   // no allocation per position: the login-line scan runs this over tens of megabytes
        if (LowerAscii(s[i]) != first) continue;
        size_t k = 1;
        while (k < n && LowerAscii(s[i + k]) == LowerAscii(word[k])) ++k;
        if (k == n) return true;
    }
    return false;
}

// Values of identity keys become <redacted>; the key stays so the line still reads. A key is matched on its
// last part, case-insensitive, with underscores ignored, so player_id, rsiHandle and targetAccountId all count.
//   - the quoted value after `for user`, and the value after `username` or SetLocalPlayerId;
//   - nickname, nick, displayName, playerName, characterName, charName, screenName, profileName, accountName,
//     handle, uid, and any key ending in playerId, userId, accountId or GEID, after `=` or `:`, quoted or not
//     (also JSON-escaped);
//   - user, owner, player, login and character after `=` or `:` (one word);
//   - handle, nickname, account and the id keys in brackets (Handle[x], GEID [x]);
//   - a number after an id key separated by spaces (`geid 12 - accountId 34`, `geid #12`);
//   - a run of 9 to 12 digits after any key that ends in `id` (ownerId:..., citizen_id=..., id 2000998877,
//     geid(2000998877)), up to three of space = : # ( < - " ' [ between;
//   - `name X` on a line that names an id with a value (the character in the login status line), and `name=X`,
//     `"name":"X"` on a line that also has one of the words login, character, player, account, nickname or handle (as a
//     whole word) or an id with a value. Only the login status line (`name X` next to an id) teaches the name to other lines.
// An unquoted nickname, character name, displayName or username after `=` or `:` may hold up to six words; it
// stops at the next word that looks like a key (`key=`, a bracket), at a lone `-`, or at a plain word such as
// `and`, `went` or `to`. Any word that starts with `Player_` loses what follows the underscore (batch names such
// as Player_<id>), unless it is a lower-case key followed by `=` (player_count=5 keeps its key). `handle=0x..`
// is left alone: that is a system handle. A bare `name=X` on a line without that context is not hidden.
// When `learned` is given, each value that was hidden is added to it (Learn filters them), except the value of the
// weak keys user, owner, player and login, which are hidden where they stand but never taught to other lines.
inline std::string RedactIdentityKeys(const std::string& s, std::vector<std::string>* learned = nullptr) {
    const auto wordChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    const auto blank = [](char c) { return c == ' ' || c == '\t'; };
    const auto isQuote = [](char c) { return c == '\'' || c == '"'; };
    const auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    const auto ends = [](const std::string& w, const char* suf) {
        const size_t n = std::strlen(suf);
        return w.size() >= n && w.compare(w.size() - n, n, suf) == 0;
    };
    const auto idKey = [&](const std::string& k) {
        return ends(k, "geid") || ends(k, "accountid") || ends(k, "playerid") || ends(k, "userid") || ends(k, "rsiid") || k == "uid";
    };
    const auto nickKey = [&](const std::string& k) {
        return ends(k, "nickname") || k == "nick" || ends(k, "displayname") || ends(k, "playername") || ends(k, "charactername") ||
               ends(k, "charname") || ends(k, "screenname") || ends(k, "profilename") || ends(k, "accountname");
    };
    const auto stopWord = [](const std::string& w) {   // plain words that end an unquoted multi-word name
        static const char* const words[] = {"and", "or", "to", "for", "at", "in", "on", "by", "with", "from",
                                            "is", "was", "has", "had", "went", "left", "joined", "entered", "killed", "logged",
                                            "connected", "disconnected", "spawned", "died"};
        const std::string l = LowerAscii(w);
        for (const char* x : words) if (l == x) return true;
        return false;
    };
    const auto notValue = [](const std::string& w) {   // plain words after `username` that are not a name
        static const char* const words[] = {"is", "was", "not", "empty", "null", "none", "missing", "unknown", "invalid",
                                            "required", "must", "cannot", "can", "has", "does", "did", "and", "or", "for"};
        const std::string l = LowerAscii(w);
        for (const char* x : words) if (l == x) return true;
        return false;
    };
    // What a line shows before it is read word by word, so `name X` works whichever side of the id it sits.
    const auto lineInfo = [&](size_t a, bool& idValue, bool& context, bool& nameCtx) {
        size_t b = s.find('\n', a);
        if (b == std::string::npos) b = s.size();
        std::string low;
        low.reserve(b - a);
        for (size_t k = a; k < b; ++k) if (s[k] != '_') low += LowerAscii(s[k]);
        idValue = false;
        for (const char* key : {"geid", "accountid", "playerid", "userid"}) {
            const size_t n = std::strlen(key);
            for (size_t at = low.find(key); at != std::string::npos && !idValue; at = low.find(key, at + 1)) {
                size_t q = at + n;
                while (q < low.size() && (low[q] == '"' || low[q] == '\'' || low[q] == '\\')) ++q;
                while (q < low.size() && (low[q] == ' ' || low[q] == '\t')) ++q;
                idValue = q < low.size() && (low[q] == '=' || low[q] == ':' || low[q] == '[' || low[q] == '#' ||
                                             std::isdigit(static_cast<unsigned char>(low[q])) ||
                                             low.compare(q, 10, "<redacted>") == 0);   // an id RedactLegacyIds already hid
            }
        }
        // The context words count only as whole words (letters and digits; `_` and punctuation separate), so
        // CreateHandle, handler and players do not make a line "about" a login.
        bool hasLogin = false, hasChar = false, hasPlayer = false, hasAccount = false, hasNick = false, hasHandle = false;
        for (size_t k = a; k < b;) {
            if (!std::isalnum(static_cast<unsigned char>(s[k]))) { ++k; continue; }
            size_t e = k;
            while (e < b && std::isalnum(static_cast<unsigned char>(s[e]))) ++e;
            const size_t n = e - k;
            const auto is = [&](const char* w) { return std::strlen(w) == n && EqualsNoCase(s, k, w); };
            if (n >= 5 && n <= 8) {
                hasLogin = hasLogin || is("login");
                hasChar = hasChar || is("character");
                hasPlayer = hasPlayer || is("player");
                hasAccount = hasAccount || is("account");
                hasNick = hasNick || is("nickname");
                hasHandle = hasHandle || is("handle");
            }
            k = e;
        }
        context = idValue || hasLogin || hasChar || hasPlayer || hasAccount || hasNick || hasHandle;
        nameCtx = idValue || hasAccount || hasNick || hasHandle || low.find("geid") != std::string::npos;
    };
    std::string out;
    size_t i = 0;
    bool idLine = false, ctxLine = false, nameCtx = false, atLineStart = true;
    while (i < s.size()) {
        if (atLineStart) { lineInfo(i, idLine, ctxLine, nameCtx); atLineStart = false; }
        if (!wordChar(s[i])) {
            if (s[i] == '\n') atLineStart = true;
            // 'Name' [200099887766] or "Name" [id]: a quoted name followed by a bracketed 9 to 12 digit id.
            if (isQuote(s[i])) {
                size_t c = i + 1;
                while (c < s.size() && c < i + 66 && s[c] != s[i] && s[c] != '\n') ++c;
                if (c < s.size() && s[c] == s[i] && c > i + 1) {
                    size_t b = c + 1;
                    while (b < s.size() && blank(s[b])) ++b;
                    size_t dend = b + 1;
                    while (dend < s.size() && std::isdigit(static_cast<unsigned char>(s[dend]))) ++dend;
                    if (b < s.size() && s[b] == '[' && dend < s.size() && s[dend] == ']' && dend - (b + 1) >= 9 && dend - (b + 1) <= 12) {
                        if (learned) { learned->push_back(s.substr(i + 1, c - i - 1)); learned->push_back(s.substr(b + 1, dend - b - 1)); }
                        out += s[i];
                        out += "<redacted>";
                        out.append(s, c, b + 1 - c);
                        out += "<redacted>";
                        i = dend;
                        continue;
                    }
                }
            }
            out += s[i++];
            continue;
        }
        size_t j = i;
        while (j < s.size() && wordChar(s[j])) ++j;
        const std::string word = s.substr(i, j - i);
        const std::string lw = LowerAscii(word);
        std::string key;
        for (char c : lw) if (c != '_') key += c;
        out += word;
        i = j;
        {   // geid200099887766: an id key with its digits glued on
            size_t nd = 0;
            while (nd < word.size() && std::isdigit(static_cast<unsigned char>(word[word.size() - 1 - nd]))) ++nd;
            if (nd >= 4 && nd < word.size()) {
                std::string base;
                for (size_t k = 0; k + nd < lw.size(); ++k) if (lw[k] != '_') base += lw[k];
                if (idKey(base) || (nd >= 9 && nd <= 12 && ends(base, "id"))) {
                    if (learned) learned->push_back(word.substr(word.size() - nd));
                    out.resize(out.size() - nd);
                    out += "<redacted>";
                    continue;
                }
            }
        }
        size_t p = j;
        bool bracket = false, quotedOnly = false, multiword = false, ok = false, xml = false;
        bool weak = false;   // user=, owner=, player=, login=: hidden here, but not learned for other lines
        const bool isNick = nickKey(key), isId = idKey(key), isHandle = ends(key, "handle"), isAccount = ends(key, "account");
        if (ends(key, "username") || key == "setlocalplayerid") {
            const bool local = key == "setlocalplayerid";
            while (p < s.size() && blank(s[p])) ++p;
            bool sep = false;
            if (local && p < s.size() && s[p] == '(') {   // SetLocalPlayerId(id) or (id=...)
                ++p;
                while (p < s.size() && blank(s[p])) ++p;
                size_t q = p;
                while (q < s.size() && wordChar(s[q])) ++q;
                if (q > p && q < s.size() && s[q] == '=') p = q + 1;
                while (p < s.size() && blank(s[p])) ++p;
            } else if (p < s.size() && (s[p] == '=' || s[p] == ':')) {
                ++p;
                while (p < s.size() && blank(s[p])) ++p;
                sep = true;
            }
            ok = p > j;
            multiword = sep && !local;
            if (ok && !local && !sep) {
                size_t q = p;
                while (q < s.size() && wordChar(s[q])) ++q;
                if (notValue(s.substr(p, q - p))) ok = false;
            }
        } else if (isNick || isId || isHandle || isAccount) {
            size_t q = p;
            if (q < s.size() && s[q] == '\\') ++q;   // a JSON-escaped key
            if (q < s.size() && isQuote(s[q])) ++q;   // a quoted key, as in JSON
            while (q < s.size() && blank(s[q])) ++q;
            if (q < s.size() && (s[q] == '=' || s[q] == ':') && !isAccount) {
                p = q + 1;
                while (p < s.size() && blank(s[p])) ++p;
                ok = true;
                multiword = isNick;
                if (isHandle && p + 1 < s.size() && s[p] == '0' && (s[p + 1] == 'x' || s[p + 1] == 'X')) ok = false;
            } else {
                q = j;
                while (q < s.size() && blank(s[q])) ++q;
                if (q < s.size() && s[q] == '[' && (!isNick || ends(key, "nickname"))) {
                    p = q + 1; bracket = ok = true;
                } else if (isId && q > j && q < s.size() &&
                           (std::isdigit(static_cast<unsigned char>(s[q])) ||
                            (s[q] == '#' && q + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[q + 1]))))) {
                    p = s[q] == '#' ? q + 1 : q; ok = true;   // geid 123456, geid #123456
                } else if (isId && q == j && q + 1 < s.size() && isQuote(s[q]) && std::isdigit(static_cast<unsigned char>(s[q + 1]))) {
                    p = q; ok = true;   // accountid"5544332211"
                } else if (j < s.size() && s[j] == '>' && out.size() > word.size() && out[out.size() - word.size() - 1] == '<') {
                    p = j + 1; ok = xml = true;   // <Nickname>x</Nickname>, <geid>1</geid>
                }
            }
        } else if (key == "name" && ctxLine) {   // "geid 1 - accountId 2 - name X", name=X, "name":"X"
            size_t q = p;
            if (q < s.size() && isQuote(s[q])) ++q;
            const size_t afterKey = q;
            while (q < s.size() && blank(s[q])) ++q;
            bool sep = false;
            if (q < s.size() && (s[q] == '=' || s[q] == ':')) {
                sep = true;
                ++q;
                while (q < s.size() && blank(s[q])) ++q;
            }
            if (!sep && afterKey == p && p < s.size() && s[p] == '[' && p + 1 < s.size() && nameCtx) {
                p = p + 1; bracket = ok = true;   // Name[x] on a line with a handle, account or id
            } else if (q < s.size() && (wordChar(s[q]) || isQuote(s[q])) && (sep || (idLine && q > afterKey))) {
                p = q; ok = true; multiword = true;
                weak = !idLine;   // a bare name=X is hidden where it stands; only the login status line teaches it
            }
        } else if (lw == "user" || lw == "owner" || lw == "player" || lw == "login" || lw == "character") {
            size_t q = p;
            while (q < s.size() && blank(s[q])) ++q;
            if (lw == "player" && p < s.size() && s[p] == '[') {   // Player[Name] Requesting Inventory
                p = p + 1; bracket = ok = true;
            } else if (q + 1 < s.size() && (s[q] == '=' || s[q] == ':') && !(s[q] == ':' && s[q + 1] == ':')) {
                p = q + 1;
                while (p < s.size() && blank(s[p])) ++p;
                ok = p < s.size() && !space(s[p]);
                weak = lw != "character";
                if (ok) {   // not when the value is itself the next key (`Character: geid 5`, `user: name=x`)
                    size_t t = p;
                    std::string vw, vraw;
                    while (t < s.size() && wordChar(s[t])) { vraw += LowerAscii(s[t]); if (s[t] != '_') vw += LowerAscii(s[t]); ++t; }
                    if (idKey(vw) || nickKey(vw) || ends(vw, "handle") || ends(vw, "account") || vw == "name" || stopWord(vw) ||
                        (t < s.size() && (s[t] == '=' || s[t] == ':'))) ok = false;
                    // `Character: createdAt 1581606700563 - ...`: a word that is followed by a number is a key, not a name.
                    {
                        size_t u = t;
                        while (u < s.size() && blank(s[u])) ++u;
                        if (u > t && u < s.size() && std::isdigit(static_cast<unsigned char>(s[u]))) ok = false;
                    }
                    // Ordinary values are not names: a flag, a common word, a short number or a hex handle.
                    if (!vraw.empty() && (CommonWord(vraw) || vraw.compare(0, 2, "0x") == 0 ||
                                          (vraw.size() < kMinDigitsLearned && vraw.find_first_not_of("0123456789") == std::string::npos)))
                        ok = false;
                    // owner, login and character are weak keys: they count only on a line about a handle, account or id.
                    if ((lw == "owner" || lw == "login" || lw == "character") && !nameCtx) ok = false;
                }
            } else if (lw == "user") {
                const size_t k = out.size() - word.size();
                size_t b = k;
                while (b > 0 && blank(out[b - 1])) --b;
                if (b < k && b >= 3 && EqualsNoCase(out, b - 3, "for") && (b == 3 || !wordChar(out[b - 4]))) {
                    while (p < s.size() && blank(s[p])) ++p;
                    ok = p > j && p < s.size() && isQuote(s[p]);
                    quotedOnly = true;
                }
            }
        }
        if (!ok && key.size() >= 2 && ends(key, "id")) {   // ownerId:5544332211, citizen_id=2000998877, id 2000998877, geid(2000998877)
            size_t q = j;
            if (q < s.size() && s[q] == '\\') ++q;
            for (size_t seps = 0; q < s.size() && seps < 3 && s[q] && std::strchr(" \t=:#(<-\"'[", s[q]); ++q, ++seps) {}
            size_t d = q;
            while (d < s.size() && std::isdigit(static_cast<unsigned char>(s[d]))) ++d;
            if (d - q >= 9 && d - q <= 12 && (d >= s.size() || !std::isalnum(static_cast<unsigned char>(s[d])))) { p = q; ok = true; }
        }
        if (!ok && word.size() > 7 && EqualsNoCase(word, 0, "player_")) {   // Player_<id> batch names
            size_t q = j;
            while (q < s.size() && blank(s[q])) ++q;
            // A word followed by `=` or `:` (`Player_Count=5`, `PLAYER_COUNT: 5`) is a key and keeps its name; any
            // other Player_<x> is a batch name and loses what follows, even if it is only an event name.
            const bool keyLike = q < s.size() && (s[q] == '=' || s[q] == ':') && !(q + 1 < s.size() && s[q] == ':' && s[q + 1] == ':');
            if (!keyLike) {
                out.resize(out.size() - word.size() + 7);
                out += "<redacted>";
                continue;
            }
        }
        if (!ok || p >= s.size()) continue;
        if (!bracket && s[p] == '[' && p + 1 < s.size() && isQuote(s[p + 1])) ++p;   // "nickname":["x"]
        size_t vs = p, e = p;
        if (bracket) {
            const size_t lim = (std::min)(s.size(), p + kMaxValueScan);
            size_t close = std::string::npos;
            for (size_t c = p; c < lim; ++c) if (s[c] == ']') { close = c; break; }
            if (close != std::string::npos) e = close;
            else {   // no close in the window: hide the rest of the token
                while (vs < s.size() && blank(s[vs])) ++vs;
                e = vs;
                while (e < s.size() && !space(s[e])) ++e;
            }
        } else if (isQuote(s[p]) || (s[p] == '\\' && p + 1 < s.size() && isQuote(s[p + 1]))) {
            const bool esc = s[p] == '\\';
            const size_t qp = esc ? p + 1 : p;
            const char q = s[qp];
            const size_t lim = (std::min)(s.size(), qp + 1 + kMaxValueScan);
            size_t close = std::string::npos, stop = lim;
            for (size_t c = qp + 1; c < lim; ++c) {
                if (s[c] == '\n') { stop = c; break; }
                if (s[c] == q && (esc || s[c - 1] != '\\')) { close = c; break; }
            }
            if (close != std::string::npos) {
                vs = qp + 1; e = close;
                if (esc && e > vs && s[e - 1] == '\\') --e;
            } else if (quotedOnly) continue;
            else e = stop;   // unterminated: hide up to the end of the window or line
        } else {
            if (quotedOnly) continue;
            if (xml) {   // the value runs to the closing tag
                while (e < s.size() && s[e] != '<' && s[e] != '\n') ++e;
                if (e > vs && learned) learned->push_back(s.substr(vs, e - vs));
                if (e > vs) { out.append(s, j, vs - j); out += "<redacted>"; i = e; }
                continue;
            }
            const auto apostrophe = [&](size_t k) {   // O'Brien: an apostrophe between two letters is part of the name
                return s[k] == '\'' && k > p && k + 1 < s.size() && std::isalpha(static_cast<unsigned char>(s[k - 1])) &&
                       std::isalpha(static_cast<unsigned char>(s[k + 1]));
            };
            while (e < s.size() && !space(s[e]) && (!std::strchr(",;)]}\"'", s[e]) || apostrophe(e))) ++e;
            for (int more = 0; multiword && more < 5 && e < s.size() && blank(s[e]); ++more) {
                size_t q = e;
                while (q < s.size() && blank(s[q])) ++q;
                size_t t = q;
                // a word runs to the next space, comma, quote or bracket; a letter-apostrophe-letter stays inside it
                while (t < s.size() && !space(s[t]) && (!std::strchr(",;)]}\"'", s[t]) || apostrophe(t))) ++t;
                const std::string next = s.substr(q, t - q);
                if (t == q || next == "-" || next.find_first_of("=[<]()\"',;:{}") != std::string::npos) {
                    // keep a word that only has apostrophes between letters (O'Brien)
                    bool onlyApos = t > q && next != "-" && stopWord(next) == false;
                    for (char c : next) if (std::strchr("=[<]()\",;:{}", c)) onlyApos = false;
                    if (!onlyApos) break;
                }
                if (stopWord(next)) break;
                e = t;
            }
        }
        if (e <= vs) continue;
        if (learned && !weak) learned->push_back(s.substr(vs, e - vs));
        out.append(s, j, vs - j);
        out += "<redacted>";
        i = e;
    }
    return out;
}

// --- the ids the crash redactor before this one hid ------------------------------------------------

// "geid 200012345", "accountId=9876543", "playerGEID: 1", `geid""1234`, geid123: one of geid, accountid, account_id
// and playerid (so playergeid too) in any case, up to three of space, = : " [ after it, then three or more digits.
// The digits become <redacted>. A key glued to a letter or digit before it does not count (xgeid), except after an
// `r`. This is the old crash redactor's rule, kept whole so that no id shape it hid is shown now, in either mode.
inline std::string RedactLegacyIds(const std::string& s) {
    static const char* const keys[] = {"geid", "accountid", "account_id", "playerid"};
    const auto asciiAlnum = [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    std::string out;
    size_t copied = 0;                       // s[0, copied) is already in out
    size_t lastEnd = std::string::npos;      // where the last hidden digits ended: what stands before is now a marker
    size_t i = 0;
    while (i < s.size()) {
        const char c = LowerAscii(s[i]);
        if (c != 'g' && c != 'a' && c != 'p') { ++i; continue; }
        size_t kl = 0;
        for (const char* key : keys) {
            const size_t n = std::strlen(key);
            if (i + n > s.size()) continue;
            size_t k = 0;
            while (k < n && LowerAscii(s[i + k]) == key[k]) ++k;
            if (k == n) { kl = n; break; }
        }
        if (!kl) { ++i; continue; }
        if (i > 0 && i != lastEnd && asciiAlnum(s[i - 1]) && LowerAscii(s[i - 1]) != 'r') { i += kl; continue; }
        size_t a = i + kl, seps = 0;
        while (a < s.size() && seps < 3 && (s[a] == ' ' || s[a] == '=' || s[a] == ':' || s[a] == '"' || s[a] == '[')) { ++a; ++seps; }
        size_t e = a;
        while (e < s.size() && s[e] >= '0' && s[e] <= '9') ++e;
        if (e - a < 3) { i += kl; continue; }
        out.append(s, copied, a - copied);
        out += "<redacted>";
        copied = lastEnd = i = e;
    }
    if (copied == 0) return s;
    out.append(s, copied, std::string::npos);
    return out;
}

// --- identity learned from the text ---------------------------------------------------------------

// The values learned from a file, the profile roots and the Windows account names: what Redact knows about.
struct Identities {
    std::vector<std::string> profileRoots;   // USERPROFILE, for RedactPaths
    std::vector<std::string> accountNames;   // the Windows account name, for RedactAccountNames
    // Values from real login lines ("User Login Success", "AccountLoginCharacterStatus"): the user's own handle,
    // nickname, character name and ids. At most kMaxPriority; once full, a new one is dropped, never an old one.
    std::vector<std::string> priority;
    // Values from every other line. At most kMaxLearned (kMaxLearnedStrict in strict mode); once full, a new one is dropped.
    std::vector<std::string> learned;
    // Strict mode only: the crash redactor's own handle shapes (Handle[..], Nickname[..], nickname="..", handle=".."),
    // a list of their own (at most kMaxLegacy) so that noise from other values cannot starve them.
    std::vector<std::string> legacy;
    // How many values were refused because a list was full: login-line values (priority, or the per-source share)
    // and legacy handle shapes. The callers turn a non-zero count into a notice.
    size_t priorityDropped = 0, legacyDropped = 0;
    // Strict mode (crash reports): every learned value is hidden anywhere, also inside other words, and the
    // Handle[..], Nickname[..], nickname=".." and handle=".." values of the whole text are learned even when they
    // are a common word or a short number. It over-redacts on purpose.
    bool strict = false;
    // The lower-case form of every value in priority, learned and legacy, so a value is looked up in constant time.
    std::unordered_set<std::string> seen;
};

enum class Source { Other, Login, Legacy };   // Legacy: the crash redactor's own handle shapes (strict mode only)

// Adds a value to a learned list when it is worth replacing everywhere: 3 to 64 characters, not a marker, a hex
// number or one of the machine words of SystemToken, and not already known. A leading `[` is dropped. A short
// number or a plain word the logs use (CommonWord) is learned only from a login line, or, in strict mode, from the
// crash redactor's handle shapes; `allowCommon` false (the words of a multi-word name) refuses common words always.
inline void AddLearned(Identities& id, std::string v, Source src = Source::Other, bool allowCommon = true,
                       size_t priorityRoom = kMaxPriority) {
    const auto trim = [](std::string& t) {
        while (!t.empty() && (t.back() == ' ' || t.back() == '\t' || t.back() == ']')) t.pop_back();
        size_t a = 0;
        while (a < t.size() && (t[a] == ' ' || t[a] == '\t' || t[a] == '[')) ++a;
        t.erase(0, a);
    };
    trim(v);
    if (v.size() < kMinLearnedLength || v.size() > kMaxLearnedLength) return;
    if (v.find_first_of("<>%\r\n") != std::string::npos) return;
    const std::string low = LowerAscii(v);
    // `handle=0x7ff...` style system handles are not names: skipped when they come from an ordinary line in relaxed
    // mode. A handle a login line announces, or the crash redactor's own shapes, is kept even when it starts with 0x.
    if (low.compare(0, 2, "0x") == 0 && src == Source::Other && !id.strict) return;
    // Strict mode (crash reports) hides what the old crash redactor hid: a handle from its own shapes or from a login
    // line is hidden everywhere even when it is a machine word (Nobody, red). The matcher leaves the redaction
    // markers themselves alone (MarkerLen), so a value such as `red`, `ted` or `use` cannot damage `<redacted>`.
    const bool strictOwn = id.strict && (src == Source::Legacy || src == Source::Login) && allowCommon;
    const bool ownValue = allowCommon && (src == Source::Login || (src == Source::Legacy && id.strict));
    // A machine word (SYSTEM, Nobody) is never taught, except in strict mode from a login line or a legacy handle shape.
    if (!strictOwn && SystemToken(low)) return;
    // Pieces of a marker (red, ted, use) from an ordinary line are not names. From a login line or a legacy handle
    // shape they are: the matcher leaves the markers alone, so the value is safe to hide.
    if (src == Source::Other && !strictOwn) {
        static const char* const markers[] = {"<redacted>", "<user>", "%userprofile%"};
        for (const char* m : markers) if (std::string(m).find(low) != std::string::npos) return;
    }
    if (CommonWord(low) && !ownValue) return;
    const bool shortNumber = v.size() < kMinDigitsLearned && v.find_first_not_of("0123456789") == std::string::npos;
    if (shortNumber && !(src == Source::Legacy && id.strict)) return;
    if (id.seen.count(low)) return;
    if (src == Source::Login) {
        if (id.priority.size() >= kMaxPriority || priorityRoom == 0) { ++id.priorityDropped; return; }
        id.seen.insert(low);
        id.priority.push_back(std::move(v));
        return;
    }
    if (src == Source::Legacy) {
        if (id.legacy.size() >= kMaxLegacy) { ++id.legacyDropped; return; }
        id.seen.insert(low);
        id.legacy.push_back(std::move(v));
        return;
    }
    if (id.learned.size() >= (id.strict ? kMaxLearnedStrict : kMaxLearned)) return;
    id.seen.insert(low);
    id.learned.push_back(std::move(v));
}

// The old crash redactor's handle shapes: the plain text between `Handle[` and `]`, `Nickname[` and `]`,
// `nickname="` and `"`, `handle="` and `"`, wherever those keys appear (also as the end of a longer word), when it
// is 3 to 64 letters, digits, `_` or `-`. Strict mode only: Learn calls it.
inline void LearnLegacyHandles(Identities& id, const std::string& text) {
    for (const char* key : {"Handle[", "Nickname[", "nickname=\"", "handle=\""}) {
        const size_t kl = std::strlen(key);
        const char close = key[kl - 1] == '[' ? ']' : '"';
        for (size_t p = text.find(key); p != std::string::npos; p = text.find(key, p + 1)) {
            const size_t a = p + kl;
            size_t b = a;
            while (b < text.size() && b - a <= 64 && text[b] != close) ++b;
            if (b >= text.size() || text[b] != close || b == a || b - a > 64) continue;
            bool plain = true;
            for (size_t k = a; k < b && plain; ++k) {
                const char c = text[k];
                plain = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
            }
            if (plain) AddLearned(id, text.substr(a, b - a), Source::Legacy);
        }
    }
}

// A line of a real login: what the game writes when the user signs in and when it names the character.
inline bool IsLoginLine(const std::string& line) {
    return ContainsNoCase(line, 0, line.size(), "user login success") || ContainsNoCase(line, 0, line.size(), "accountlogincharacterstatus");
}

// Learns from one line (at most kMaxLearnLine of it): the values the identity keys announce. A login line's values
// go to the priority list, at most `*priorityRoom` new ones (when given; the room shrinks by what was added); any other
// line adds at most kMaxOtherPerLine entries. A multi-word value (it has a space) also teaches each of its words of
// three or more characters, such as a first name; a value joined by `_` or `-` (an NPC id such as PU_Human_Enemy_5555)
// is taught whole only. The per-line cap counts every entry, a value and each of its words.
inline void LearnLine(Identities& id, const std::string& rawLine, bool login, size_t* priorityRoom = nullptr) {
    const std::string line = rawLine.size() > kMaxLearnLine ? rawLine.substr(0, kMaxLearnLine) : rawLine;
    std::vector<std::string> found;
    RedactIdentityKeys(line, &found);
    const size_t cap = login || id.strict ? kMaxLoginPerLine : kMaxOtherPerLine;
    const Source src = login ? Source::Login : Source::Other;
    const size_t before = id.priority.size();
    const size_t room = priorityRoom ? *priorityRoom : kMaxPriority;
    size_t used = 0;
    const auto add = [&](std::string v, bool allowCommon) {
        if (used >= cap) return false;
        ++used;
        const size_t added = id.priority.size() - before;
        AddLearned(id, std::move(v), src, allowCommon, room > added ? room - added : 0);
        return true;
    };
    for (const std::string& v : found) {
        if (!add(v, true)) break;
        if (v.find_first_of(" \t") == std::string::npos) continue;
        size_t a = 0;
        while (a < v.size()) {
            while (a < v.size() && !std::isalnum(static_cast<unsigned char>(v[a])) && static_cast<unsigned char>(v[a]) < 0x80) ++a;
            size_t b = a;
            while (b < v.size() && (std::isalnum(static_cast<unsigned char>(v[b])) || static_cast<unsigned char>(v[b]) >= 0x80)) ++b;
            if (b > a && (a > 0 || b < v.size()) && !add(v.substr(a, b - a), false)) break;
            a = b == a ? a + 1 : b;
        }
    }
    if (priorityRoom) *priorityRoom -= (std::min)(*priorityRoom, id.priority.size() - before);
}

// Reads every identity value the text announces (the same keys RedactIdentityKeys hides) into the learned lists.
// Only a line that shows a login shape ranks first, and the text as a whole may add at most kMaxPriorityPerSource
// such values: any other line that merely mentions a login could be hostile or noisy. Call it on the whole text that
// will be redacted, not only the part that is shown.
inline void Learn(Identities& id, const std::string& text) {
    if (id.strict) LearnLegacyHandles(id, text);
    size_t room = kMaxPriorityPerSource;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = text.substr(pos, (std::min)(nl - pos, kMaxLearnLine));
        pos = nl + 1;
        LearnLine(id, line, IsLoginLine(line), &room);
    }
}

// Feeds a file in chunks of any size and learns from the login lines only: the cheap way to read a log of tens of
// megabytes. A line is cut at its newline across chunks. Only the first kMaxLearnLine bytes of a line are kept; if a
// login line is longer than that, those bytes are still read and CutLogin() is true. The work is bounded whatever the
// file holds: at most kMaxScanLoginLines login lines and kMaxScanLoginBytes bytes of them are parsed (Capped() says a
// line was left out), and the file adds at most kMaxPriorityPerSource priority values, so one noisy log cannot fill
// the list ahead of the others. `prepare` cleans a line first (for example makes it valid UTF-8).
class LoginScanner {
public:
    explicit LoginScanner(Identities& id, std::string (*prepare)(const std::string&) = nullptr) : id_(id), prepare_(prepare) {}

    void Feed(const char* data, size_t n) {
        size_t at = 0;
        while (at < n) {
            const char* nl = static_cast<const char*>(std::memchr(data + at, '\n', n - at));
            const size_t end = nl ? static_cast<size_t>(nl - data) : n;
            if (!skipping_) {
                const size_t room = kMaxLearnLine - (std::min)(kMaxLearnLine, line_.size());
                const size_t take = (std::min)(end - at, room);
                line_.append(data + at, take);
                if (take < end - at) skipping_ = true;   // keep the first kMaxLearnLine bytes, drop the rest
            }
            at = end;
            if (nl) { ++at; EndLine(); }
        }
    }
    void Finish() { EndLine(); }   // the last line of a file may have no newline
    bool Capped() const { return capped_; }       // a login line was left out: too many, or too much text
    bool CutLogin() const { return cutLogin_; }   // a login line was longer than kMaxLearnLine: only its start was read

private:
    void EndLine() {
        if (!line_.empty()) {
            while (!line_.empty() && line_.back() == '\r') line_.pop_back();
            if (IsLoginLine(line_)) {
                if (skipping_) cutLogin_ = true;
                if (lines_ >= kMaxScanLoginLines || bytes_ >= kMaxScanLoginBytes) capped_ = true;
                else {
                    ++lines_;
                    bytes_ += line_.size();
                    LearnLine(id_, prepare_ ? prepare_(line_) : line_, true, &room_);
                }
            }
        }
        line_.clear();
        skipping_ = false;
    }
    Identities& id_;
    std::string (*prepare_)(const std::string&);
    std::string line_;
    bool skipping_ = false, capped_ = false, cutLogin_ = false;
    size_t lines_ = 0, bytes_ = 0, room_ = kMaxPriorityPerSource;
};

// Every learned value becomes <redacted>. A value of kSubstringLearnedLength or more characters (or one with a
// character other than a letter or digit) is found anywhere, in any ASCII case, inside other words too, with an
// Aho-Corasick automaton: one pass, whatever the number of values. A shorter value, or an ordinary word such as a
// handle called `Character`, is replaced only as a whole token (a run of letters, digits and non-ASCII bytes), in
// relaxed mode. Strict mode finds every value anywhere. Touching or overlapping hits become one marker.
struct LearnedMatcher {
    int cls[256] = {};
    size_t width = 1;
    std::vector<int> next, longest;     // the automaton: transitions and the longest value ending at a node
    std::vector<std::string> tokens;    // short values, sorted

    explicit LearnedMatcher(const std::vector<std::string>& values, bool strict = false) {
        std::vector<std::string> longValues;
        for (const std::string& v : values) {
            if (v.empty()) continue;
            bool plain = true;
            for (char c : v) if (!std::isalnum(static_cast<unsigned char>(c)) && static_cast<unsigned char>(c) < 0x80) plain = false;
            const std::string low = LowerAscii(v);
            // relaxed mode: a short value or an ordinary word is replaced only as a whole token
            const bool wholeToken = plain && !strict && (v.size() < kSubstringLearnedLength || CommonWord(low));
            (wholeToken ? tokens : longValues).push_back(low);
        }
        std::sort(tokens.begin(), tokens.end());
        if (longValues.empty()) return;
        // alphabet: the bytes that occur in values, plus one class for everything else
        size_t ncls = 1;
        for (const std::string& v : longValues)
            for (char c : v) if (!cls[static_cast<unsigned char>(c)]) cls[static_cast<unsigned char>(c)] = static_cast<int>(ncls++);
        const size_t W = width = ncls;
        next.assign(W, -1);   // node 0 is the root
        longest.assign(1, 0);
        std::vector<int> fail(1, 0);
        for (const std::string& v : longValues) {
            size_t n = 0;
            for (char c : v) {
                const size_t k = static_cast<size_t>(cls[static_cast<unsigned char>(c)]);
                if (next[n * W + k] < 0) {
                    next[n * W + k] = static_cast<int>(longest.size());
                    longest.push_back(0); fail.push_back(0);
                    next.resize(longest.size() * W, -1);
                }
                n = static_cast<size_t>(next[n * W + k]);
            }
            longest[n] = static_cast<int>(v.size());
        }
        std::vector<size_t> queue;
        for (size_t k = 0; k < W; ++k) {
            int& t = next[k];
            if (t < 0) t = 0; else { fail[static_cast<size_t>(t)] = 0; queue.push_back(static_cast<size_t>(t)); }
        }
        for (size_t h = 0; h < queue.size(); ++h) {
            const size_t n = queue[h];
            longest[n] = (std::max)(longest[n], longest[static_cast<size_t>(fail[n])]);
            for (size_t k = 0; k < W; ++k) {
                int& t = next[n * W + k];
                const int viaFail = next[static_cast<size_t>(fail[n]) * W + k];
                if (t < 0) t = viaFail;
                else { fail[static_cast<size_t>(t)] = viaFail; queue.push_back(static_cast<size_t>(t)); }
            }
        }
    }

    std::string Apply(const std::string& text) const {
        std::string cur;
        const std::string* src = &text;
        if (!longest.empty()) {
            std::vector<short> diff(text.size() + 1, 0);
            size_t state = 0;
            bool any = false;
            for (size_t i = 0; i < text.size(); ++i) {
                if (const size_t m = MarkerLen(text, i)) { state = 0; i += m - 1; continue; }
                state = static_cast<size_t>(next[state * width + static_cast<size_t>(cls[static_cast<unsigned char>(LowerAscii(text[i]))])]);
                const int len = longest[state];
                if (len > 0) { ++diff[i + 1 - static_cast<size_t>(len)]; --diff[i + 1]; any = true; }
            }
            if (any) {
                cur.reserve(text.size());
                int depth = 0;
                bool hidden = false;
                for (size_t i = 0; i < text.size(); ++i) {
                    depth += diff[i];
                    if (depth > 0) { if (!hidden) cur += "<redacted>"; hidden = true; }
                    else { hidden = false; cur += text[i]; }
                }
                src = &cur;
            }
        }
        if (tokens.empty()) return *src;
        const std::string& in = *src;
        const auto tokenChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80; };
        std::string out;
        out.reserve(in.size());
        size_t i = 0;
        while (i < in.size()) {
            if (const size_t m = MarkerLen(in, i)) { out.append(in, i, m); i += m; continue; }
            if (!tokenChar(in[i])) { out += in[i++]; continue; }
            size_t e = i;
            while (e < in.size() && tokenChar(in[e])) ++e;
            const bool hit = std::binary_search(tokens.begin(), tokens.end(), LowerAscii(in.substr(i, e - i)));
            if (hit) out += "<redacted>"; else out.append(in, i, e - i);
            i = e;
        }
        return out;
    }
};

inline std::string RedactLearned(const std::string& text, const std::vector<std::string>& values) {
    return LearnedMatcher(values).Apply(text);
}

// Redacts many strings with the same identities: the learned values are compiled once.
class Redactor {
public:
    explicit Redactor(const Identities& id) : id_(id), matcher_(AllLearned(id), id.strict) {}
    std::string Apply(const std::string& text) const {
        // The key pass runs before the matcher as well as after it: a learned value that equals a key word (a handle
        // called Account) must not turn the key into a marker before the key pass has hidden the value it announces.
        return RedactIdentityKeys(matcher_.Apply(RedactIdentityKeys(
            RedactAccountNames(RedactLegacyIds(PathsAndEmails(text, id_.profileRoots)), id_.accountNames))));
    }
private:
    static std::vector<std::string> AllLearned(const Identities& id) {
        std::vector<std::string> values = id.priority;
        values.insert(values.end(), id.learned.begin(), id.learned.end());
        values.insert(values.end(), id.legacy.begin(), id.legacy.end());
        return values;
    }
    Identities id_;
    LearnedMatcher matcher_;
};

// Everything a string goes through (see the top of this file). Idempotent.
inline std::string RedactWith(const std::string& text, const Identities& id) { return Redactor(id).Apply(text); }

// The same without learned values.
inline std::string Redact(const std::string& text, const std::vector<std::string>& profileRoots,
                          const std::vector<std::string>& accountNames = {}) {
    Identities id;
    id.profileRoots = profileRoots;
    id.accountNames = accountNames;
    return RedactWith(text, id);
}

} // namespace sco::launcher::redact
