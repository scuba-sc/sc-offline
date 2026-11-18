// SPDX-License-Identifier: GPL-3.0-only
// Differential check of the shared redactor (launcher/redact.h) against the crash-report redactor it replaced.
// The old_* functions below are copied verbatim from launcher/launcher.cpp at commit 2b4ac1ea ("Docs: correct stale
// facts and cut repetition for 2.4.1", lines 1294 to 1356) with only the names changed (old_ prefix) and `static`
// kept. The check: over a generated grid of keys x separators x digit lengths x quoting, and over realistic log lines,
// every word or number the old redactor hid is hidden by the new strict mode too; the id digits are hidden by the
// relaxed mode too. Names and ids are made up. Portable: no files, no Windows headers.
#include "../launcher/redact.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {

// ---- verbatim from launcher/launcher.cpp at 2b4ac1ea (see the top of this file) ----
static const char* old_kRedactKeys[] = { "Handle", "Nickname", "GEID", "PlayerGEID", "AccountId", "Account", "accountId", "playerGEID" };

static std::vector<std::string> old_HandlesIn(const std::string& text) {
    std::vector<std::string> found;
    for (const char* key : { "Handle[", "Nickname[", "nickname=\"", "handle=\"" }) {
        const char close = key[strlen(key) - 1] == '[' ? ']' : '"';
        for (size_t p = text.find(key); p != std::string::npos; p = text.find(key, p + 1)) {
            const size_t a = p + strlen(key), b = text.find(close, a);
            if (b == std::string::npos || b == a || b - a > 64) continue;
            const std::string h = text.substr(a, b - a);
            bool plain = true;
            for (char c : h) if (!(isalnum((unsigned char)c) || c == '_' || c == '-')) plain = false;
            if (plain && h.size() >= 3 && std::find(found.begin(), found.end(), h) == found.end()) found.push_back(h);
        }
    }
    return found;
}

static void old_ReplaceAllNoCase(std::string& t, const std::string& what, const std::string& with) {
    if (what.empty()) return;
    std::string low = t, w = what;
    for (char& c : low) c = (char)tolower((unsigned char)c);
    for (char& c : w) c = (char)tolower((unsigned char)c);
    std::string out; size_t from = 0;
    for (size_t p = low.find(w); p != std::string::npos; p = low.find(w, p + w.size())) {
        out.append(t, from, p - from); out += with; from = p + w.size();
    }
    out.append(t, from, std::string::npos);
    t.swap(out);
}

// Redacts one file's text. handles: every RSI handle seen in any bundled file; user: Windows user name.
static std::string old_Redact(std::string t, const std::vector<std::string>& handles, const std::string& user) {
    for (const char* key : old_kRedactKeys) {
        const std::string k = std::string(key) + "[";
        for (size_t p = t.find(k); p != std::string::npos; p = t.find(k, p + 1)) {
            const size_t a = p + k.size(), b = t.find(']', a);
            if (b == std::string::npos || b - a > 80) continue;
            const bool numeric = !strcmp(key, "Handle") || !strcmp(key, "Nickname") ? false : true;
            t.replace(a, b - a, numeric ? "<id>" : "<handle>");
        }
    }
    // "geid 200012345", "accountId=9876543", "playerGEID: 1": a key, up to 2 separators, then digits.
    std::string low = t;
    for (char& c : low) c = (char)tolower((unsigned char)c);
    for (const char* key : { "geid", "accountid", "account_id", "playerid" }) {
        const size_t kl = strlen(key);
        for (size_t p = low.find(key); p != std::string::npos; p = low.find(key, p + kl)) {
            if (p && isalnum((unsigned char)low[p - 1]) && low[p - 1] != 'r') continue;   // "playergeid" ok, "xgeid" no
            size_t a = p + kl, seps = 0;
            while (a < low.size() && seps < 3 && (low[a] == ' ' || low[a] == '=' || low[a] == ':' || low[a] == '"' || low[a] == '[')) { ++a; ++seps; }
            size_t b = a; while (b < low.size() && isdigit((unsigned char)low[b])) ++b;
            if (b - a < 3) continue;
            t.replace(a, b - a, "<id>"); low.replace(a, b - a, "<id>");
        }
    }
    for (const std::string& h : handles) old_ReplaceAllNoCase(t, h, "<handle>");
    if (user.size() >= 2) {
        for (const char* sep : { "\\Users\\", "/Users/", "\\\\Users\\\\", "/home/" })
            old_ReplaceAllNoCase(t, std::string(sep) + user, std::string(sep) + "<user>");
    }
    return t;
}
// ---- end of the verbatim copy ----

using sco::launcher::redact::Identities;
using sco::launcher::redact::Learn;
using sco::launcher::redact::RedactWith;

