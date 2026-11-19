// SPDX-License-Identifier: GPL-3.0-only
// Portable checks for the read-only MCP server (launcher/mcp_server.h) over a fake platform: no files, no sockets.
#include "../launcher/mcp_server.h"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>

using namespace sco::launcher::mcp;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static std::string g_log, g_game_path = "C:\\Games\\StarCitizen\\LIVE\\Bin64";
static std::map<std::string, std::string> g_heads;   // the whole file scanLog streams per log (for login lines)
static size_t g_scan_chunk = 4096;                   // the chunk size scanLog delivers; small values test lines cut across chunks
static std::vector<std::string> g_scanned_logs, g_read_logs;   // which logs the server asked for
static bool g_log_older = false, g_log_exists = true;
static std::string g_log_error;
static std::string g_ini = "# comment\r\n\r\ngame = C:\\Users\\Alex\\Games\\SC\r\nchannel = LIVE\r\n[extra]\r\nkey=v\r\n; also a comment\r\nno equals here\r\n";

static Platform MakePlatform() {
    Platform p;
    p.version = "2.4.1-test";
    p.profileRoots = {"C:\\Users\\Alex"};
    p.accountNames = {"Alex"};
    p.status = [] {
        StatusInfo s;
        s.launcherVersion = "2.4.1-test";
        s.gameFolderFound = true;
        s.gameFolder = "C:\\Users\\Alex\\Games\\SC\\LIVE\\Bin64";
        s.gameBuild = "StarCitizen.exe 123 bytes";
        s.modInstalled = true;
        s.eac = "disabled";
        s.networkBlocked = true;
        s.eacRename = true;
        s.pluginsEnabled = 1;
        s.pluginsTotal = 2;
        s.pcChangesLeft = "hosts=added at c:\\users\\ALEX\\x";
        s.gameRunning = true;
        return s;
    };
    p.plugins = [] {
        std::vector<PluginFolder> v;
        v.push_back({"zeta", "id = zeta\nname = Zeta\nversion = 1.0\n", true});
        v.push_back({"crew", "; c\r\n# d\r\nid = crew\r\nname = Ship crew\r\nversion = 1.0.0\r\nauthor = sc-offline\r\napi = 1.1\r\nkind = native\r\nentry = crew.dll\r\ndescription = Seats = and crew\r\n[settings]\r\nid = other\r\n", false});
        v.push_back({"Bad", "name = Only a name\n", false});
        return v;
    };
    p.gameLog = true;
    p.readLog = [](const std::string& log, size_t max) {
        g_read_logs.push_back(log);
        LogRead r;
        r.path = log == "game" ? g_game_path : "C:\\Users\\Alex\\sc\\data\\" + log + ".log";
        r.exists = g_log_exists;
        r.error = g_log_error;
        r.bytes = g_log.size() > max ? g_log.substr(g_log.size() - max) : g_log;
        r.skippedOlder = g_log_older || g_log.size() > max;
        return r;
    };
    p.scanLog = [](const std::string& log, uint64_t max, const ScanSink& sink) {
        g_scanned_logs.push_back(log);
        LogScan r;
        const auto it = g_heads.find(log);
        r.exists = it != g_heads.end();
        if (!r.exists) return r;
        const std::string& text = it->second;
        r.size = text.size();
        while (r.scanned < text.size() && r.scanned < max) {
            const size_t n = static_cast<size_t>(std::min<uint64_t>({g_scan_chunk, text.size() - r.scanned, max - r.scanned}));
            sink(text.data() + r.scanned, n);
            r.scanned += n;
        }
        return r;
    };
    p.settings = [] {
        IniRead i;
        i.path = "C:\\Users\\Alex\\sc\\sc-offline.ini";
        i.exists = true;
        i.text = g_ini;
        return i;
    };
    return p;
}

static Server MakeServer() { return Server(MakePlatform()); }

static std::string Reply(Server& s, const std::string& line, bool* answered = nullptr) {
    std::string out;
    const bool got = s.HandleLine(line, out);
    if (answered) *answered = got;
    return got ? out : "";
}

static Value Json(const std::string& text) {
    const ParseResult r = Parse(text);
    if (!r.ok) { std::cerr << "bad JSON: " << text.substr(0, 200) << " (" << r.error << ")\n"; std::exit(1); }
    return r.value;
}

static const Value& Get(const Value& v, const char* key) {
    static const Value none;
    const Value* f = v.Find(key);
    return f ? *f : none;
}

static std::string Quote(const std::string& s) { return Serialize(Value::Str(s)); }

static Value Call(Server& s, const std::string& tool, const std::string& args = "{}", int id = 1) {
    return Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"tools/call\",\"params\":{\"name\":" +
                             Quote(tool) + ",\"arguments\":" + args + "}}"));
}

static int ErrorCode(const Value& reply) {
    int64_t n = 0;
    return Get(reply, "error").IsObject() && Get(Get(reply, "error"), "code").AsInteger(n) ? static_cast<int>(n) : 0;
}

static const Value& ToolSchema(const Value& list, const std::string& name, const char* which) {
    for (const Value& t : Get(Get(list, "result"), "tools").Items())
        if (Get(t, "name").AsString() == name) return Get(t, which);
    static const Value none;
    return none;
}

static void ParserChecks() {
    // round trips
    for (const char* text : {"null", "true", "false", "0", "-12", "3.5", "1e3", "-0.5E-2", "\"\"", "\"a b\"", "[]", "{}",
                             "[1,[2,[3]]]", "{\"a\":{\"b\":[true,null,\"x\"]},\"c\":\"\\\\ \\\" \\n \\t\"}"}) {
        const ParseResult r = Parse(text);
        CHECK(r.ok);
        CHECK(Parse(Serialize(r.value)).ok);
        CHECK(Serialize(Parse(Serialize(r.value)).value) == Serialize(r.value));
    }
    CHECK(Serialize(Json(" { \"a\" : [ 1 , 2 ] } ")) == "{\"a\":[1,2]}");
    CHECK(Serialize(Json("1.50")) == "1.50");   // numbers keep their token (an id is echoed exactly)
    CHECK(Serialize(Json("1E400")) == "1E400");
    // strings: escapes, surrogate pairs, UTF-8 kept, control characters escaped
    CHECK(Json("\"\\u00e9\\u20ac\\ud83d\\ude00\\/\\b\\f\"").AsString() == "\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80/\b\f");
    CHECK(Serialize(Value::Str("\xC3\xA9 \xF0\x9F\x98\x80")) == "\"\xC3\xA9 \xF0\x9F\x98\x80\"");
    CHECK(Serialize(Value::Str(std::string("a\x01" "b\0c\x1f\x7f", 7))) == "\"a\\u0001b\\u0000c\\u001f\x7f\"");
    CHECK(Serialize(Value::Str("a\"b\\c\nd\re\tf\bg\fh")) == "\"a\\\"b\\\\c\\nd\\re\\tf\\bg\\fh\"");
    CHECK(Serialize(Value::Str("bad\xFF" "x\xC0\xAF" "\xED\xA0\x80")) == "\"bad\xEF\xBF\xBDx\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\"");
    CHECK(Json("{\"a\":1,\"a\":2}").Find("a")->AsDouble() == 2 && Json("{\"a\":1,\"a\":2}").Members().size() == 1);
    // malformed
    for (const char* text : {"", " ", "{", "[", "[1,]", "{\"a\":}", "{\"a\" 1}", "{a:1}", "{\"a\":1,}", "\"abc", "\"a\\x\"",
                             "\"\\u12\"", "\"\\u12G4\"", "\"\\ud800\"", "\"\\ud800x\"", "\"\\ud800\\u0041\"", "\"\\udc00\"",
                             "\"a\nb\"", "\"a\tb\"", "{} x", "1 2", "[1] ]", "01", "-", "1.", ".5", "1e", "+1", "tru", "nul",
                             "'a'", "NaN", "Infinity", "\"\xFF\"", "\"\xC3\"", "[1 2]", "{\"a\":1 \"b\":2}"}) {
        CHECK(!Parse(text).ok);
        CHECK(!Parse(text).error.empty());
    }
    // depth cap: 64 nested levels are fine, 65 are not
    CHECK(Parse(std::string(64, '[') + std::string(64, ']')).ok);
    CHECK(!Parse(std::string(65, '[') + std::string(65, ']')).ok);
    CHECK(Parse(std::string(63, '[') + "{\"a\":1}" + std::string(63, ']')).ok);
    CHECK(!Parse(std::string(64, '[') + "{\"a\":1}" + std::string(64, ']')).ok);
    CHECK(!Parse(std::string(100000, '[')).ok);
    // integers
    int64_t n = 0;
    CHECK(Json("100").AsInteger(n) && n == 100);
    CHECK(Json("1e2").AsInteger(n) && n == 100);
    CHECK(Json("100.0").AsInteger(n) && n == 100);
    CHECK(!Json("1.5").AsInteger(n));
    CHECK(!Json("\"1\"").AsInteger(n));
    CHECK(!Json("1e400").AsInteger(n));
    // UTF-8 helpers
    CHECK(SanitizeUtf8("ok \xC3\xA9") == "ok \xC3\xA9");
    CHECK(SanitizeUtf8("\xE2\x82") == "\xEF\xBF\xBD\xEF\xBF\xBD");
    CHECK(ClipUtf8("a\xC3\xA9", 2) == "a");
    CHECK(ClipUtf8("a\xC3\xA9", 3) == "a\xC3\xA9");
}

// Raw-string arguments cannot go through CHECK (its stringizing turns the backslashes into escapes).
#define REDACT_IS(in, roots, want) \
    do { ++checks; if (Redact(in, roots) != (want)) { \
        std::cerr << "FAIL " << __LINE__ << ": Redact(" << (in) << ") gave " << Redact(in, roots) << "\n"; std::exit(1); } } while (0)

static void RedactChecks() {
    const std::vector<std::string> roots = {"C:\\Users\\Alex"};
    CHECK(Redact("C:\\Users\\Alex\\AppData\\x", roots) == "%USERPROFILE%\\AppData\\x");
    CHECK(Redact("c:\\users\\ALEX\\x and C:\\USERS\\alex", roots) == "%USERPROFILE%\\x and %USERPROFILE%");
    CHECK(Redact("C:\\Users\\Alexander\\x", roots) == "C:\\Users\\<user>\\x");   // another account, not the profile
    CHECK(Redact("C:\\Users\\Bob\\x", roots) == "C:\\Users\\<user>\\x");
    CHECK(Redact("D:/Users/Bob/x", roots) == "D:/Users/<user>/x");
    CHECK(Redact("d:\\users\\Bob Smith\\x", roots) == "d:\\users\\<user>\\x");
    // no separator after the name: it ends at a character a path cannot hold in a log, so a spaced name is covered
    CHECK(Redact("opened C:\\Users\\Bob and closed", roots) == "opened C:\\Users\\<user>");
    CHECK(Redact("\"C:\\Users\\Bob\" end", roots) == "\"C:\\Users\\<user>\" end");
    CHECK(Redact("C:\\Users\\Bob", roots) == "C:\\Users\\<user>");
    CHECK(Redact("C:\\Users\\", roots) == "C:\\Users\\");
    CHECK(Redact("C:\\Users\\<user>\\x", roots) == "C:\\Users\\<user>\\x");   // idempotent
    CHECK(Redact(Redact("C:\\Users\\Bob\\x", roots), roots) == "C:\\Users\\<user>\\x");
    CHECK(Redact("C:\\Program Files\\x", roots) == "C:\\Program Files\\x");
    CHECK(Redact("C:\\Users\\Alex", {"C:\\Users\\Alex\\"}) == "%USERPROFILE%");   // a trailing slash on the root
    CHECK(Redact("C:\\Users\\Alex\\x", {}) == "C:\\Users\\<user>\\x");
    CHECK(Redact("two C:\\Users\\A\\x C:\\Users\\B\\y", {}) == "two C:\\Users\\<user>\\x C:\\Users\\<user>\\y");

    // a run of separators is one separator (escaped or JSON-quoted logs); the run is kept as written
    REDACT_IS(R"(C:\\Users\\bobsecret\\AppData\\x)", {}, R"(C:\\Users\\<user>\\AppData\\x)");
    REDACT_IS(R"(C:\\Users\\bobsecret\\AppData\\x)", roots, R"(C:\\Users\\<user>\\AppData\\x)");
    REDACT_IS(R"(C:\\Users\\Alex\\AppData\\x)", roots, R"(%USERPROFILE%\\AppData\\x)");
    REDACT_IS("C:/Users//bobsecret//x", {}, "C:/Users//<user>//x");

    // a spaced name with no separator after it: up to the next character a log path cannot hold
    REDACT_IS(R"(C:\Users\Jane Doe)", {}, R"(C:\Users\<user>)");
    REDACT_IS(R"(see "C:\Users\Jane Doe" now)", {}, R"(see "C:\Users\<user>" now)");
    REDACT_IS(R"(see 'C:\Users\Jane Doe' now)", {}, R"(see 'C:\Users\<user>' now)");
    REDACT_IS(R"(f(C:\Users\Jane Doe) g)", {}, R"(f(C:\Users\<user>) g)");
    REDACT_IS(R"([C:\Users\Jane Doe] {C:\Users\Jo Mo})", {}, R"([C:\Users\<user>] {C:\Users\<user>})");
    REDACT_IS(R"(a C:\Users\Jane Doe, b C:\Users\Jo Mo; c)", {}, R"(a C:\Users\<user>, b C:\Users\<user>; c)");
    REDACT_IS("C:\\Users\\Jane Doe\nnext line", {}, "C:\\Users\\<user>\nnext line");
    REDACT_IS(R"(<C:\Users\Jane Doe>)", {}, R"(<C:\Users\<user>>)");
    REDACT_IS(R"(C:\Users\Jane Doe|x)", {}, R"(C:\Users\<user>|x)");

    // the profile root: a boundary after it is a separator, the end, or anything that cannot continue the name
    const std::vector<std::string> odd = {R"(Z:\Profiles\bobsecret)"};
    REDACT_IS(R"(Z:\Profiles\bobsecret.)", odd, "%USERPROFILE%.");
    REDACT_IS(R"(Z:\Profiles\bobsecret.log)", odd, "%USERPROFILE%.log");
    REDACT_IS(R"(Z:\Profiles\bobsecret-)", odd, "%USERPROFILE%-");
    REDACT_IS(R"(Z:\Profiles\bobsecret_)", odd, "%USERPROFILE%_");
    REDACT_IS(R"(Z:\Profiles\bobsecret, x)", odd, "%USERPROFILE%, x");
    REDACT_IS(R"(Z:\Profiles\bobsecret)", odd, "%USERPROFILE%");
    REDACT_IS(R"(Z:\Profiles\bobsecret\x)", odd, R"(%USERPROFILE%\x)");
    REDACT_IS(R"(Z:\Profiles\bobsecret2\x)", odd, R"(Z:\Profiles\bobsecret2\x)");     // another account
    REDACT_IS(R"(Z:\Profiles\bobsecret-two\x)", odd, R"(Z:\Profiles\bobsecret-two\x)");
    REDACT_IS(R"(Z:\Profiles\bobsecret_two\x)", odd, R"(Z:\Profiles\bobsecret_two\x)");
    REDACT_IS("Z:\\Profiles\\bobsecret\xC3\xA9", odd, "Z:\\Profiles\\bobsecret\xC3\xA9");

    // a Users component after any separator, not only after a drive letter
    REDACT_IS("/c/Users/erin/x", {}, "/c/Users/<user>/x");
    REDACT_IS(R"(\Device\HarddiskVolume3\Users\frank\x)", {}, R"(\Device\HarddiskVolume3\Users\<user>\x)");
    REDACT_IS(R"(\\?\C:\Users\gina\x)", {}, R"(\\?\C:\Users\<user>\x)");
    REDACT_IS(R"(\\host\c$\Users\hank\x)", {}, R"(\\host\c$\Users\<user>\x)");
    REDACT_IS("/mnt/c/users/Ivy Lee/x", {}, "/mnt/c/users/<user>/x");
    REDACT_IS("/opt/MyUsers/x", {}, "/opt/MyUsers/x");   // only a component named Users
    REDACT_IS("/home/ab/x", {}, "/home/<user>/x");   // a home folder, whatever the length of the name
    REDACT_IS("/usr/Users", {}, "/usr/Users");
    REDACT_IS("/usr/Users/", {}, "/usr/Users/");
    for (const char* secret : {"erin", "frank", "gina", "hank"})
        CHECK(Redact(std::string("x /c/Users/") + secret + "/y \\\\?\\C:\\Users\\" + secret, {}).find(secret) == std::string::npos);
}

