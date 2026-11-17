// SPDX-License-Identifier: GPL-3.0-only
// Portable checks for the read-only MCP server (launcher/mcp_server.h) over a fake platform: no files, no sockets.
#include "../launcher/mcp_server.h"
#include <cstdlib>
#include <iostream>

using namespace sco::launcher::mcp;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static std::string g_log, g_game_path = "C:\\Games\\StarCitizen\\LIVE\\Bin64";
static bool g_log_older = false, g_log_exists = true;
static std::string g_log_error;
static std::string g_ini = "# comment\r\n\r\ngame = C:\\Users\\Alex\\Games\\SC\r\nchannel = LIVE\r\n[extra]\r\nkey=v\r\n; also a comment\r\nno equals here\r\n";

static Platform MakePlatform() {
    Platform p;
    p.version = "2.4.1-test";
    p.profileRoots = {"C:\\Users\\Alex"};
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
    p.readLog = [](const std::string& log, size_t max) {
        LogRead r;
        r.path = log == "game" ? g_game_path : "C:\\Users\\Alex\\sc\\data\\" + log + ".log";
        r.exists = g_log_exists;
        r.error = g_log_error;
        r.bytes = g_log.size() > max ? g_log.substr(g_log.size() - max) : g_log;
        r.skippedOlder = g_log_older || g_log.size() > max;
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

static void RedactChecks() {
    const std::vector<std::string> roots = {"C:\\Users\\Alex"};
    CHECK(Redact("C:\\Users\\Alex\\AppData\\x", roots) == "%USERPROFILE%\\AppData\\x");
    CHECK(Redact("c:\\users\\ALEX\\x and C:\\USERS\\alex", roots) == "%USERPROFILE%\\x and %USERPROFILE%");
    CHECK(Redact("C:\\Users\\Alexander\\x", roots) == "C:\\Users\\<user>\\x");   // another account, not the profile
    CHECK(Redact("C:\\Users\\Bob\\x", roots) == "C:\\Users\\<user>\\x");
    CHECK(Redact("D:/Users/Bob/x", roots) == "D:/Users/<user>/x");
    CHECK(Redact("d:\\users\\Bob Smith\\x", roots) == "d:\\users\\<user>\\x");
    CHECK(Redact("opened C:\\Users\\Bob and closed", roots) == "opened C:\\Users\\<user> and closed");
    CHECK(Redact("\"C:\\Users\\Bob\" end", roots) == "\"C:\\Users\\<user>\" end");
    CHECK(Redact("C:\\Users\\Bob", roots) == "C:\\Users\\<user>");
    CHECK(Redact("C:\\Users\\", roots) == "C:\\Users\\");
    CHECK(Redact("C:\\Users\\<user>\\x", roots) == "C:\\Users\\<user>\\x");   // idempotent
    CHECK(Redact(Redact("C:\\Users\\Bob\\x", roots), roots) == "C:\\Users\\<user>\\x");
    CHECK(Redact("C:\\Program Files\\x", roots) == "C:\\Program Files\\x");
    CHECK(Redact("C:\\Users\\Alex", {"C:\\Users\\Alex\\"}) == "%USERPROFILE%");   // a trailing slash on the root
    CHECK(Redact("C:\\Users\\Alex\\x", {}) == "C:\\Users\\<user>\\x");
    CHECK(Redact("two C:\\Users\\A\\x C:\\Users\\B\\y", {}) == "two C:\\Users\\<user>\\x C:\\Users\\<user>\\y");
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

int main() {
    ParserChecks();
    RedactChecks();
    NegotiationChecks();
    FramingChecks();
    ListChecks();
    ToolChecks();
    LogChecks();
    ToolCallChecks();
    std::cout << "launcher mcp: " << checks << " portable checks passed\n";
}