unsigned checks = 0, grid_cases = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

std::string Low(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The maximal runs of letters, digits and underscores of at least 3 characters.
std::vector<std::string> Tokens(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        const auto word = [&](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
        if (!word(s[i])) { ++i; continue; }
        size_t e = i;
        while (e < s.size() && word(s[e])) ++e;
        if (e - i >= 3) out.push_back(s.substr(i, e - i));
        i = e;
    }
    return out;
}

bool AllDigits(const std::string& t) { return t.find_first_not_of("0123456789") == std::string::npos; }

// Every token of `input` that the old output no longer contains must be gone from `now` too (case-insensitive).
// digitsOnly limits the check to numbers (relaxed mode: it keeps short and ordinary words on purpose).
// Returns the first token that is still shown, or "".
std::string Missed(const std::string& input, const std::string& was, const std::string& now, bool digitsOnly) {
    // the redaction markers are not text: a learned `red` or `use` is checked outside them
    std::string lowNow = Low(now);
    for (const char* marker : {"<redacted>", "<user>", "%userprofile%"})
        for (size_t at = lowNow.find(marker); at != std::string::npos; at = lowNow.find(marker, at)) lowNow.erase(at, std::strlen(marker));
    const std::string lowWas = Low(was);
    for (const std::string& t : Tokens(input)) {
        const std::string lt = Low(t);
        if (digitsOnly && !AllDigits(t)) continue;
        if (lowWas.find(lt) != std::string::npos) continue;   // the old redactor kept it
        if (lowNow.find(lt) != std::string::npos) return t;
    }
    return "";
}

// A file's worth of text through the old redactor (handles seen in any file) and the new one.
struct Pair { std::string was, strict, relaxed; };

Pair Both(const std::vector<std::string>& files, size_t which, const std::string& user) {
    std::vector<std::string> handles;
    for (const std::string& f : files)
        for (const std::string& h : old_HandlesIn(f)) if (std::find(handles.begin(), handles.end(), h) == handles.end()) handles.push_back(h);
    Identities strict, relaxed;
    strict.strict = true;
    for (Identities* id : {&strict, &relaxed}) {
        if (!user.empty()) {
            id->profileRoots = {"C:\\Users\\" + user};
            id->accountNames = {user};
        }
        for (const std::string& f : files) Learn(*id, f);
    }
    return {old_Redact(files[which], handles, user), RedactWith(files[which], strict), RedactWith(files[which], relaxed)};
}

void Expect(const std::vector<std::string>& files, size_t which, const std::string& user, bool relaxedToo, const char* what) {
    ++grid_cases;
    const Pair p = Both(files, which, user);
    const std::string miss = Missed(files[which], p.was, p.strict, false);
    if (!miss.empty()) {
        std::cerr << what << ": strict mode shows `" << miss << "`\n  input:  " << files[which] << "\n  old:    " << p.was << "\n  strict: " << p.strict << "\n";
        std::exit(1);
    }
    if (relaxedToo) {
        const std::string missR = Missed(files[which], p.was, p.relaxed, true);
        if (!missR.empty()) {
            std::cerr << what << ": relaxed mode shows the number `" << missR << "`\n  input:   " << files[which] << "\n  old:     " << p.was << "\n  relaxed: " << p.relaxed << "\n";
            std::exit(1);
        }
    }
    ++checks;
}

// Every string of 0 to maxLen characters over `alphabet`.
void Strings(const std::string& alphabet, size_t maxLen, std::vector<std::string>& out) {
    out.assign(1, "");
    size_t from = 0;
    for (size_t len = 1; len <= maxLen; ++len) {
        const size_t to = out.size();
        for (size_t i = from; i < to; ++i) for (char c : alphabet) out.push_back(out[i] + c);
        from = to;
    }
}

void IdGrid() {
    const char* keys[] = {"geid", "GEID", "accountid", "accountId", "AccountId", "account_id", "Account_Id", "playerid", "playerId",
                          "PlayerId", "playergeid", "playerGEID", "PlayerGEID", "prgeid", "xgeid", "my_geid", "Acct.geid"};
    std::vector<std::string> seps;
    Strings(" =:\"[", 4, seps);   // up to 4: the old rule stops at 3, and a fourth must not matter
    const std::string digits = "7391028465739102";
    const size_t lengths[] = {1, 2, 3, 4, 7, 9, 12, 16};
    const char* before[] = {"", "\"", "{\"", "'", "<", " ", "("};
    const char* after[] = {"", " tail", "\"", "]", ",x", "}"};
    for (const char* key : keys)
        for (const std::string& sep : seps)
            for (size_t len : lengths)
                for (const char* pre : before)
                    for (const char* post : after) {
                        // every length and framing for 0-2 separators; 3 and 4 separators on a sample of lengths, no leading frame
                        if (sep.size() >= 3 && ((len != 3 && len != 9) || *pre)) continue;
                        const std::string line = std::string(pre) + key + sep + digits.substr(0, len) + post;
                        Expect({line}, 0, "", true, "id grid");
                    }
}

void BracketGrid() {
    const char* keys[] = {"Handle", "Nickname", "GEID", "PlayerGEID", "AccountId", "Account", "accountId", "playerGEID"};
    const char* prefixes[] = {"", "x", "My", "_", "rsi"};
    std::vector<std::string> contents = {"Zorbax9", "12345", "ab", "a b c", "x!y", "2000998877", "7654321", "Quillon Vex",
                                         std::string(64, 'k') + "9", std::string(80, 'm'), std::string(81, 'n')};
    for (const char* key : keys)
        for (const char* pre : prefixes)
            for (const std::string& c : contents)
                for (const char* frame : {"%s[%s] done", "x %s[%s]", "[%s[%s]]"}) {
                    char buf[512];
                    std::snprintf(buf, sizeof buf, frame, (std::string(pre) + key).c_str(), c.c_str());
                    Expect({buf}, 0, "", true, "bracket grid");
                }
}

// Handles seen by key in one file hide everywhere in every file, in any case, also inside other words.
void HandleGrid() {
    // Machine words (SYSTEM, LOCAL, NONE, NULL, DEFAULT, Nobody), pieces of the redaction markers (red, use) and key
    // words (Account, Geid, Handle, Nickname, AccountId) are in the list: strict mode hides them everywhere, as the old
    // redactor did, and leaves the markers themselves alone.
    const char* names[] = {"Zorbax9", "Zed", "Ab_7", "xX-Pilot-Xx", "Vexa_Quill77", "a1b", "12345", "1234567", "Admin", "Character",
                           "true", "Bob", "Quillon", "Nim", "Handle1", "Mara-Q", "Star", "Citizen", "Server", "Game",
                           "SYSTEM", "LOCAL", "NONE", "NULL", "DEFAULT", "Nobody", "red", "use", "user", "redacted",
                           "Account", "Geid", "Handle", "Nickname", "AccountId",
                           "0xGamer", "0xBEEF", "0x7ff9", "Ted", "Pro", "Act", "Use", "File", "Eda", "Ser", "Fil"};
    const char* shapes[] = {"Handle[%s]", "Nickname[%s]", "nickname=\"%s\"", "handle=\"%s\"", "rsiHandle[%s]", "myNickname[%s]",
                            "xnickname=\"%s\"", "[Notice] Handle[%s] joined"};
    for (const char* name : names)
        for (const char* shape : shapes) {
            char key[256];
            std::snprintf(key, sizeof key, shape, name);
            const std::string n = name;
            const std::vector<std::string> files = {std::string("2026 start ") + key + " end\n",
                                                   "hello " + n + " and x" + n + "x and " + n + "_tail and " + Low(n) + " " + std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(n[0])))) + Low(n).substr(1) + "\n"};
            Expect(files, 0, "", false, "handle grid, own file");
            Expect(files, 1, "", false, "handle grid, other file");
        }
    // a handle taught by a long, noisy file
    std::string noisy;
    for (int k = 0; k < 300; ++k) noisy += "nickname=\"Npc" + std::to_string(1000 + k) + "\"\n";
    Expect({noisy + "Handle[Zorbax9]\n", "Zorbax9 and Npc1000 and Npc1299\n"}, 1, "", false, "handle grid, noisy file");
    // more than the old 1024 cap, in one file and across files: the handle of a later file must still be hidden
    std::string flood;
    for (int k = 0; k < 3000; ++k) flood += "nickname=\"Npc" + std::to_string(10000 + k) + "\"\n";
    Expect({flood, "[Notice] Handle[Realhandle9] joined\n", "hello Realhandle9 and Npc10000 and Npc12999\n"}, 2, "", false, "handle grid, 3000 values then a handle");
    Expect({flood + "Handle[Realhandle9]\n", "hello Realhandle9 and Npc10000 and Npc12999\n"}, 1, "", false, "handle grid, 3000 values in the same file");
    std::string statusFlood;   // 1200 login-status lines first: they must not push Handle[..] out either
    for (int k = 0; k < 1200; ++k)
        statusFlood += "Character: geid " + std::to_string(2000000000 + k) + " - accountId 5544332211 - name Vex" + std::to_string(1000 + k) + " - state X\n";
    Expect({statusFlood, "Handle[Zorbax9] joined\n", "chat Zorbax9 hi\n"}, 2, "", false, "handle grid, login-status flood");
}