static void LimitChecks() {
    Server s = MakeServer();
    const std::string ping = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}";
    const auto object = [](size_t keys) {
        std::string line = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"";
        for (size_t i = 0; i + 3 < keys; ++i) line += ",\"k" + std::to_string(i) + "\":0";
        return line + "}";
    };
    // an object at the member cap parses; one more is refused, and the session goes on
    CHECK(Parse(object(kMaxMembers)).ok);
    const ParseResult over = Parse(object(kMaxMembers + 1));
    CHECK(!over.ok && over.error.find("too many members") != std::string::npos);
    // ~80k distinct keys in under 1 MiB: refused quickly (it used to take seconds), the next request still answers
    {
        const std::string big = object(80000);
        CHECK(big.size() < kMaxLine);
        const auto t0 = std::chrono::steady_clock::now();
        const Value r = Json(Reply(s, big));
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ErrorCode(r) == -32700 && Get(Get(r, "error"), "message").AsString().find("too many members") != std::string::npos);
        CHECK(ms < 1000);
        CHECK(Get(Json(Reply(s, ping)), "result").IsObject());
    }
    // too many values in one message, however they are spread
    {
        std::string arr = "[";
        for (size_t i = 0; i + 1 < kMaxValues; ++i) arr += i ? ",1" : "1";
        CHECK(Parse(arr + "]").ok);                       // exactly kMaxValues values
        const ParseResult many = Parse(arr + ",1]");
        CHECK(!many.ok && many.error.find("too many values") != std::string::npos);
        const Value r = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":" + arr + ",1]}"));
        CHECK(ErrorCode(r) == -32700);
        CHECK(Get(Json(Reply(s, ping)), "result").IsObject());
    }
    // a duplicate key still replaces the earlier one
    CHECK(Get(Json("{\"a\":1,\"a\":2}"), "a").AsString() == "2");
}

static void NegotiationChecks() {
    Server s = MakeServer();
    for (const char* v : {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"}) {
        const Value r = Json(Reply(s, std::string("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"") + v + "\"}}"));
        CHECK(Get(Get(r, "result"), "protocolVersion").AsString() == v);
    }
    Value r = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2099-01-01\",\"capabilities\":{},\"clientInfo\":{\"name\":\"t\",\"version\":\"1\"}}}"));
    CHECK(Get(r, "id").AsString() == "abc");
    CHECK(Get(r, "jsonrpc").AsString() == "2.0");
    const Value& res = Get(r, "result");
    CHECK(Get(res, "protocolVersion").AsString() == "2025-11-25");
    CHECK(Get(Get(Get(res, "capabilities"), "tools"), "listChanged").IsBool() && !Get(Get(Get(res, "capabilities"), "tools"), "listChanged").AsBool());
    CHECK(Get(res, "capabilities").Members().size() == 1);
    CHECK(Get(Get(res, "serverInfo"), "name").AsString() == "sc-offline");
    CHECK(Get(Get(res, "serverInfo"), "title").AsString() == "sc-offline");
    CHECK(Get(Get(res, "serverInfo"), "version").AsString() == "2.4.1-test");
    const std::string ins = Get(res, "instructions").AsString();
    CHECK(ins.find("read-only") != std::string::npos && ins.find("%USERPROFILE%") != std::string::npos);
    CHECK(ins.find("sco-core-mcp") != std::string::npos && ins.find("scuba-engine") != std::string::npos);
    size_t sentences = 0;
    for (size_t i = 0; i < ins.size(); ++i) if (ins[i] == '.' && (i + 1 == ins.size() || ins[i + 1] == ' ')) ++sentences;
    CHECK(sentences >= 3 && sentences <= 5);
    // lenient: no params, and a numeric id is echoed as written
    r = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1.50,\"method\":\"initialize\"}"));
    CHECK(Get(Get(r, "result"), "protocolVersion").AsString() == "2025-11-25");
    CHECK(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1.50,\"method\":\"ping\"}") == "{\"jsonrpc\":\"2.0\",\"id\":1.50,\"result\":{}}");
    CHECK(ErrorCode(Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":5}}"))) == -32602);
    // before initialize everything answers normally
    Server fresh = MakeServer();
    CHECK(Get(Get(Json(Reply(fresh, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}")), "result"), "tools").Items().size() == 4);
}

static void FramingChecks() {
    Server s = MakeServer();
    bool answered = true;
    for (const char* quiet : {"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}",
                              "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":1}}",
                              "{\"jsonrpc\":\"2.0\",\"method\":\"nonsense/unknown\"}",
                              "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"nope\"}}",
                              "{\"jsonrpc\":\"2.0\",\"id\":9,\"result\":{}}", "", "   ", "\r"}) {
        CHECK(Reply(s, quiet, &answered).empty());
        CHECK(!answered);
    }
    CHECK(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}\r") == "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{}}");
    CHECK(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"ping\"}") == "{\"jsonrpc\":\"2.0\",\"id\":null,\"result\":{}}");
    // errors
    Value e = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"resources/list\"}"));
    CHECK(ErrorCode(e) == -32601 && Get(e, "id").AsDouble() == 4);
    CHECK(Get(Get(e, "error"), "message").AsString().find("resources/list") != std::string::npos);
    e = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"notifications/initialized\"}"));   // with an id it is a request
    CHECK(ErrorCode(e) == -32601);
    for (const char* bad : {"{not json", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"} trailing", "\"unterminated", "{\"a\":\"\\q\"}",
                            "{\"a\":\"\\ud800\"}"}) {
        e = Json(Reply(s, bad));
        CHECK(ErrorCode(e) == -32700);
        CHECK(Get(e, "id").IsNull() && e.Find("id") != nullptr);
    }
    e = Json(Reply(s, std::string(65, '[') + std::string(65, ']')));
    CHECK(ErrorCode(e) == -32700);
    e = Json(Reply(s, "[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}]"));
    CHECK(ErrorCode(e) == -32600 && Get(e, "id").IsNull());
    e = Json(Reply(s, "[]"));
    CHECK(ErrorCode(e) == -32600);
    for (const char* bad : {"42", "\"x\"", "null", "{}", "{\"jsonrpc\":\"1.0\",\"id\":1,\"method\":\"ping\"}",
                            "{\"jsonrpc\":\"2.0\",\"id\":1}", "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":5}",
                            "{\"id\":1,\"method\":\"ping\"}", "{\"jsonrpc\":\"2.0\",\"id\":{},\"method\":\"ping\"}",
                            "{\"jsonrpc\":\"2.0\",\"id\":true,\"method\":\"ping\"}"}) {
        CHECK(ErrorCode(Json(Reply(s, bad))) == -32600);
    }
    CHECK(ErrorCode(Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":[]}"))) == -32602);
    // oversize
    const std::string big = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\",\"params\":{\"x\":\"" + std::string(kMaxLine, 'a') + "\"}}";
    e = Json(Reply(s, big));
    CHECK(ErrorCode(e) == -32600 && Get(e, "id").IsNull());
    CHECK(Get(Get(e, "error"), "message").AsString().find("1 MiB") != std::string::npos);

    // the stdio loop: chunked reads, an oversize line answered once and skipped, the rest still served, EOF ends it
    std::string input = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n\n   \n" + big + "\n" +
                        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\r\n"
                        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}\n"
                        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}";   // no final newline
    for (size_t chunk : {static_cast<size_t>(1000), static_cast<size_t>(65536), static_cast<size_t>(7)}) {
        size_t at = 0;
        std::vector<std::string> out;
        Serve(s, [&](char* buf, size_t n) {
            const size_t take = std::min({n, chunk, input.size() - at});
            std::memcpy(buf, input.data() + at, take);
            at += take;
            return take;
        }, [&](const std::string& line) { CHECK(line.find('\n') == std::string::npos); out.push_back(line); return true; });
        CHECK(out.size() == 4);
        CHECK(out[0] == "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}");
        CHECK(ErrorCode(Json(out[1])) == -32600);
        CHECK(out[2] == "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{}}");
        CHECK(out[3] == "{\"jsonrpc\":\"2.0\",\"id\":3,\"result\":{}}");
    }
    // an oversize last line without a newline, and a writer that stops the loop
    std::string tail = "{\"x\":\"" + std::string(kMaxLine + 10, 'b');
    size_t at = 0;
    std::vector<std::string> out;
    Serve(s, [&](char* buf, size_t n) { const size_t take = std::min(n, tail.size() - at); std::memcpy(buf, tail.data() + at, take); at += take; return take; },
          [&](const std::string& line) { out.push_back(line); return true; });
    CHECK(out.size() == 1 && ErrorCode(Json(out[0])) == -32600);
    input = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}\n";
    at = 0; out.clear();
    Serve(s, [&](char* buf, size_t n) { const size_t take = std::min(n, input.size() - at); std::memcpy(buf, input.data() + at, take); at += take; return take; },
          [&](const std::string& line) { out.push_back(line); return false; });
    CHECK(out.size() == 1);
}

// The validator the tool schemas are checked with (a stricter reading than the server's own check: it also
// insists on every required property and the exact set of properties).
static void ListChecks() {
    Server s = MakeServer();
    const Value list = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"));
    const auto& tools = Get(Get(list, "result"), "tools").Items();
    CHECK(tools.size() == 4);
    const char* names[] = {"get_status", "list_plugins", "read_log", "get_settings"};
    for (size_t i = 0; i < 4; ++i) {
        const Value& t = tools[i];
        CHECK(Get(t, "name").AsString() == names[i]);
        CHECK(!Get(t, "title").AsString().empty());
        const std::string d = Get(t, "description").AsString();
        CHECK(d.size() > 80 && d.find("read-only") != std::string::npos);
        size_t sentences = 0;
        for (size_t k = 0; k < d.size(); ++k) if (d[k] == '.' && (k + 1 == d.size() || d[k + 1] == ' ')) ++sentences;
        CHECK(sentences >= 2 && sentences <= 4);
        const Value& in = Get(t, "inputSchema");
        CHECK(Get(in, "type").AsString() == "object");
        CHECK(Get(in, "additionalProperties").IsBool() && !Get(in, "additionalProperties").AsBool());
        for (const auto& p : Get(in, "properties").Members()) {
            CHECK(Get(p.second, "type").IsString() && !Get(p.second, "description").AsString().empty());
        }
        const Value& out = Get(t, "outputSchema");
        CHECK(Get(out, "type").AsString() == "object");
        CHECK(Get(out, "required").IsArray() && !Get(out, "required").Items().empty());
        CHECK(Get(out, "properties").IsObject());
        CHECK(Get(out, "required").Items().size() == Get(out, "properties").Members().size());
        for (const auto& p : Get(out, "properties").Members()) CHECK(Get(p.second, "type").IsString());
        const Value& a = Get(t, "annotations");
        CHECK(Get(a, "readOnlyHint").IsBool() && Get(a, "readOnlyHint").AsBool());
        CHECK(Get(a, "destructiveHint").IsBool() && !Get(a, "destructiveHint").AsBool());
        CHECK(Get(a, "idempotentHint").IsBool() && Get(a, "idempotentHint").AsBool());
        CHECK(Get(a, "openWorldHint").IsBool() && !Get(a, "openWorldHint").AsBool());
    }
    // read_log's input schema
    const Value& props = Get(ToolSchema(list, "read_log", "inputSchema"), "properties");
    CHECK(Get(Get(props, "log"), "enum").Items().size() == 4);
    CHECK(Get(ToolSchema(list, "read_log", "inputSchema"), "required").Items().size() == 1);
    int64_t n = 0;
    CHECK(Get(Get(props, "lines"), "minimum").AsInteger(n) && n == 1);
    CHECK(Get(Get(props, "lines"), "maximum").AsInteger(n) && n == 500);
    CHECK(Get(Get(props, "lines"), "default").AsInteger(n) && n == 100);
    CHECK(Get(Get(props, "contains"), "minLength").AsInteger(n) && n == 1);
    CHECK(Get(Get(props, "contains"), "maxLength").AsInteger(n) && n == 200);
    // no tool takes a command or action: the surface is exactly these four
    for (const char* extra : {"start_game", "run_command", "set_setting", "install"})
        CHECK(ErrorCode(Call(s, extra)) == -32602);
}

// Does `v` match `schema` exactly: every required property present, no other, types and bounds kept?
static bool Matches(const Value& schema, const Value& v) {
    if (CheckSchema(schema, v, "x") != "") return false;
    if (Get(schema, "type").AsString() == "object") {
        for (const Value& r : Get(schema, "required").Items()) if (!v.Find(r.AsString())) return false;
        for (const auto& m : v.Members()) if (!Get(schema, "properties").Find(m.first)) return false;
        for (const auto& m : v.Members()) if (!Matches(*Get(schema, "properties").Find(m.first), m.second)) return false;
    }
    if (Get(schema, "type").AsString() == "array")
        for (const Value& item : v.Items()) if (!Matches(Get(schema, "items"), item)) return false;
    return true;
}

static const Value& Structured(const Value& reply) { return Get(Get(reply, "result"), "structuredContent"); }

// A successful call: structuredContent matches outputSchema; content[0].text is the same object as JSON.
static Value Ok(Server& s, const Value& reply, const std::string& tool) {
    static Value list;
    list = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"));
    const Value& res = Get(reply, "result");
    CHECK(res.IsObject());
    CHECK(Get(res, "isError").IsBool() && !Get(res, "isError").AsBool());
    CHECK(Matches(ToolSchema(list, tool, "outputSchema"), Structured(reply)));
    const Value& content = Get(res, "content");
    CHECK(content.Items().size() == 1 && Get(content.Items()[0], "type").AsString() == "text");
    CHECK(Get(content.Items()[0], "text").AsString() == Serialize(Structured(reply)));
    return Structured(reply);   // a copy: callers pass a temporary reply
}

static std::vector<std::string> Lines(const Value& out) {
    std::vector<std::string> v;
    for (const Value& l : Get(out, "lines").Items()) v.push_back(l.AsString());
    return v;
}

static void ToolChecks() {
    Server s = MakeServer();
    // get_status
    {
        const Value reply = Call(s, "get_status");
        const Value& o = Ok(s, reply, "get_status");
        CHECK(Get(o, "launcher_version").AsString() == "2.4.1-test");
        CHECK(Get(o, "game_folder_found").AsBool());
        CHECK(Get(o, "game_folder").AsString() == "%USERPROFILE%\\Games\\SC\\LIVE\\Bin64");
        CHECK(Get(o, "game_build").AsString() == "StarCitizen.exe 123 bytes");
        CHECK(Get(o, "mod_installed").AsBool());
        CHECK(Get(o, "eac").AsString() == "disabled");
        CHECK(Get(o, "network_blocked").AsBool() && Get(o, "eac_rename").AsBool() && Get(o, "plugins_setting_on").AsBool());
        int64_t n = 0;
        CHECK(Get(o, "plugins_enabled").AsInteger(n) && n == 1 && Get(o, "plugins_total").AsInteger(n) && n == 2);
        CHECK(Get(o, "pc_changes_left").AsString() == "hosts=added at c:\\users\\<user>\\x" ||
              Get(o, "pc_changes_left").AsString() == "hosts=added at %USERPROFILE%\\x");
        CHECK(!Get(o, "wine").AsBool() && Get(o, "game_running").AsBool());
        // extra arguments are refused
        CHECK(ErrorCode(Call(s, "get_status", "{\"x\":1}")) == -32602);
        CHECK(ErrorCode(Call(s, "get_status", "[]")) == -32602);
        CHECK(ErrorCode(Call(s, "get_status", "null")) == -32602);
    }
    // get_status with no game folder and an unexpected eac word
    {
        Platform p = MakePlatform();
        p.status = [] { StatusInfo i; i.gameFolder = "C:\\Users\\Alex\\leftover"; i.eac = "weird"; return i; };
        Server t(p);
        const Value& o = Ok(t, Call(t, "get_status"), "get_status");
        CHECK(!Get(o, "game_folder_found").AsBool() && Get(o, "game_folder").AsString().empty() && Get(o, "game_build").AsString().empty());
        CHECK(Get(o, "eac").AsString() == "not_installed" && Get(o, "pc_changes_left").AsString().empty());
    }
    // list_plugins
    {
        const Value& o = Ok(s, Call(s, "list_plugins"), "list_plugins");
        const auto& ps = Get(o, "plugins").Items();
        int64_t n = 0;
        CHECK(ps.size() == 3 && Get(o, "count").AsInteger(n) && n == 3 && !Get(o, "truncated").AsBool());
        CHECK(Get(ps[0], "folder").AsString() == "Bad" && Get(ps[1], "folder").AsString() == "crew" && Get(ps[2], "folder").AsString() == "zeta");
        CHECK(Get(ps[1], "enabled").AsBool() && !Get(ps[2], "enabled").AsBool());
        CHECK(Get(ps[1], "id").AsString() == "crew" && Get(ps[1], "name").AsString() == "Ship crew" && Get(ps[1], "version").AsString() == "1.0.0");
        CHECK(Get(ps[1], "author").AsString() == "sc-offline" && Get(ps[1], "api").AsString() == "1.1" && Get(ps[1], "kind").AsString() == "native");
        CHECK(Get(ps[1], "entry").AsString() == "crew.dll" && Get(ps[1], "description").AsString() == "Seats = and crew");
        CHECK(Get(ps[1], "warnings").Items().empty());   // [settings] keys are not plugin fields
        CHECK(Get(ps[2], "author").AsString().empty() && Get(ps[2], "entry").AsString().empty());
        CHECK(Get(ps[0], "warnings").Items().size() == 2);   // no id, no version (name is there)
        CHECK(Get(ps[0], "warnings").Items()[0].AsString() == "plugin.ini has no id");
        CHECK(Get(ps[0], "warnings").Items()[1].AsString() == "plugin.ini has no version");
        CHECK(ErrorCode(Call(s, "list_plugins", "{\"folder\":\"crew\"}")) == -32602);
    }
    {
        Platform p = MakePlatform();
        p.plugins = [] {
            std::vector<PluginFolder> v;
            for (int i = 0; i < 300; ++i) v.push_back({"p" + std::to_string(1000 + i), "id = p" + std::to_string(1000 + i) + "\nname = n\nversion = 1\n", false});
            v.push_back({"long", "id = other\nname = " + std::string(400, 'n') + "\nversion=1\ndescription = C:\\Users\\Bob\\x\n", false});
            v.push_back({"mismatch", "id = else\nname = n\nversion = 1\n", false});
            v.push_back({"empty", "", false});
            return v;
        };
        Server t(p);
        const Value& o = Ok(t, Call(t, "list_plugins"), "list_plugins");
        CHECK(Get(o, "plugins").Items().size() == 256 && Get(o, "truncated").AsBool());
        Platform q = MakePlatform();
        q.plugins = [] {
            std::vector<PluginFolder> v;
            v.push_back({"long", "id = other\nname = " + std::string(400, 'n') + "\nversion=1\ndescription = C:\\Users\\Bob\\x\n", false});
            v.push_back({"mismatch", "id = else\nname = n\nversion = 1\n", false});
            v.push_back({"empty", "", false});
            return v;
        };
        Server u(q);
        const Value& r = Ok(u, Call(u, "list_plugins"), "list_plugins");
        const auto& ps = Get(r, "plugins").Items();
        CHECK(ps.size() == 3 && Get(ps[0], "folder").AsString() == "empty" && Get(ps[1], "folder").AsString() == "long");
        CHECK(Get(ps[0], "warnings").Items().size() == 3);
        CHECK(Get(ps[1], "name").AsString().size() == 256);
        CHECK(Get(ps[1], "description").AsString() == "C:\\Users\\<user>\\x");
        bool shortened = false, mismatch = false;
        for (const Value& w : Get(ps[1], "warnings").Items()) {
            shortened = shortened || w.AsString().find("name is longer than 256") != std::string::npos;
            mismatch = mismatch || w.AsString().find("does not match") != std::string::npos;
        }
        CHECK(shortened && mismatch);
        CHECK(Get(ps[2], "warnings").Items().size() == 1 && Get(ps[2], "warnings").Items()[0].AsString().find("'else'") != std::string::npos);
    }
    // get_settings
    {
        const Value& o = Ok(s, Call(s, "get_settings"), "get_settings");
        CHECK(Get(o, "path").AsString() == "%USERPROFILE%\\sc\\sc-offline.ini" && Get(o, "exists").AsBool());
        const auto& st = Get(o, "settings").Items();
        CHECK(st.size() == 3);
        CHECK(Get(st[0], "section").AsString().empty() && Get(st[0], "key").AsString() == "game" && Get(st[0], "value").AsString() == "%USERPROFILE%\\Games\\SC");
        CHECK(Get(st[1], "key").AsString() == "channel" && Get(st[1], "value").AsString() == "LIVE");
        CHECK(Get(st[2], "section").AsString() == "extra" && Get(st[2], "key").AsString() == "key" && Get(st[2], "value").AsString() == "v");
        CHECK(ErrorCode(Call(s, "get_settings", "{\"x\":true}")) == -32602);
    }
    {
        Platform p = MakePlatform();
        p.settings = [] { IniRead i; i.path = "C:\\x\\sc-offline.ini"; return i; };
        Server t(p);
        const Value& o = Ok(t, Call(t, "get_settings"), "get_settings");
        CHECK(!Get(o, "exists").AsBool() && Get(o, "settings").Items().empty());
        p.settings = [] { IniRead i; i.exists = true; i.error = "access denied"; return i; };
        Server u(p);
        const Value reply = Call(u, "get_settings");
        CHECK(Get(Get(reply, "result"), "isError").AsBool() && !Get(reply, "error").IsObject());
        CHECK(Get(Get(Get(reply, "result"), "content").Items()[0], "text").AsString().find("access denied") != std::string::npos);
        CHECK(Get(Get(reply, "result"), "structuredContent").IsNull());
        p.settings = [] { IniRead i; i.exists = true; i.text = "\xEF\xBB\xBF" "a = 1\n"; for (int k = 0; k < 1200; ++k) i.text += "k" + std::to_string(k) + " = " + std::string(2000, 'v') + "\n"; return i; };
        Server w(p);
        const Value& big = Ok(w, Call(w, "get_settings"), "get_settings");
        CHECK(Get(big, "settings").Items().size() == 1000 && Get(big, "truncated").AsBool());
        CHECK(Get(Get(big, "settings").Items()[0], "key").AsString() == "a");
        CHECK(Get(Get(big, "settings").Items()[1], "value").AsString().size() == 1024);
        // An identity key hides its value even when no log line taught it (made-up values).
        p.settings = [] { IniRead i; i.exists = true; i.text = "nickname = Qwertyuiop\ngeid = 2000998877\naccount_id = 5544332211\nplugins = on\n"; return i; };
        Server k(p);
        const Value& ids = Ok(k, Call(k, "get_settings"), "get_settings");
        const std::string shown = Serialize(ids);
        CHECK(shown.find("Qwertyuiop") == std::string::npos && shown.find("2000998877") == std::string::npos && shown.find("5544332211") == std::string::npos);
        CHECK(Get(Get(ids, "settings").Items()[0], "key").AsString() == "nickname" && Get(Get(ids, "settings").Items()[3], "value").AsString() == "on");
    }
}

static std::string Numbered(int from, int to) {
    std::string t;
    for (int i = from; i <= to; ++i) t += "line " + std::to_string(i) + "\n";
    return t;
}