// A handle from a real login line is hidden in relaxed mode too, even when it is a marker piece (Ted, Red), starts with 0x,
// or is a machine word, and the markers themselves stay intact.
void RelaxedLoginHandles() {
    for (const char* name : {"Ted", "Red", "Pro", "Act", "Use", "File", "0xGamer", "0xBEEF", "Admin", "Zorbax9"}) {
        const std::string n = name;
        Identities id;
        Learn(id, "User Login Success - Handle[" + n + "] Account[7654321]\n");
        const std::string out = RedactWith("chat " + n + " says hi, the " + Low(n) + " went away\nmail a@example.com\n", id);
        ++grid_cases;
        std::string stripped = Low(out);
        for (const char* marker : {"<redacted>", "<user>", "%userprofile%"})
            for (size_t at = stripped.find(marker); at != std::string::npos; at = stripped.find(marker, at)) stripped.erase(at, std::strlen(marker));
        const std::string ln = Low(n);
        for (size_t at = stripped.find(ln); at != std::string::npos; at = stripped.find(ln, at + 1)) {
            const bool before = at > 0 && std::isalnum(static_cast<unsigned char>(stripped[at - 1]));
            const bool after = at + ln.size() < stripped.size() && std::isalnum(static_cast<unsigned char>(stripped[at + ln.size()]));
            if (!before && !after) { std::cerr << "relaxed login handle `" << n << "` shown: " << out << "\n"; std::exit(1); }
        }
        CHECK(out.find("a@example.com") == std::string::npos);
        CHECK(out.find("<redacted>") != std::string::npos);
    }
}