static void LogChecks() {
    Server s = MakeServer();
    g_log_older = false; g_log_exists = true; g_log_error.clear();
    g_log = Numbered(1, 1000);
    // default 100, newest last
    {
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        const auto v = Lines(o);
        int64_t n = 0;
        CHECK(v.size() == 100 && v.front() == "line 901" && v.back() == "line 1000");
        CHECK(Get(o, "log").AsString() == "mod" && Get(o, "exists").AsBool());
        CHECK(Get(o, "path").AsString() == "%USERPROFILE%\\sc\\data\\mod.log");
        CHECK(Get(o, "matched_lines").AsInteger(n) && n == 1000);
        CHECK(Get(o, "truncated").AsBool());
    }
    // 500 is the most; 501 and 0 are refused
    {
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"launcher\",\"lines\":500}"), "read_log");
        CHECK(Lines(o).size() == 500 && Lines(o).front() == "line 501");
        const Value& few = Ok(s, Call(s, "read_log", "{\"log\":\"launcher_previous\",\"lines\":1}"), "read_log");
        CHECK(Lines(few) == std::vector<std::string>{"line 1000"});
        const Value& all = Ok(s, Call(s, "read_log", "{\"log\":\"game\",\"lines\":500}"), "read_log");
        CHECK(Get(all, "path").AsString() == "C:\\Games\\StarCitizen\\LIVE\\Bin64");
        for (const char* bad : {"{\"log\":\"mod\",\"lines\":501}", "{\"log\":\"mod\",\"lines\":0}", "{\"log\":\"mod\",\"lines\":-1}",
                                "{\"log\":\"mod\",\"lines\":1.5}", "{\"log\":\"mod\",\"lines\":\"10\"}", "{\"log\":\"mod\",\"lines\":null}",
                                "{\"log\":\"mod\",\"contains\":\"\"}", "{\"log\":\"mod\",\"contains\":5}", "{}", "{\"lines\":10}",
                                "{\"log\":\"other\"}", "{\"log\":5}", "{\"log\":\"mod\",\"extra\":1}"}) {
            const Value r = Call(s, "read_log", bad);
            CHECK(ErrorCode(r) == -32602);
            CHECK(!Get(Get(r, "error"), "message").AsString().empty());
        }
        CHECK(ErrorCode(Call(s, "read_log", "{\"log\":\"mod\",\"contains\":\"" + std::string(201, 'a') + "\"}")) == -32602);
        CHECK(!Get(Get(Call(s, "read_log", "{\"log\":\"mod\",\"contains\":\"" + std::string(200, 'a') + "\"}"), "result"), "content").IsNull());
        CHECK(Get(Get(Call(s, "read_log", "{\"log\":\"mod\",\"lines\":501}"), "error"), "message").AsString().find("lines") != std::string::npos);
        CHECK(Get(Get(Call(s, "read_log", "{\"log\":\"mod\",\"bogus\":1}"), "error"), "message").AsString().find("bogus") != std::string::npos);
        CHECK(Get(Get(Call(s, "read_log", "{\"log\":\"mod\",\"lines\":1e2}"), "result"), "isError").IsBool());   // 1e2 is the integer 100
        CHECK(Lines(Structured(Call(s, "read_log", "{\"log\":\"mod\",\"lines\":1e2}"))).size() == 100);
    }
    // the filter runs before the last N are taken; ASCII case-insensitive
    {
        g_log = "Alpha one\nbeta two\nALPHA three\nGamma\nalpha four\n";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\",\"contains\":\"aLpHa\",\"lines\":2}"), "read_log");
        int64_t n = 0;
        CHECK(Lines(o) == (std::vector<std::string>{"ALPHA three", "alpha four"}));
        CHECK(Get(o, "matched_lines").AsInteger(n) && n == 3 && Get(o, "truncated").AsBool());
        const Value& none = Ok(s, Call(s, "read_log", "{\"log\":\"mod\",\"contains\":\"zzz\"}"), "read_log");
        CHECK(Lines(none).empty() && Get(none, "matched_lines").AsInteger(n) && n == 0 && !Get(none, "truncated").AsBool());
        const Value& everything = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(Lines(everything).size() == 5 && !Get(everything, "truncated").AsBool());
    }
    // 64 KiB cap: the oldest lines go first
    {
        g_log.clear();
        for (int i = 0; i < 400; ++i) g_log += "L" + std::to_string(1000 + i) + std::string(997, 'x') + "\n";   // 1002 bytes each
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\",\"lines\":500}"), "read_log");
        size_t total = 0;
        for (const std::string& l : Lines(o)) total += l.size();
        CHECK(total <= 65536 && total > 65536 - 2000);
        CHECK(Lines(o).size() < 400 && Lines(o).back().substr(0, 5) == "L1399");
        int64_t n = 0;
        CHECK(Get(o, "matched_lines").AsInteger(n) && n == 400 && Get(o, "truncated").AsBool());
        // one line longer than the cap is cut, not dropped
        g_log = "short\n" + std::string(200000, 'y') + "\n";
        const Value& one = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(Lines(one).size() == 1 && Lines(one)[0].size() < 65536 && Get(one, "truncated").AsBool());
    }
    // CRLF stripped, blank lines kept, an unterminated last line counts
    {
        g_log = "one\r\n\r\ntwo\r\r\nthree";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(Lines(o) == (std::vector<std::string>{"one", "", "two", "three"}));
        CHECK(!Get(o, "truncated").AsBool());
    }
    // invalid UTF-8 becomes U+FFFD: the reply parses and the text is valid
    {
        g_log = "ok \xC3\xA9\nbad \xFF\xFE here\nlone \xED\xA0\x80 and cut \xE2\x82\n";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        const auto v = Lines(o);
        CHECK(v.size() == 3 && v[0] == "ok \xC3\xA9");
        CHECK(v[1] == "bad \xEF\xBF\xBD\xEF\xBF\xBD here");
        for (const std::string& l : v) CHECK(SanitizeUtf8(l) == l);
        CHECK(Parse(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}")).ok);
        std::string raw = Serialize(Call(s, "read_log", "{\"log\":\"mod\"}"));
        CHECK(Parse(raw).ok && SanitizeUtf8(raw) == raw);
    }
    // redaction: the profile and any other account's path, in lines and in the filter
    {
        g_log = "start C:\\Users\\Alex\\AppData\\x\nother C:\\Users\\Someone\\y\nforward d:/users/Bob/z\nplain line\n";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(Lines(o) == (std::vector<std::string>{"start %USERPROFILE%\\AppData\\x", "other C:\\Users\\<user>\\y", "forward d:/users/<user>/z", "plain line"}));
        const std::string text = Serialize(o);
        for (const char* secret : {"Alex", "Someone", "Bob"}) CHECK(text.find(secret) == std::string::npos);
        // the account name cannot be probed through `contains`
        const Value& probe = Ok(s, Call(s, "read_log", "{\"log\":\"mod\",\"contains\":\"alex\"}"), "read_log");
        CHECK(Lines(probe).empty());
        const Value& shown = Ok(s, Call(s, "read_log", "{\"log\":\"mod\",\"contains\":\"%userprofile%\"}"), "read_log");
        CHECK(Lines(shown).size() == 1);
        // escaped (doubled) separators in a JSON-style log are redacted too, and the name cannot be probed
        g_log = "path C:\\\\Users\\\\bobsecret\\\\AppData\\\\x\n";
        const Value& esc = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(Lines(esc) == std::vector<std::string>{"path C:\\\\Users\\\\<user>\\\\AppData\\\\x"});
        CHECK(Lines(Ok(s, Call(s, "read_log", "{\"log\":\"mod\",\"contains\":\"bobsecret\"}"), "read_log")).empty());
    }
    // game.log identity: the Windows account name, account and player identifiers, e-mail addresses
    {
        g_log =
            "[GameProfiles]: Successfully activated profile 'default' for user 'zq9handle'\n"
            "username zq9handle signedIn 1 userIndex 0\n"
            "<Expect Incoming Connection> session=ab12 node_id=cd34 nickname=\"Zq9Nick\" playerGEID=1234567890 [Team_Network]\n"
            "<Expect Incoming Connection> nickname=Zq9Bare playerGEID=77 playerId=88 accountId=99 handle=zq9h2 ok\n"
            "SetLocalPlayerId 1234567890 firstGEID[1234567890] done\n"
            "mail zq9@example.com sent, owner Alex ran alex-tool and Alexa, ALEX.\n"
            "{\"nickname\":\"Zq9Json\",\"playerGEID\": 5}\n";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"game\",\"lines\":500}"), "read_log");
        CHECK(Lines(o) == (std::vector<std::string>{
            "[GameProfiles]: Successfully activated profile 'default' for user '<redacted>'",
            "username <redacted> signedIn 1 userIndex 0",
            "<Expect Incoming Connection> session=ab12 node_id=cd34 nickname=\"<redacted>\" playerGEID=<redacted> [Team_Network]",
            "<Expect Incoming Connection> nickname=<redacted> playerGEID=<redacted> playerId=<redacted> accountId=<redacted> handle=<redacted> ok",
            "SetLocalPlayerId <redacted> firstGEID[<redacted>] done",
            "mail <redacted> sent, owner <user> ran <user>-tool and Alexa, <user>.",
            "{\"nickname\":\"<redacted>\",\"playerGEID\": <redacted>}"}));
        const std::string text = Serialize(o);
        for (const char* secret : {"zq9", "Zq9", "1234567890", "example.com"}) CHECK(text.find(secret) == std::string::npos);
        // the filter sees redacted text: neither the name, an identifier nor an address can be probed
        for (const char* probe : {"zq9handle", "zq9nick", "zq9bare", "zq9json", "zq9h2", "1234567890", "example.com"}) {
            const std::string args = std::string("{\"log\":\"game\",\"contains\":\"") + probe + "\"}";
            CHECK(Lines(Ok(s, Call(s, "read_log", args), "read_log")).empty());
        }
        CHECK(Lines(Ok(s, Call(s, "read_log", "{\"log\":\"game\",\"contains\":\"zq9handle\"}"), "read_log")).empty());
        CHECK(Lines(Ok(s, Call(s, "read_log", "{\"log\":\"game\",\"contains\":\"<user>\"}"), "read_log")).size() == 1);
        CHECK(Lines(Ok(s, Call(s, "read_log", "{\"log\":\"game\",\"contains\":\"<redacted>\"}"), "read_log")).size() == 7);
    }
    // a name learned anywhere in the scanned window is hidden on every line, also when its own line is not returned
    {
        g_log =
            "User Login Success - Handle[zq9hand], Nickname[Zq9Nick], Account[1234567]\n"
            "Character: geid 200123456 - accountId 300123456 - name Zq9Char\n"
            "player_name=Zq9Pn\n"
            "chat from ZQ9HAND: hi Zq9Char and zq9nick (Zq9Pn)\n"
            "plain\n";
        const Value& two = Ok(s, Call(s, "read_log", "{\"log\":\"game\",\"lines\":2}"), "read_log");
        CHECK(Lines(two) == (std::vector<std::string>{"chat from <redacted>: hi <redacted> and <redacted> (<redacted>)", "plain"}));
        const Value& all = Ok(s, Call(s, "read_log", "{\"log\":\"game\",\"lines\":10}"), "read_log");
        CHECK(Lines(all) == (std::vector<std::string>{
            "User Login Success - Handle[<redacted>], Nickname[<redacted>], Account[<redacted>]",
            "Character: geid <redacted> - accountId <redacted> - name <redacted>",
            "player_name=<redacted>",
            "chat from <redacted>: hi <redacted> and <redacted> (<redacted>)",
            "plain"}));
        const std::string text = Serialize(all);
        for (const char* secret : {"zq9", "Zq9", "ZQ9", "1234567", "200123456", "300123456"}) CHECK(text.find(secret) == std::string::npos);
        for (const char* probe : {"zq9hand", "zq9nick", "zq9char", "zq9pn", "1234567", "200123456"}) {
            const std::string args = std::string("{\"log\":\"game\",\"contains\":\"") + probe + "\"}";
            CHECK(Lines(Ok(s, Call(s, "read_log", args), "read_log")).empty());
        }
        // a name announced later in the window also hides the lines before it
        g_log = "chat from Zq9Late\nUser Login Success - Handle[Zq9Late], Nickname[x1], Account[1]\n";
        const Value& late = Ok(s, Call(s, "read_log", "{\"log\":\"game\"}"), "read_log");
        CHECK(Serialize(late).find("Zq9Late") == std::string::npos);
    }
    // a tail of a larger file: the cut first line is dropped and the result says older content was not scanned
    {
        g_log = "rtial line\nfull 1\nfull 2\n";
        g_log_older = true;
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(Lines(o) == (std::vector<std::string>{"full 1", "full 2"}) && Get(o, "truncated").AsBool());
        g_log = "no newline at all";
        CHECK(Lines(Structured(Call(s, "read_log", "{\"log\":\"mod\"}"))).empty());
        g_log_older = false;
        // the real 2 MiB window: the server asks for exactly kMaxLogBytes
        g_log = std::string(kMaxLogBytes, 'a') + "\nlast\n";
        const Value& big = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(Lines(big) == std::vector<std::string>{"last"} && Get(big, "truncated").AsBool());
    }
    // a missing file is a normal result; an unreadable one is a tool error, not a JSON-RPC error
    {
        g_log.clear(); g_log_exists = false;
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"game\"}"), "read_log");
        CHECK(!Get(o, "exists").AsBool() && Lines(o).empty() && !Get(o, "truncated").AsBool());
        g_log_exists = true; g_log_error = "the file is locked";
        const Value r = Call(s, "read_log", "{\"log\":\"mod\"}");
        CHECK(Get(r, "error").IsNull() && Get(Get(r, "result"), "isError").AsBool());
        CHECK(Get(Get(Get(r, "result"), "content").Items()[0], "text").AsString().find("locked") != std::string::npos);
        g_log_error.clear();
    }
}

static void ToolCallChecks() {
    Server s = MakeServer();
    Value r = Call(s, "no_such_tool");
    CHECK(ErrorCode(r) == -32602 && Get(Get(r, "error"), "message").AsString().find("no_such_tool") != std::string::npos);
    r = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{}}"));
    CHECK(ErrorCode(r) == -32602);
    r = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\"}"));
    CHECK(ErrorCode(r) == -32602);
    r = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":5}}"));
    CHECK(ErrorCode(r) == -32602);
    // arguments may be left out for tools that take none
    r = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"get_status\"}}"));
    CHECK(Get(r, "result").IsObject());
    // the unknown name is echoed redacted and cleaned
    r = Call(s, "C:\\Users\\Eve\\x");
    CHECK(Serialize(r).find("Eve") == std::string::npos);
}

static void IdentityChecks() {
    const std::vector<std::string> none, alex = {"Alex"};
    // the Windows account name: whole tokens only, any case, anywhere
    CHECK(Redact("hi Alex.", {}, alex) == "hi <user>.");
    CHECK(Redact("ALEX alex_x x-alex Alexa (alex) 'alex'", {}, alex) == "<user> <user>_x x-<user> Alexa (<user>) '<user>'");
    CHECK(Redact("a1alex alex1", {}, alex) == "a1alex alex1");
    CHECK(Redact("Alex Alex", {}, {"alex", "ALEX", ""}) == "<user> <user>");
    CHECK(Redact("user redacted Users USERPROFILE", {}, {"user", "Redacted", "users", "userprofile"}) == "user redacted Users USERPROFILE");
    // trade-off: an account named like a common word hides that word everywhere
    CHECK(Redact("admin login by Admin", {}, {"admin"}) == "<user> login by <user>");
    // names of 1 or 2 characters are not searched for in text; paths are still redacted
    CHECK(Redact("a b ab 12 x", {}, {"a", "ab", "1"}) == "a b ab 12 x");
    CHECK(Redact("C:\\Users\\ab\\x ab", {}, {"ab"}) == "C:\\Users\\<user>\\x ab");
    CHECK(Redact("C:\\Users\\ab\\x ab", {"C:\\Users\\ab"}, {"ab"}) == "%USERPROFILE%\\x ab");
    CHECK(Redact("abc abcd", {}, {"abc"}) == "<user> abcd");
    // idempotent
    CHECK(Redact(Redact("Alex username Alex for user 'Alex' a@b.co", {}, alex), {}, alex) == Redact("Alex username Alex for user 'Alex' a@b.co", {}, alex));
    // identity keys: the value goes, the key stays
    CHECK(Redact("activated profile 'default' for user 'Bob Q'", {}, none) == "activated profile 'default' for user '<redacted>'");
    CHECK(Redact("For User \"Bob\"!", {}, none) == "For User \"<redacted>\"!");
    CHECK(Redact("for user bob", {}, none) == "for user bob");   // only the quoted form is a key
    CHECK(Redact("looking for user 'x", {}, none) == "looking for user 'x");   // no closing quote
    CHECK(Redact("username Bob signedIn 1", {}, none) == "username <redacted> signedIn 1");
    CHECK(Redact("Username=Bob, x", {}, none) == "Username=<redacted>, x");
    CHECK(Redact("username: 'Bob'", {}, none) == "username: '<redacted>'");
    CHECK(Redact("usernames are", {}, none) == "usernames are");
    CHECK(Redact("username", {}, none) == "username");
    CHECK(Redact("nickname=\"A B\" x", {}, none) == "nickname=\"<redacted>\" x");
    CHECK(Redact("nickname = Bob x", {}, none) == "nickname = <redacted>");   // an unquoted nickname may hold several words
    CHECK(Redact("nickname=\"\" x", {}, none) == "nickname=\"\" x");
    CHECK(Redact("PLAYERGEID=1;accountId:2 playerId=3)handle=h", {}, none) == "PLAYERGEID=<redacted>;accountId:<redacted> playerId=<redacted>)handle=<redacted>");
    CHECK(Redact("myhandle=x geidx=1 handled=1", {}, none) == "myhandle=<redacted> geidx=1 handled=1");   // a key matches on its last part
    CHECK(Redact("firstGEID[42] lastgeid[]", {}, none) == "firstGEID[<redacted>] lastgeid[]");
    CHECK(Redact("nickname", {}, none) == "nickname");
    // e-mail addresses
    CHECK(Redact("mail a.b+c@mail.example.com. end", {}, none) == "mail <redacted>. end");
    CHECK(Redact("a@b x@y.z @a.com me@x.io", {}, none) == "a@b x@y.z @a.com <redacted>");
    CHECK(Redact("username bob@example.com", {}, none) == "username <redacted>");
    // unquoted values with spaces stop at the next key
    CHECK(Redact("nickname=Bob The Builder playerGEID=5 ok", {}, none) == "nickname=<redacted> playerGEID=<redacted> ok");
    CHECK(Redact("nickname=Bob The Builder [Team_Network]", {}, none) == "nickname=<redacted> [Team_Network]");
    CHECK(Redact("Username: Bob Smith", {}, none) == "Username: <redacted>");
    CHECK(Redact("username Bob Smith", {}, none) == "username <redacted> Smith");   // the plain form hides one word
    // other shapes: a space before the bracket, SetLocalPlayerId with parentheses, underscored and JSON-escaped keys
    CHECK(Redact("secondGEID [42] x", {}, none) == "secondGEID [<redacted>] x");
    CHECK(Redact("SetLocalPlayerId(123) SetLocalPlayerId(id=456) SetLocalPlayerId: 789", {}, none) ==
          "SetLocalPlayerId(<redacted>) SetLocalPlayerId(id=<redacted>) SetLocalPlayerId: <redacted>");
    CHECK(Redact("player_id=1 account_id=2 userId=3 Nick=bob display_name=\"A B\"", {}, none) ==
          "player_id=<redacted> account_id=<redacted> userId=<redacted> Nick=<redacted> display_name=\"<redacted>\"");
    CHECK(Redact("{\\\"nickname\\\":\\\"Bob\\\",\\\"x\\\":1}", {}, none) == "{\\\"nickname\\\":\\\"<redacted>\\\",\\\"x\\\":1}");
    CHECK(Redact("nickname=\"a\\\"b\" end", {}, none) == "nickname=\"<redacted>\" end");   // an escaped quote does not end the value
    // Player_<id> batch names
    CHECK(Redact("Player_200123456789 spawned; CreatePlayer Player_Bob_TheBuilder", {}, none) ==
          "Player_<redacted> spawned; CreatePlayer Player_<redacted>");
    CHECK(Redact("Player_ Players player_x", {}, none) == "Player_ Players player_<redacted>");
    // not identifiers
    CHECK(Redact("username is empty; handle=0x1F4 closed", {}, none) == "username is empty; handle=0x1F4 closed");
    // an address made from the account name goes whole
    CHECK(Redact("mail alex@example.com", {}, alex) == "mail <redacted>");
    // long names are also found inside host and folder names; short ones are not
    CHECK(Redact("DESKTOP-ZORBAX zorbax_saves zorbax2 zorbax", {}, {"Zorbax"}) == "DESKTOP-<user> <user>_saves <user>2 <user>");
    CHECK(Redact("DESKTOP-ALEX alex_x ALEX-PC alexa_x", {}, alex) == "DESKTOP-<user> <user>_x <user>-PC alexa_x");
    // a domain profile folder goes whole, not only up to the dot
    CHECK(Redact("hi zorbax.CORP and zorbax", {}, {"zorbax", "zorbax.CORP"}) == "hi <user> and <user>");
    // the login status line: bracket forms of handle, nickname and account
    CHECK(Redact("User Login Success - Handle[zq9hand], Nickname[Zq9 Nick], Account[1234567]", {}, none) ==
          "User Login Success - Handle[<redacted>], Nickname[<redacted>], Account[<redacted>]");
    CHECK(Redact("handle [x] account [7]", {}, none) == "handle [<redacted>] account [<redacted>]");
    // space-separated pairs, as AccountLoginCharacterStatus_Character writes them: numbers and the character name
    CHECK(Redact("geid 200123456 - accountId 300123456 - name Zq9Char", {}, none) == "geid <redacted> - accountId <redacted> - name <redacted>");
    CHECK(Redact("<AccountLoginCharacterStatus_Character> Character: geid 200123456 - accountId 300123456 - name Zq9Char - state STATE_CURRENT", {}, none) ==
          "<AccountLoginCharacterStatus_Character> Character: geid <redacted> - accountId <redacted> - name <redacted> - state STATE_CURRENT");
    CHECK(Redact("name Bob", {}, none) == "name Bob");                          // no geid on the line: not a character name
    CHECK(Redact("geid 1234\nname Bob", {}, none) == "geid <redacted>\nname Bob");   // the line ends the context
    CHECK(Redact("geid:1234567 geid=77 playerGEID 9999 geid [123] userId 5 geid", {}, none) ==
          "geid:<redacted> geid=<redacted> playerGEID <redacted> geid [<redacted>] userId <redacted> geid");
    CHECK(Redact("accountId is empty, geid unknown", {}, none) == "accountId is empty, geid unknown");
    CHECK(Redact("{\"geid\":200123456,\"accountId\":\"300123456\"}", {}, none) == "{\"geid\":<redacted>,\"accountId\":\"<redacted>\"}");
    // player_name is a key; other player_ keys and batch names behave as before
    CHECK(Redact("player_name=Foo", {}, none) == "player_name=<redacted>");
    CHECK(Redact("Player_Name: Foo", {}, none) == "Player_Name: <redacted>");
    CHECK(Redact("character_name=\"Foo Bar\"", {}, none) == "character_name=\"<redacted>\"");
    CHECK(Redact("player_count=5 Player_Foo player_Foo=1", {}, none) == "player_count=5 Player_<redacted> player_Foo=1");
    // paths: /c/Users, doubled and mixed separators, a trailing name
    CHECK(Redact("x /c/Users/Foo/y C:\\\\Users\\\\Foo\\\\y C:/Users\\Foo/y", {}, none) == "x /c/Users/<user>/y C:\\\\Users\\\\<user>\\\\y C:/Users\\<user>/y");
    // hostile lines stay fast: none of these may take a noticeable time
    {
        const auto t0 = std::chrono::steady_clock::now();
        const size_t sz = 1u << 20;
        std::string a, g, q, k, u, h, w;
        for (size_t i = 0; i < sz / 2; ++i) a += "a@";
        for (size_t i = 0; i < sz / 5; ++i) g += "GEID[";
        for (size_t i = 0; i < sz / 12; ++i) q += "nickname=\"x ";
        for (size_t i = 0; i < sz / 11; ++i) u += "for user '";
        for (size_t i = 0; i < sz / 9; ++i) k += "username: a";
        for (size_t i = 0; i < sz / 7; ++i) h += "Handle[";
        for (size_t i = 0; i < sz / 15; ++i) w += "geid 1 name x ";
        for (const std::string* hostile : {&a, &g, &q, &u, &k, &h, &w}) {
            const std::string r = Redact(*hostile, {}, alex);
            CHECK(!r.empty());
            Identities id;
            Learn(id, *hostile);
            CHECK(id.learned.size() <= kMaxLearned);
            CHECK(!RedactWith(*hostile, id).empty());
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(secs < 5.0);
    }
}

// The shared redactor learns identity values from a whole file and hides them everywhere (crash reports and read_log).
static void LearnChecks() {
    using namespace sco::launcher::redact;
    Identities id;
    id.profileRoots = {"C:\\Users\\Alex"};
    id.accountNames = {"Alex"};
    const std::string a = "User Login Success - Handle[zq9hand], Nickname[Zq9Nick], Account[1234567]\nwelcome back zq9hand!\n";
    const std::string b = "free text Zq9Nick and ZQ9HAND and Zq9Char waved\nCharacter geid 200123456 - accountId 300123456 - name Zq9Char\n";
    Learn(id, a);
    Learn(id, b);   // a second file teaches the same set: crash reports learn from every bundled file
    CHECK(RedactWith(a, id) == "User Login Success - Handle[<redacted>], Nickname[<redacted>], Account[<redacted>]\nwelcome back <redacted>!\n");
    CHECK(RedactWith(b, id) == "free text <redacted> and <redacted> and <redacted> waved\nCharacter geid <redacted> - accountId <redacted> - name <redacted>\n");
    CHECK(RedactWith(RedactWith(b, id), id) == RedactWith(b, id));
    // a value is replaced inside other words, in any case
    CHECK(RedactWith("xZQ9HANDy Zq9nicks", id) == "x<redacted>y <redacted>s");
    // a learned name from one file reaches another file that never names it by key
    Identities two;
    Learn(two, "nickname=\"Zq9Two\" in file one");
    CHECK(RedactWith("file two says Zq9Two left", two) == "file two says <redacted> left");
    // a multi-word value also teaches its first word
    Identities multi;
    Learn(multi, "nickname=Zq9First Second\n");
    CHECK(RedactWith("hi zq9first", multi) == "hi <redacted>");
    // paths and account names still go
    CHECK(RedactWith("C:\\Users\\Alex\\x Alex a@b.co", id) == "%USERPROFILE%\\x <user> <redacted>");
    // not learned: short values, common words, hex handles, markers, anything with a marker character
    Identities none;
    Learn(none, "nickname=ab handle=true username=null nickname=default handle=0x1F4 handle=red nickname=user nick=<x>");
    CHECK(none.learned.empty());
    CHECK(RedactWith("ab true null default red user", none) == "ab true null default red user");
    // the list is bounded
    Identities many;
    std::string flood;
    for (int k = 0; k < 2000; ++k) flood += "nickname=zq9n" + std::to_string(100000 + k) + "\n";
    Learn(many, flood);
    CHECK(many.learned.size() == kMaxLearned);
}

// Findings from review: identities that a tail read misses, crowded-out logins, prefixed keys, e-mail forms,
// over-redaction of ordinary text, hostile input and the crash-report flow.
static void ReviewChecks() {
    using namespace sco::launcher::redact;
    const std::vector<std::string> none;
    const std::string login = "User Login Success - Handle[Zorbax9], Nickname[Quillon Vex], Account[300111230]\n";

    // the login line is outside the scanned tail (a huge Game.log): the start of the log teaches the name
    {
        Server s = MakeServer();
        g_log_exists = true; g_log_error.clear(); g_log_older = true;
        g_heads.clear();
        g_heads["game"] = login + "filler\n";
        g_log = "tail start\nCActor::Kill: 'Zorbax9'\nplayer Zorbax9 chat\nquillon vex waved\n";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"game\"}"), "read_log");
        CHECK(Serialize(o).find("Zorbax9") == std::string::npos && Serialize(o).find("Quillon") == std::string::npos);
        CHECK(Serialize(o).find("quillon") == std::string::npos);
        CHECK(Lines(Ok(s, Call(s, "read_log", "{\"log\":\"game\",\"contains\":\"zorbax9\"}"), "read_log")).empty());
        // the session remembers it, even when no head is available any more
        g_heads.clear();
        g_log = "after chat Zorbax9\n";
        const Value& again = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(Serialize(again).find("Zorbax9") == std::string::npos);
        // the head of another log teaches a name too
        g_heads["mod"] = "nickname=\"Zq9Other\" joined\n";
        g_log = "bye Zq9Other\n";
        CHECK(Serialize(Ok(s, Call(s, "read_log", "{\"log\":\"launcher\"}"), "read_log")).find("Zq9Other") == std::string::npos);
        g_heads.clear(); g_log_older = false;
    }
    // other players do not crowd the user out: login-line values have their own list
    {
        Identities id;
        std::string many;
        for (int k = 0; k < 700; ++k) many += "playerGEID[" + std::to_string(1000000 + k) + "] nickname=\"npc" + std::to_string(1000 + k) + "\"\n";
        Learn(id, many);
        Learn(id, login);
        CHECK(id.learned.size() == kMaxLearned);
        CHECK(RedactWith("Spawn Flow Player Zorbax9 entered; killed by Zorbax9", id) == "Spawn Flow Player <redacted> entered; killed by <redacted>");
    }
    // a key matches on its last part: prefixes are covered, and the old redactor's shapes still are
    {
        const std::pair<const char*, const char*> table[] = {
            {"rsiHandle[Hidden1]", "rsiHandle[<redacted>]"},
            {"MyAccount[55512345]", "MyAccount[<redacted>]"},
            {"userNickname[Sneaky2]", "userNickname[<redacted>]"},
            {"SomeHandle[Foo3x]", "SomeHandle[<redacted>]"},
            {"rsiHandle=Hidden3", "rsiHandle=<redacted>"},
            {"playerHandle: Hidden4", "playerHandle: <redacted>"},
            {"accountName=Hidden5", "accountName=<redacted>"},
            {"user: Hidden6", "user: <redacted>"},
            {"user=Hidden7", "user=<redacted>"},
            {"handle x owner=Hidden8 player=Hidden9 login=Hidden10", "handle x owner=<redacted> player=<redacted> login=<redacted>"},
            {"handle x char_name=Hidden12 character=Hidden13", "handle x char_name=<redacted> character=<redacted>"},
            {"screenName=Hidden14 profileName=Hidden15", "screenName=<redacted> profileName=<redacted>"},
            {"uid=555111222 rsi_id=777888999", "uid=<redacted> rsi_id=<redacted>"},
            {"targetAccountId=300123456 owner_account_id=5", "targetAccountId=<redacted> owner_account_id=<redacted>"},
            {"geid  #123456", "geid  #<redacted>"},
            {"Login ok name=Hidden11", "Login ok name=<redacted>"},
            {"geid 1 - name O'Brien - state x", "geid <redacted> - name <redacted> - state x"},
            {"name O'Brien - geid 1", "name <redacted> - geid <redacted>"},
            {"{\"geid\":1,\"name\":\"Zq9 Char\"}", "{\"geid\":<redacted>,\"name\":\"<redacted>\"}"},
        };
        for (const auto& row : table) {
            const std::string got = Redact(row.first, none, none);
            if (got != row.second) std::cerr << "got " << got << " for " << row.first << "\n";
            CHECK(got == row.second);
        }
        // no context, no key: ordinary text is left alone
        CHECK(Redact("name=shader users=5 ThreadId=7", none, none) == "name=shader users=5 ThreadId=7");
        CHECK(Redact("handle=0xBEEF", none, none) == "handle=0xBEEF");
        CHECK(Redact("accountId is empty; name collision detected", none, none) == "accountId is empty; name collision detected");
    }
    // e-mail: non-ASCII, IDN domains, percent-encoded and quoted local parts
    CHECK(Redact("p\xC3\xA4t@example.com", none, none) == "<redacted>");
    CHECK(Redact("x@ex\xC3\xA4mple.com", none, none) == "<redacted>");
    CHECK(Redact("pat%40example.com", none, none) == "<redacted>");
    CHECK(Redact("see \"pat q\"@example.com now", none, none) == "see <redacted> now");
    CHECK(Redact("a@b x@y.z", none, none) == "a@b x@y.z");
    // home paths, also for one and two character names
    CHECK(Redact("/home/ab/x /home/zorbax/y", none, none) == "/home/<user>/x /home/<user>/y");
    // ordinary words and numbers survive a learned value
    {
        Identities id;
        Learn(id, "playerId=100 nickname=The Dude handle=Server Nickname[Star] handle=Hand 10\n");
        const std::string plain = "1000ms build 2.4.100 port 5100 the weather other theory Starting Star Citizen starter server SomeHandle collision";
        CHECK(RedactWith(plain, id) == plain);
        CHECK(RedactWith("hi dude", id) == "hi <redacted>");
        Identities digits;
        Learn(digits, "accountId=[300111230] x\n");
        CHECK(digits.learned.size() == 1 && digits.learned[0] == "300111230");
        CHECK(RedactWith("see 300111230.", digits) == "see <redacted>.");
    }
    // multi-word names: every word is learned, an unquoted run stops at a plain word
    {
        Identities id;
        Learn(id, "geid 1 - accountId 2 - name Quillon Von Zeta\n");
        CHECK(RedactWith("later Von Zeta and quillon", id) == "later <redacted> <redacted> and <redacted>");
        CHECK(Redact("nickname=Bob Smith went to town for the day", none, none) == "nickname=<redacted> went to town for the day");
        CHECK(Redact("nickname=Bob the Builder playerGEID=5", none, none) == "nickname=<redacted> playerGEID=<redacted>");
    }
    // many same-prefix values over a long same-prefix text take one pass
    {
        Identities id;
        for (int k = 0; k < static_cast<int>(kMaxLearned); ++k) id.learned.push_back(std::string(58, 'a') + std::to_string(1000 + k));
        std::string text;
        while (text.size() < (2u << 20)) text += std::string(120, 'a') + "\n";
        const auto t0 = std::chrono::steady_clock::now();
        const std::string r = RedactWith(text, id);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(!r.empty() && secs < 3.0);
        // overlapping and touching hits become one marker
        Identities two;
        two.learned = {"zq9alpha", "alphazq9beta"};
        CHECK(RedactWith("x zq9alphazq9beta y zq9alpha", two) == "x <redacted> y <redacted>");
    }
    // a line full of profile paths is redacted before the cut, so a name never sits at the cut
    {
        Server s = MakeServer();
        g_log_exists = true; g_log_older = false; g_log_error.clear(); g_heads.clear();
        std::string line;
        while (line.size() < 70000) line += "C:\\Users\\Zq9Path\\ ";
        g_log = line + " tail Zq9Path\n";
        CHECK(Serialize(Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log")).find("Zq9Path") == std::string::npos);
    }
    // the crash-report flow: learn from every bundled file first, then redact each file
    {
        Identities host;
        host.profileRoots = {"C:\\Users\\Alex"};
        host.accountNames = {"Alex"};
        const std::string files[] = {"mod: loaded from C:\\Users\\Alex\\sc, zq9crew joined\n",
                                     login + "welcome ZQ9CREW? no: zorbax9 (Quillon) mail p@example.com\n"};
        for (const std::string& f : files) Learn(host, f);
        const Redactor r(host);
        for (const std::string& f : files) {
            const std::string out = r.Apply(f);
            for (const char* secret : {"Zorbax9", "zorbax9", "Quillon", "300111230", "Alex", "p@example.com"})
                CHECK(out.find(secret) == std::string::npos);
        }
        CHECK(r.Apply(files[0]) == "mod: loaded from %USERPROFILE%\\sc, zq9crew joined\n");   // nothing announced zq9crew
    }
}

// Second review round: the strict crash mode, flooded lists, apostrophes, quoted names with bracketed ids,
// key shapes, and ordinary words that must survive.
static void SecondReviewChecks() {
    using namespace sco::launcher::redact;
    // crash mode: a 3 letter handle, a common-word handle and glued words are hidden, as the old redactor did
    {
        Identities id;
        id.strict = true;
        Learn(id, "User Login Success - Handle[Bob], Nickname[Admin], Account[300111230]\n");
        const std::string out = RedactWith("Bobcat xBobx Bob99 Welcome Admin Admin1 administrator", id);
        for (const char* w : {"Bob", "Admin", "admin"}) CHECK(out.find(w) == std::string::npos);
        // the same text in the relaxed mode keeps ordinary words that contain a short handle
        Identities relaxed;
        Learn(relaxed, "User Login Success - Handle[Bob], Nickname[Admin], Account[300111230]\n");
        CHECK(RedactWith("Bobcat", relaxed) == "Bobcat");
    }
    // the old shapes: geid 200012345, accountId=9876543, playerGEID: 1, nickname="x"
    {
        Identities id;
        id.strict = true;
        const std::string in = "geid 200012345 accountId=9876543 playerGEID: 1 nickname=\"Fox\" Fox lives\n";
        Learn(id, in);
        const std::string out = RedactWith(in, id);
        for (const char* w : {"200012345", "9876543", "Fox"}) CHECK(out.find(w) == std::string::npos);
    }
    // junk before the real login line cannot keep the handle out
    {
        Identities id;
        std::string head;
        for (int k = 0; k < 80; ++k) head += "Login nickname=Junk" + std::to_string(100000 + k) + "_aaaaaa\n";
        for (int k = 0; k < 600; ++k) head += "nickname=Noise" + std::to_string(100000 + k) + "\n";
        head += "User Login Success - Handle[Zorbax_Kestrel77], Nickname[Zorbax_Kestrel77], Account[300111230]\n";
        Learn(id, head);
        CHECK(RedactWith("Hello Zorbax_Kestrel77 welcome", id) == "Hello <redacted> welcome");
    }
    // multi-word names with apostrophes on every word, and a long name
    {
        const std::pair<const char*, const char*> table[] = {
            {"geid 1 - accountId 2 - name Sean O'Brien", "geid <redacted> - accountId <redacted> - name <redacted>"},
            {"name Mary-Jane O'Neil - geid 1", "name <redacted> - geid <redacted>"},
            {"nickname=Sean D'Arcy-Smith joined", "nickname=<redacted> joined"},
            {"name Vexa Mirandola Von Trapp Smith - geid 1", "name <redacted> - geid <redacted>"},
        };
        for (const auto& t : table) CHECK(RedactIdentityKeys(t.first) == t.second);
    }
    // a quoted name followed by a bracketed id, and Player[Name]
    {
        Identities id;
        const std::string in = "CActor::Kill: 'Vexa_Q' [200099887766] in zone killed by 'Other_Z' [200011112222]\n"
                               "Ran corpse test for player 'Vexa_Q' [200099887766]\nPlayer[Vexa_Q] Requesting Inventory\n"
                               "driven by \"Other_Z\" [200011112222] later Other_Z left\n";
        Learn(id, in);
        const std::string out = RedactWith(in, id);
        for (const char* w : {"Vexa_Q", "Other_Z", "200099887766", "200011112222"}) CHECK(out.find(w) == std::string::npos);
        CHECK(out.find("CActor::Kill") != std::string::npos);
        CHECK(RedactIdentityKeys("it says 'hello' [5] and 'a' [12345678901234]") == "it says 'hello' [5] and 'a' [12345678901234]");
    }
    // keys are not mangled; batch names and glued, quoted, XML and JSON-array forms are hidden
    {
        for (const char* keep : {"Player_Count=5", "PLAYER_COUNT=5", "player_count: 5"})
            CHECK(RedactIdentityKeys(keep) == keep);
        CHECK(RedactIdentityKeys("Player_Zorbax now") == "Player_<redacted> now");
        CHECK(RedactIdentityKeys("batch Player_200099887766 done") == "batch Player_<redacted> done");
        const std::pair<const char*, const char*> table[] = {
            {"geid200099887766", "geid<redacted>"},
            {"accountid\"5544332211\"", "accountid\"<redacted>\""},
            {"GEID[ 123 ", "GEID[ <redacted> "},
            {"<Nickname>Zorbax9</Nickname>", "<Nickname><redacted></Nickname>"},
            {"<geid>200099887766</geid>", "<geid><redacted></geid>"},
            {"<AccountId>5544332211</AccountId>", "<AccountId><redacted></AccountId>"},
            {"\"nickname\":[\"Zorbax9\"]", "\"nickname\":[\"<redacted>\"]"},
            {"Login User Login Success - Name[Zed], Handle[Zed2]", "Login User Login Success - Name[<redacted>], Handle[<redacted>]"},
        };
        for (const auto& t : table) {
            const std::string out = RedactIdentityKeys(t.first);
            if (out != t.second) std::cerr << "got [" << out << "] want [" << t.second << "]\n";
            CHECK(out == t.second);
        }
    }
    // ordinary diagnostic words stay readable
    {
        for (const char* keep : {"Owner: TerrainSystem", "Login: starting", "Character: Human_Male_01 loaded",
                                 "CVars: user=false; owner=0; login=true; player=1", "Owner: TerrainSystem Login: starting"})
            CHECK(RedactIdentityKeys(keep) == keep);
        Identities id;
        Learn(id, "Owner: TerrainSystem\nCharacter: Human_Male_01 loaded\n");
        CHECK(RedactWith("TerrainSystem Human_Male_01", id) == "TerrainSystem Human_Male_01");
    }
}

// Third review round: the maintainer's decisions and what the differential test (tools/launcher_redact_diff_test.cpp) found.
// A noisy log cannot evict the real handle, the whole log file is scanned in chunks for login lines, common-word
// handles from login lines are replaced as whole tokens, short account names stop at - and _, %5C %2F %40 are
// decoded, weak keys and machine words are not learned, Game.log is opt-in, and hostile input stays fast.
static std::string Filler(size_t bytes) {   // ordinary log lines, about `bytes` long
    static const std::string line = "<2026-03-01T12:00:00.000Z> [Notice] <Physics> frame 12345 ok zone=Stanton port 5100 build 2.4.100\n";
    std::string t;
    t.reserve(bytes + line.size());
    while (t.size() < bytes) t += line;
    return t;
}

static void ThirdReviewChecks() {
    using namespace sco::launcher::redact;
    const std::vector<std::string> none;
    const std::string login = "User Login Success - Handle[Zorbax9], Nickname[Quillon Vex], Account[300111230]\n";

    // (2) a noisy log cannot evict the real handle: login values have their own bounded list that is never evicted
    {
        Identities id;
        std::string noise;
        for (int k = 0; k < 5000; ++k) noise += "[Notice] nickname=\"Npc" + std::to_string(100000 + k) + "\" playerGEID[" + std::to_string(2000000000 + k) + "]\n";
        Learn(id, noise);
        CHECK(id.learned.size() == kMaxLearned && id.priority.empty());
        Learn(id, login);
        CHECK(id.learned.size() == kMaxLearned && !id.priority.empty());
        Learn(id, noise);
        CHECK(RedactWith("hi Zorbax9 and quillon, 300111230", id) == "hi <redacted> and <redacted>, <redacted>");
        // the priority list is bounded and never evicts: the first values stay
        Identities full;
        std::string logins;
        for (size_t k = 0; k < kMaxPriorityPerSource + 50; ++k) logins += "User Login Success - Handle[Early" + std::to_string(1000 + k) + "]\n";
        Learn(full, logins);
        // one text adds at most kMaxPriorityPerSource login values: a flood in one source cannot fill the list
        CHECK(full.priority.size() == kMaxPriorityPerSource && full.priority[0] == "Early1000" && full.learned.empty());
        for (size_t round = 1; round < kMaxPriority / kMaxPriorityPerSource + 2; ++round) {
            std::string more;
            for (size_t k = 0; k < kMaxPriorityPerSource; ++k) more += "User Login Success - Handle[Round" + std::to_string(round) + "x" + std::to_string(1000 + k) + "]\n";
            Learn(full, more);
        }
        CHECK(full.priority.size() == kMaxPriority && full.priority[0] == "Early1000" && full.learned.empty());
        // one line adds at most a few values; a login line a few more
        Identities line;
        Learn(line, "x nickname=Aaa111 handle=Bbb222 accountName=Ccc333 rsiHandle=Ddd444 charName=Eee555 uid=1234567890 playerId=987654321 displayName=Fff666 screenName=Ggg777 profileName=Hhh888 playerName=Iii999\n");
        CHECK(line.learned.size() == kMaxOtherPerLine && line.priority.empty());
        Identities loginLine;
        std::string many = "User Login Success - ";
        for (int k = 0; k < 40; ++k) many += "nickname=Fake" + std::to_string(100 + k) + " ";
        Learn(loginLine, many + "\n");
        CHECK(loginLine.priority.size() <= kMaxLoginPerLine && loginLine.learned.empty());
        // the same through the server: 600 noise lines in the window, the login line only in the file's start
        Server s = MakeServer();
        g_log_exists = true; g_log_error.clear(); g_log_older = true;
        g_heads.clear();
        g_heads["mod"] = login + "filler\n";
        g_log = "cut line\nchat Zorbax9 first\n";
        for (int k = 0; k < 600; ++k) g_log += "[Notice] nickname=\"Npc" + std::to_string(100000 + k) + "\"\n";
        g_log += "chat Zorbax9 last\n";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\",\"contains\":\"chat\"}"), "read_log");
        CHECK(Lines(o) == (std::vector<std::string>{"chat <redacted> first", "chat <redacted> last"}));
        g_heads.clear(); g_log_older = false;
    }

    // (3) the whole log file is scanned in chunks; only login lines are parsed; what is skipped is said
    {
        Server s = MakeServer();
        g_log_exists = true; g_log_error.clear(); g_log_older = false;
        g_log = "tail chat Zorbax9 and quillon_vex and Zq9NoLogin\n";
        g_heads.clear();
        // a login line deep in a file of several MiB (the 2 MiB window cannot see it), cut across tiny chunks
        g_heads["launcher"] = Filler(3u << 20) + login + "<AccountLoginCharacterStatus_Character> Character: geid 2000998877 - accountId 5544332211 - name Quillon_Vex - state STATE_CURRENT\n" + Filler(1u << 20);
        g_heads["mod"] = "nickname=\"Zq9NoLogin\" joined but no login line announces it\n";
        for (size_t chunk : {size_t(1), size_t(5), size_t(4096), size_t(1) << 20}) {
            if (chunk < 5) g_heads["launcher"] = login + "<AccountLoginCharacterStatus_Character> Character: geid 2000998877 - accountId 5544332211 - name Quillon_Vex - state STATE_CURRENT\n" + Filler(2000);
            else g_heads["launcher"] = Filler(3u << 20) + login + "<AccountLoginCharacterStatus_Character> Character: geid 2000998877 - accountId 5544332211 - name Quillon_Vex - state STATE_CURRENT\n" + Filler(1u << 20);
            g_scan_chunk = chunk;
            Server fresh = MakeServer();
            const Value& o = Ok(fresh, Call(fresh, "read_log", "{\"log\":\"mod\"}"), "read_log");
            CHECK(Lines(o) == std::vector<std::string>{"tail chat <redacted> and <redacted> and Zq9NoLogin"});   // a plain nickname line in the file is not parsed
            CHECK(Get(o, "identity_scan_complete").AsBool() && Get(o, "notice").AsString().empty());
        }
        g_scan_chunk = 4096;
        // a login line longer than 16 KiB: its first 16 KiB are read (a handle near its start is learned), the result says so,
        // and the scan goes on after it
        g_heads["launcher"] = "User Login Success - Handle[Skipme1]" + std::string(20000, ' ') + "\n" + "User Login Success - Handle[Kept2x]\n";
        g_log = "chat Skipme1 and Kept2x\n";
        Server fresh = MakeServer();
        {
            const Value& cut = Ok(fresh, Call(fresh, "read_log", "{\"log\":\"mod\"}"), "read_log");
            CHECK(Lines(cut) == std::vector<std::string>{"chat <redacted> and <redacted>"});
            CHECK(!Get(cut, "identity_scan_complete").AsBool() && Get(cut, "notice").AsString().find("16 KiB") != std::string::npos);
        }
        // a name past the first 16 KiB of such a line is not learned, and the notice says so
        g_heads["launcher"] = "User Login Success - " + std::string(20000, ' ') + "Handle[Lateinline7]\n";
        g_log = "chat Lateinline7\n";
        Server late = MakeServer();
        {
            const Value& o2 = Ok(late, Call(late, "read_log", "{\"log\":\"mod\"}"), "read_log");
            CHECK(Lines(o2) == std::vector<std::string>{"chat Lateinline7"});
            CHECK(!Get(o2, "identity_scan_complete").AsBool() && Get(o2, "notice").AsString().find("16 KiB") != std::string::npos);
        }
        // a file past 64 MiB: only the first 64 MiB are read, and the result says so
        g_heads.clear();
        g_heads["mod"] = Filler(size_t(kMaxScanBytes)) + "User Login Success - Handle[Latecomer9]\n";
        g_log = "chat Latecomer9 and Zorbax9\n";
        g_heads["launcher_previous"] = login;
        Server big = MakeServer();
        const Value& o = Ok(big, Call(big, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(Lines(o) == std::vector<std::string>{"chat Latecomer9 and <redacted>"});
        CHECK(!Get(o, "identity_scan_complete").AsBool());
        const std::string notice = Get(o, "notice").AsString();
        CHECK(notice.find("mod") != std::string::npos && notice.find("64 MiB") != std::string::npos && notice.find("first") != std::string::npos);
        g_heads.clear(); g_log_older = false;
    }

    // (4) relaxed mode: a common-word handle from a real login line is replaced as a whole token
    {
        Identities relaxed, strict;
        strict.strict = true;
        const std::string l = "User Login Success - Handle[Admin], Nickname[Character], Account[300111230]\n";
        Learn(relaxed, l);
        Learn(strict, l);
        CHECK(RedactWith("Admin joined; Administrator, xAdminx, Character loaded, characters, admin.", relaxed) ==
              "<redacted> joined; Administrator, xAdminx, <redacted> loaded, characters, <redacted>.");
        CHECK(RedactWith("xAdminx Administrator", strict) == "x<redacted>x <redacted>istrator");
        // the same word from a line that is no login is not learned; nor are the words of a multi-word name
        Identities other;
        Learn(other, "nickname=Admin handle=Character\n");
        CHECK(other.learned.empty() && other.priority.empty());
        Identities multi;
        Learn(multi, "User Login Success - Nickname[The Great Zorbax]\n");
        CHECK(RedactWith("hi the weather, great Zorbax", multi) == "hi the weather, <redacted> <redacted>");
        // through the server: a common-word handle announced in the file's start
        Server s = MakeServer();
        g_log_exists = true; g_log_error.clear(); g_log_older = false;
        g_heads.clear();
        g_heads["mod"] = "User Login Success - Handle[Server], Nickname[Server]\n";
        g_log = "Server started; the server Server_1 and Servers\n";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(Lines(o) == std::vector<std::string>{"<redacted> started; the <redacted> <redacted>_1 and Servers"});
        g_heads.clear(); g_log_older = false;
    }

    // (5) short account names: - and _ end a name; %5C, %2F and %40 are decoded before paths and addresses
    {
        const std::vector<std::string> alex = {"Alex"};
        CHECK(Redact("ALEX-PC DESKTOP-ALEX alex_saves alexander xalex Alex1 (alex)", {}, alex) == "<user>-PC DESKTOP-<user> <user>_saves alexander xalex Alex1 (<user>)");
        CHECK(Redact("DESKTOP-ZORBAX zorbax_saves", {}, {"Zorbax"}) == "DESKTOP-<user> <user>_saves");
        CHECK(Redact("C%3A%5CUsers%5Cbobx%5Cfile pat%40example.com /home%2Fbobx%2Fdir c:%5cusers%5cbobx", {}, none) ==
              "C%3A\\Users\\<user>\\file <redacted> /home/<user>/dir c:\\users\\<user>");
        CHECK(Redact("C:%5CUsers%5CAlex%5Cx", {"C:\\Users\\Alex"}, alex) == "%USERPROFILE%\\x");
        CHECK(Redact("http%3A%2F%2Fexample.com%2Fa%2Fb and 100%25 and a%2Fb", {}, none) == "http%3A%2F%2Fexample.com%2Fa%2Fb and 100%25 and a%2Fb");   // nothing found: the bytes stay
        CHECK(Redact(Redact("C%3A%5CUsers%5Cbobx%5Cfile pat%40example.com", {}, none), {}, none) == Redact("C%3A%5CUsers%5Cbobx%5Cfile pat%40example.com", {}, none));
        CHECK(DecodePathEscapes("%5C%5c%2F%2f%40 %5 %zz%") == "\\\\//@ %5 %zz%");
    }

    // (6) weak keys and machine words are not learned; ordinary words stay readable
    {
        Identities id;
        const std::string in = "[net] user=SYSTEM owner=Zork99 player=Vexa77 login=Quill88 username=SYSTEM\nfilesystem SystemTime SYSTEM local NONE Zork99\n";
        Learn(id, in);
        CHECK(id.learned.empty() && id.priority.empty());
        const std::string out = RedactWith(in, id);
        CHECK(out.find("filesystem SystemTime SYSTEM local NONE Zork99") != std::string::npos);
        CHECK(out.find("user=<redacted>") != std::string::npos);   // hidden where it stands
        {   // relaxed mode never learns a machine word, from any shape
            Identities m;
            Learn(m, "nickname=SYSTEM handle=Local nickname=NULL nickname=default nickname=none nickname=ab nick=xy\nUser Login Success - Handle[SYSTEM], Nickname[None], Account[1], Handle[Default]\nHandle[LOCAL] nickname=\"null\"\n");
            CHECK(m.learned.empty() && m.priority.empty());
            CHECK(RedactWith("filesystem SystemTime default none null local system", m) == "filesystem SystemTime default none null local system");
        }
        {   // strict mode (crash reports) keeps what the old redactor did: a handle from its own shapes or a login line is
            // hidden everywhere, also a machine word or a piece of a marker; the other shapes still teach none
            Identities m;
            m.strict = true;
            Learn(m, "nickname=SYSTEM handle=Local nickname=NULL nickname=default nickname=none nickname=ab nick=xy\n");
            CHECK(m.learned.empty() && m.priority.empty());
            Identities n;
            n.strict = true;
            Learn(n, "chat Nobody says hi, Nobody2 left, red use\nUser Login Success - Handle[Nobody]\nHandle[red] Handle[use]\n");
            CHECK(RedactWith("chat Nobody says hi, Nobody2 left, red use", n) == "chat <redacted> says hi, <redacted>2 left, <redacted> <redacted>");
            // a learned `red` or `use` leaves the markers alone, and the result is stable
            const std::string once = RedactWith("C:\\Users\\bobx\\f pat@example.com geid 2000998877 Handle[Zorbax9] red", n);
            CHECK(once.find("<redacted>") != std::string::npos && once.find("<<") == std::string::npos);
            CHECK(RedactWith(once, n) == once);
        }
        CHECK(Redact("filesystem SystemTime SYSTEM", {}, {"SYSTEM", "local", "Default"}) == "filesystem SystemTime SYSTEM");
        // through the server: a weak key hides its own value only
        Server s = MakeServer();
        g_log_exists = true; g_log_error.clear(); g_log_older = false; g_heads.clear();
        g_log = "[net] user=Zq9Weak seen\nlater Zq9Weak spoke\nuser=SYSTEM then filesystem and SystemTime\n";
        const auto lines = Lines(Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log"));
        CHECK(lines.size() == 3 && lines[0] == "[net] user=<redacted> seen" && lines[1] == "later Zq9Weak spoke");
        CHECK(lines[2].find("filesystem and SystemTime") != std::string::npos);
    }
}

// The maintainer's decision: Game.log is opt-in (sc-offline.exe mcp --game-log).
static void FourthReviewChecks() {
    using namespace sco::launcher::redact;
    const std::string status =
        "<2026-03-01T12:00:01.000Z> [Notice] <AccountLoginCharacterStatus_Character> Character: createdAt 1581606700563 - updatedAt 1581606700999 - "
        "geid 2000998877 - accountId 5544332211 - name Quillon_Vex - state STATE_CURRENT\n";

    // the path and error text of read_log are cleaned with what was learned: a handle in a folder name is hidden there too
    {
        Server s = MakeServer();
        g_log_exists = true; g_log_error.clear(); g_log_older = false; g_heads.clear();
        g_heads["mod"] = "User Login Success - Handle[Zorbax_Quill]\n";
        g_log = "chat Zorbax_Quill\n";
        g_game_path = "D:\\Games\\Zorbax_Quill\\SC\\LIVE\\Game.log";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"game\"}"), "read_log");
        CHECK(Get(o, "path").AsString().find("Zorbax_Quill") == std::string::npos && Get(o, "path").AsString().find("<redacted>") != std::string::npos);
        CHECK(Lines(o) == std::vector<std::string>{"chat <redacted>"});
        g_log_error = "cannot open D:\\Games\\Zorbax_Quill\\SC\\Game.log";
        const Value r = Call(s, "read_log", "{\"log\":\"game\"}");
        const std::string text = Get(Get(Get(r, "result"), "content").Items()[0], "text").AsString();
        CHECK(Get(Get(r, "result"), "isError").AsBool() && text.find("Zorbax_Quill") == std::string::npos && text.find("cannot open") != std::string::npos);
        g_log_error.clear();
        g_game_path = "C:\\Games\\StarCitizen\\LIVE\\Bin64";
    }

    // more id keys: any key that ends in `id` with 9 to 12 digits after it, in both modes
    for (const bool strict : {false, true}) {
        Identities id;
        id.strict = strict;
        const char* hidden[] = {"ownerId:5544332211", "citizenId=5544332211", "citizen_id: 2000998877", "id=2000998877", "characterId 2000998877",
                                "friendid=#2000998877", "geid(2000998877)", "geid-2000998877", "playerGEID<2000998877>", "rsi_id 4433221100",
                                "{\"senderId\":\"2000998877\"}", "recipientId[2000998877]", "ownerid2000998877"};
        for (const char* in : hidden) {
            const std::string out = RedactWith(std::string("x ") + in + " y", id);
            CHECK(out.find("00998877") == std::string::npos && out.find("4433221100") == std::string::npos && out.find("5544332211") == std::string::npos);
            CHECK(RedactWith(out, id) == out);
        }
        const char* kept[] = {"createdAt 1581606700563", "pid=1234", "id=12345678", "valid 12345", "id=1234567890123"};
        for (const char* in : kept) CHECK(RedactWith(in, id) == in);
    }

    // a handle that equals a key word does not stop the key rule on later lines
    for (const bool strict : {false, true}) {
        for (const char* word : {"Account", "Geid", "Handle", "Nickname", "AccountId"}) {
            Identities id;
            id.strict = strict;
            Learn(id, std::string("User Login Success - Handle[") + word + "]\n");
            const std::string out = RedactWith(std::string("a ") + word + "[8877] and geid [4455] and Handle[Qq9] and nickname=Zz9 " + word + " b", id);
            CHECK(out.find("8877") == std::string::npos && out.find("4455") == std::string::npos && out.find("Qq9") == std::string::npos && out.find("Zz9") == std::string::npos);
        }
    }

    // the real login status line: createdAt, updatedAt and state are keys, not names, and are never learned
    for (const bool strict : {false, true}) {
        Identities id;
        id.strict = strict;
        Learn(id, status);
        for (const std::string& v : id.priority) CHECK(v != "createdAt" && v != "updatedAt" && v != "STATE_CURRENT" && v != "state");
        for (const std::string& v : id.learned) CHECK(v != "createdAt" && v != "updatedAt" && v != "STATE_CURRENT" && v != "state");
        const std::string out = RedactWith(status, id);
        CHECK(out.find("createdAt 1581606700563") != std::string::npos && out.find("updatedAt 1581606700999") != std::string::npos);
        CHECK(out.find("state STATE_CURRENT") != std::string::npos);
        CHECK(out.find("Quillon_Vex") == std::string::npos && out.find("2000998877") == std::string::npos && out.find("5544332211") == std::string::npos);
        CHECK(RedactWith("[mod] createdAt=5 state ok updatedAt 7", id) == "[mod] createdAt=5 state ok updatedAt 7");
        if (!strict) CHECK(RedactWith("hi Quillon_Vex", id) == "hi <redacted>");
    }

    // `name=X` fires only for whole context words, and never teaches other lines
    {
        Identities id;
        Learn(id, "[render] CreateHandle ok name=MainPass\n[plugin] handler registered name=Spawner\n[net] players online name=Gateway\n[net] account service reachable name=Gateway2\n"
                  "[loader] plugin loaded name=crew login screen skipped\n");
        CHECK(id.learned.empty() && id.priority.empty());
        const std::string out = RedactWith("[render] CreateHandle ok name=MainPass\n[plugin] handler registered name=Spawner\n[net] account service reachable name=Gateway2\nlater MainPass Spawner Gateway2 crew\n", id);
        CHECK(out == "[render] CreateHandle ok name=MainPass\n[plugin] handler registered name=Spawner\n[net] account service reachable name=<redacted>\nlater MainPass Spawner Gateway2 crew\n");
    }

    // an NPC id in a kill line is taught whole, not as the words Human, Enemy and NPC
    {
        Identities id;
        Learn(id, "<Actor Death> CActor::Kill: 'PU_Human_Enemy_NPC_5555' [2000112233] in zone 'x' killed by 'Zorbax9' [2000998877]\n");
        CHECK(RedactWith("Human Enemy NPC PU_Human_Enemy_NPC_5555", id) == "Human Enemy NPC <redacted>");
    }

    // a flood of fake login lines in one log cannot keep the real handle of another log out: each log has its own quota
    {
        Server s = MakeServer();
        g_log_exists = true; g_log_error.clear(); g_log_older = true; g_heads.clear();
        std::string fakes;
        for (int k = 0; k < 400; ++k) fakes += "User Login Success - Handle[Fake" + std::to_string(100000 + k) + "]\n";
        g_heads["mod"] = fakes;
        g_heads["launcher"] = fakes;
        g_heads["launcher_previous"] = fakes;
        g_heads["game"] = "User Login Success - Handle[Zorbax_Quill]\n";
        g_log = "cut\nchat Zorbax_Quill and Fake100000\n";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\",\"contains\":\"chat\"}"), "read_log");
        CHECK(Lines(o) == std::vector<std::string>{"chat <redacted> and <redacted>"});
        g_heads.clear(); g_log_older = false;
    }

    // too many login lines in one log: the scan stops parsing, and says so
    {
        Server s = MakeServer();
        g_log_exists = true; g_log_error.clear(); g_log_older = true; g_heads.clear();
        std::string dense;
        for (size_t k = 0; k < kMaxScanLoginLines + 10; ++k) dense += "User Login Success - Handle[Same_Handle]\n";
        dense += "User Login Success - Handle[Toolate_Handle]\n";
        g_heads["mod"] = dense;
        g_log = "cut\nchat Toolate_Handle\n";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(!Get(o, "identity_scan_complete").AsBool() && Get(o, "notice").AsString().find("more login lines") != std::string::npos);
        g_heads.clear(); g_log_older = false;
    }
}