void RealisticLines() {
    const std::string lines =
        "<2026-03-01T12:00:00.000Z> [Notice] <Expect Incoming Connection> session=ab12 node_id=cd34 nickname=\"Zorbax9\" playerGEID=2000998877 [Team_Network]\n"
        "<2026-03-01T12:00:01.000Z> [Notice] <AccountLoginCharacterStatus_Character> Character: geid 2000998877 - accountId 5544332211 - name Quillon_Vex - state STATE_CURRENT\n"
        "User Login Success - Handle[Zorbax9], Nickname[Quillon Vex], Account[7654321]\n"
        "<CActor::Kill> CActor::Kill: 'Zorbax9' [2000998877] in zone 'x' killed by 'Other_Z' [2000112233]\n"
        "{\"geid\":2000998877,\"accountId\":\"5544332211\",\"playerid\": 77}\n"
        "SetLocalPlayerId 2000998877 playerGEID[2000998877] done\n"
        "account_id=5544332211 GEID: 2000998877 PlayerId \"123456\"\n"
        "rsiHandle[Zorbax9] firstGEID[2000998877] geid\"\"1234 geid123 accountId \"5544332211\" playerid=[300]\n"
        "chat Zorbax9: hi quillon_vex, welcome zorbax9 and Other_Z\n"
        "data dir C:\\Users\\Alex\\AppData\\Local\\x and /home/Alex/y and D:\\Users\\Alexander\\z and \\\\Users\\\\Alex\\\\q\n";
    std::vector<std::string> files;
    size_t pos = 0;
    while (pos < lines.size()) {   // every line alone, then the whole text, as one file
        const size_t nl = lines.find('\n', pos);
        files.push_back(lines.substr(pos, nl - pos + 1));
        pos = nl + 1;
    }
    const std::vector<std::string> all = files;
    for (size_t i = 0; i < all.size(); ++i) Expect(all, i, "Alex", true, "realistic line");
    Expect({lines, lines}, 0, "Alex", true, "realistic file");
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();
    IdGrid();
    BracketGrid();
    HandleGrid();
    RelaxedLoginHandles();
    RealisticLines();
    // the harness itself: the old redactor really hides what these cases say it hides
    {
        const Pair p = Both({"geid 2000998877 Handle[Zorbax9] zorbax9"}, 0, "");
        CHECK(p.was.find("2000998877") == std::string::npos && Low(p.was).find("zorbax9") == std::string::npos);
        CHECK(p.strict.find("2000998877") == std::string::npos && Low(p.strict).find("zorbax9") == std::string::npos);
        CHECK(p.relaxed.find("2000998877") == std::string::npos);
        CHECK(!Missed("geid 2000998877", "geid 2000998877", "geid 2000998877", false).empty() == false);   // old kept it: nothing to hold the new one to
        CHECK(Missed("geid 2000998877", "geid <id>", "geid 2000998877", false) == "2000998877");
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    CHECK(secs < 60.0);
    std::cout << "launcher redact diff: " << grid_cases << " cases, " << checks << " checks passed in " << secs << " s\n";
}