static void GameLogChecks() {
    // without the flag: a normal tool error that says how to enable it; the file is not read, not even scanned
    {
        Platform p = MakePlatform();
        p.gameLog = false;
        Server s(p);
        g_log_exists = true; g_log_error.clear(); g_log_older = false;
        g_heads.clear();
        g_heads["game"] = "User Login Success - Handle[Zq9GameOnly]\n";
        g_log = "mod says Zq9GameOnly\n";
        g_read_logs.clear(); g_scanned_logs.clear();
        const Value r = Call(s, "read_log", "{\"log\":\"game\"}");
        CHECK(Get(r, "error").IsNull() && Get(Get(r, "result"), "isError").AsBool());
        const std::string text = Get(Get(Get(r, "result"), "content").Items()[0], "text").AsString();
        CHECK(text.find("--game-log") != std::string::npos && text.find("account names") != std::string::npos);
        CHECK(Get(Get(r, "result"), "structuredContent").IsNull());
        CHECK(g_read_logs.empty() && g_scanned_logs.empty());
        // the other logs still work, and Game.log is still not touched (so its names are not learned either)
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\"}"), "read_log");
        CHECK(Lines(o) == std::vector<std::string>{"mod says Zq9GameOnly"});
        for (const std::string& n : g_read_logs) CHECK(n != "game");
        for (const std::string& n : g_scanned_logs) CHECK(n != "game");
        CHECK(!g_scanned_logs.empty());
        // get_status still reports the folder
        CHECK(Get(Structured(Call(s, "get_status")), "game_folder_found").AsBool());
        g_heads.clear();
    }
    // with the flag: served, scanned, redacted
    {
        Server s = MakeServer();   // the fake platform has gameLog = true
        g_log_exists = true; g_log_error.clear(); g_log_older = false;
        g_heads.clear();
        g_heads["game"] = "User Login Success - Handle[Zq9GameOnly]\n";
        g_log = "chat Zq9GameOnly\n";
        g_read_logs.clear(); g_scanned_logs.clear();
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"game\"}"), "read_log");
        CHECK(Lines(o) == std::vector<std::string>{"chat <redacted>"});
        CHECK(std::find(g_read_logs.begin(), g_read_logs.end(), "game") != g_read_logs.end());
        CHECK(std::find(g_scanned_logs.begin(), g_scanned_logs.end(), "game") != g_scanned_logs.end());
        g_heads.clear();
    }
    // the tool text and the server instructions say so
    {
        Server s = MakeServer();
        const Value list = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"));
        for (const Value& t : Get(Get(list, "result"), "tools").Items())
            if (Get(t, "name").AsString() == "read_log") {
                const std::string d = Get(t, "description").AsString();
                CHECK(d.find("--game-log") != std::string::npos && d.find("account names") != std::string::npos);
                CHECK(d.find("64 MiB") != std::string::npos);
            }
        const Value init = Json(Reply(s, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}"));
        CHECK(Get(Get(init, "result"), "instructions").AsString().find("--game-log") != std::string::npos);
    }
}

// Hostile input stays fast: five hostile 1 MiB lines and a 64 MiB synthetic log, together under 5 seconds.
static void TimingChecks() {
    using namespace sco::launcher::redact;
    const auto t0 = std::chrono::steady_clock::now();
    const size_t sz = 1u << 20;
    std::vector<std::string> hostile(5);
    for (size_t i = 0; i < sz / 7; ++i) hostile[0] += "Handle[";
    hostile[0] = "User Login Success - " + hostile[0];
    for (size_t i = 0; i < sz / 12; ++i) hostile[1] += "nickname=\"x ";
    for (size_t i = 0; i < sz / 2; ++i) hostile[2] += "a@";
    for (size_t i = 0; i < sz / 14; ++i) hostile[3] += "geid 1 name x ";
    for (size_t i = 0; i < sz / 5; ++i) hostile[4] += "GEID[";
    for (const std::string& h : hostile) {   // the redactor itself, strict and relaxed, on a whole 1 MiB line
        for (const bool strict : {false, true}) {
            Identities id;
            id.strict = strict;
            id.accountNames = {"Alex"};
            Learn(id, h);
            CHECK(id.learned.size() <= kMaxLearnedStrict && id.priority.size() <= kMaxPriority);
            CHECK(!RedactWith(h, id).empty());
        }
    }
    // the server: the five hostile lines are the tail of mod.log, and mod.log is 64 MiB with the hostile lines inside
    // it too, thousands of distinct fake login lines after the real one
    std::string file;
    file.reserve(size_t(kMaxScanBytes) + (8u << 20));
    std::string mix = Filler(size_t(kMaxScanBytes) - 6 * sz - (1u << 20));
    file += mix.substr(0, mix.size() / 4);
    file += "User Login Success - Handle[Zorbax9], Nickname[Quillon Vex]\n";
    file += mix.substr(mix.size() / 4, mix.size() / 4);
    for (int k = 0; k < 5000; ++k) file += "User Login Success - Handle[Fake" + std::to_string(100000 + k) + "]\n";
    for (const std::string& h : hostile) { file += h; file += '\n'; }
    file += mix.substr(mix.size() / 2);
    CHECK(file.size() <= size_t(kMaxScanBytes) + 4096);
    Server s = MakeServer();
    g_log_exists = true; g_log_error.clear(); g_log_older = true;
    g_heads.clear();
    g_heads["mod"] = file;
    g_heads["launcher_previous"] = Filler(1u << 20);
    g_scan_chunk = 1u << 20;
    g_log.clear();
    for (const std::string& h : hostile) { g_log += h; g_log += '\n'; }
    g_log += "chat Zorbax9\n";
    const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"mod\",\"contains\":\"chat\"}"), "read_log");
    CHECK(Lines(o) == std::vector<std::string>{"chat <redacted>"});
    g_heads.clear(); g_log_older = false; g_scan_chunk = 4096;
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();   // before the dense logs below are built
    // logs of 64 MiB made only of login lines, plain and with a dozen multi-word keys on each: parsing is capped per
    // log, so one call stays fast whatever the logs hold
    {
        const size_t target = size_t(kMaxScanBytes);
        std::string plain, multi, keys;
        for (size_t k = 0; plain.size() < target; ++k) plain += "User Login Success - Handle[Zorbax_" + std::to_string(k) + "]\n";
        for (int k = 0; k < 12; ++k) keys += " nickname=Aa Bb Cc Dd Ee Ff" + std::to_string(k);
        for (size_t k = 0; multi.size() < target; ++k) multi += "User Login Success - Handle[Zorbax_" + std::to_string(k) + "]" + keys + "\n";
        const auto t1 = std::chrono::steady_clock::now();
        g_heads["mod"] = std::move(plain);
        g_heads["launcher"] = std::move(multi);
        g_heads["launcher_previous"] = g_heads["mod"].substr(0, 8u << 20);
        g_heads["game"] = g_heads["launcher"].substr(0, 8u << 20);
        g_scan_chunk = 1u << 20;
        g_log_older = true;
        g_log = "cut\nchat Zorbax_3\n";
        Server dense = MakeServer();
        for (int call = 0; call < 2; ++call) {   // the second call scans again: still bounded
            const Value& d = Ok(dense, Call(dense, "read_log", "{\"log\":\"mod\"}"), "read_log");
            CHECK(!Get(d, "identity_scan_complete").AsBool() && Get(d, "notice").AsString().find("more login lines") != std::string::npos);
        }
        const double denseSecs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
        std::cout << "launcher mcp: two read_log calls over dense 64 MiB login logs in " << denseSecs << " s\n";
        CHECK(denseSecs < 5.0);
        g_heads.clear(); g_log_older = false; g_scan_chunk = 4096;
    }
    std::cout << "launcher mcp: hostile lines and a " << file.size() / (1u << 20) << " MiB log in " << secs << " s\n";
    CHECK(secs < 5.0);
}

static void FifthReviewChecks() {
    using namespace sco::launcher::redact;

    // e-mail addresses in the other spellings logs and JSON use, and the odd local-part characters
    {
        Identities id;
        const char* mails[] = {"{\"email\":\"bobby@example.com\"}", "bobby\\u0040example.com", "bobby\\x40example.com", "bobby&#64;example.com",
                               "bobby&#x40;example.com", "bobby&commat;example.com", "bobby%40example.com", "o'brien@example.com",
                               "bob!c@example.com", "bobby@my_corp.example.com"};
        for (const char* m : mails) {
            const std::string out = RedactWith(std::string("mail ") + m + " sent", id);
            CHECK(out.find("bobby") == std::string::npos && out.find("brien") == std::string::npos && out.find("bob!") == std::string::npos);
            CHECK(out.find("<redacted>") != std::string::npos);
            CHECK(RedactWith(out, id) == out);
        }
        CHECK(RedactWith("see 100 percent at 5@6", id) == "see 100 percent at 5@6");   // not an address
    }

    // Windows account SIDs
    {
        Identities id;
        const std::string out = RedactWith("HKEY_USERS\\S-1-5-21-1111111111-2222222222-3333333333-1001\\Software and S-1-5-18 and S-1-5-21-", id);
        CHECK(out == "HKEY_USERS\\<redacted>\\Software and S-1-5-18 and S-1-5-21-");
    }

    // a handle that is a piece of a marker, starts with 0x or is a machine word, announced by a login line
    for (const bool strict : {false, true}) {
        for (const char* name : {"Ted", "Red", "Pro", "Use", "0xGamer"}) {
            Identities id;
            id.strict = strict;
            Learn(id, std::string("User Login Success - Handle[") + name + "]\n");
            const std::string out = RedactWith(std::string("chat ") + name + " says hi, mail a@example.com", id);
            CHECK(out.find(name) == std::string::npos);
            CHECK(out.find("<redacted>") != std::string::npos && out.find("says hi") != std::string::npos);
            CHECK(RedactWith(out, id) == out);
        }
    }
    {   // relaxed mode: the 0x handle of an ordinary line is a system handle and stays
        Identities id;
        Learn(id, "x nickname=0x7ff9aa11\n");
        CHECK(RedactWith("see 0x7ff9aa11 here", id) == "see 0x7ff9aa11 here");
    }

    // strict mode: handle shapes have a list of their own, 3000 values do not push the real handle out
    {
        Identities id;
        id.strict = true;
        std::string flood;
        for (int k = 0; k < 3000; ++k) flood += "nickname=\"Npc" + std::to_string(10000 + k) + "\"\n";
        Learn(id, flood);
        Learn(id, "[Notice] Handle[Zorbax9] joined\n");
        CHECK(RedactWith("hello Zorbax9 and Npc10000", id) == "hello <redacted> and <redacted>");
        CHECK(id.legacyDropped == 0);
    }

    // a noisy log cannot starve the real handle, and a full list says so
    {
        g_log_exists = true; g_log_error.clear(); g_log_older = false; g_heads.clear();
        g_game_path = "C:\\Games\\StarCitizen\\LIVE\\Game.log";
        Server s = MakeServer();
        for (int call = 0; call < 6; ++call) {
            std::string fake;
            for (int k = 0; k < 80; ++k) fake += "User Login Success - Handle[Fake" + std::to_string(call) + "x" + std::to_string(1000 + k) + "]\n";
            g_heads["mod"] = fake;
            g_log = "nothing\n";
            Call(s, "read_log", "{\"log\":\"mod\"}");
        }
        g_heads["game"] = "User Login Success - Handle[RealHandle]\n";
        g_log = "chat RealHandle says hi\n";
        const Value& o = Ok(s, Call(s, "read_log", "{\"log\":\"game\"}"), "read_log");
        CHECK(Lines(o) == std::vector<std::string>{"chat <redacted> says hi"});
        g_heads["mod"] = std::string();
        for (int k = 0; k < 80; ++k) g_heads["mod"] += "User Login Success - Handle[Noisy" + std::to_string(1000 + k) + "]\n";
        const Value& n = Ok(s, Call(s, "read_log", "{\"log\":\"game\"}"), "read_log");
        CHECK(!Get(n, "identity_scan_complete").AsBool() && Get(n, "notice").AsString().find("login lines") != std::string::npos);
        CHECK(Lines(n) == std::vector<std::string>{"chat <redacted> says hi"});
        g_heads.clear();
    }

    // the first tool of a session already knows the handles the logs announce: a handle in the game folder name
    {
        g_log_exists = true; g_log_error.clear(); g_log_older = false; g_heads.clear();
        g_heads["mod"] = "User Login Success - Handle[Zorbax_Quill]\n";
        Platform p = MakePlatform();
        p.status = [] {
            StatusInfo st;
            st.gameFolderFound = true;
            st.gameFolder = "D:\\Games\\Zorbax_Quill\\SC\\LIVE\\Bin64";
            return st;
        };
        Server s(p);
        const Value& o = Ok(s, Call(s, "get_status"), "get_status");
        CHECK(Get(o, "game_folder").AsString() == "D:\\Games\\<redacted>\\SC\\LIVE\\Bin64");
        g_heads.clear();
    }
}

int main() {
    ParserChecks();
    LimitChecks();
    RedactChecks();
    IdentityChecks();
    LearnChecks();
    ReviewChecks();
    SecondReviewChecks();
    NegotiationChecks();
    FramingChecks();
    ListChecks();
    ToolChecks();
    LogChecks();
    ToolCallChecks();
    ThirdReviewChecks();
    FourthReviewChecks();
    FifthReviewChecks();
    GameLogChecks();
    TimingChecks();
    std::cout << "launcher mcp: " << checks << " portable checks passed\n";
}
