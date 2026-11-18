// SPDX-License-Identifier: GPL-3.0-only
// Vector dump for the read-only MCP server and the shared redactor. It runs the real C++ code
// (launcher/mcp_server.h, launcher/redact.h) and writes what that code does as one JSON file that the Rust crates
// scol-redact and scol-mcp (launcher-rs/crates) must reproduce byte for byte. Nothing here runs in game.
//
// Two sources feed the file:
//   1. Every Redact and RedactWith call that tools/launcher_mcp_test.cpp makes while it runs (its whole check suite is
//      included below with those two functions wrapped, so the dump also proves the C++ checks pass), plus its
//      RedactIdentityKeys table.
//   2. launcher-rs/tests/mcp-vectors/scenarios.json: redaction cases with learned values, MCP sessions (requests over a
//      fake platform) and stdio framing cases. This file holds only inputs; the dump adds the expected results.
//
//   launcher_mcp_dump <scenarios.json> <vectors.json>           write the vectors
//   launcher_mcp_dump --check <scenarios.json> <vectors.json>   regenerate and require the same bytes
//
// Text in the JSON is a string, {"hex":"..."} (bytes that are not valid UTF-8), {"repeat":{"unit":T,"count":N}},
// {"lines":{"template":"... {n} ...","from":A,"to":B}} (one line per n) or an array of those (concatenated).
#include "../../launcher/mcp_server.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace dump {

namespace mcp = sco::launcher::mcp;
namespace red = sco::launcher::redact;
using mcp::Value;

[[noreturn]] void Die(const std::string& why) {
    std::cerr << "launcher_mcp_dump: " << why << "\n";
    std::exit(1);
}

// ---- JSON helpers ------------------------------------------------------------------------------------------------

bool IsValidUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const size_t n = mcp::Utf8SequenceLength(s, i);
        if (n == 0) return false;
        i += n;
    }
    return true;
}

std::string Hex(const std::string& s) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) { out += d[c >> 4]; out += d[c & 15]; }
    return out;
}

// A byte string as JSON: plain when it is valid UTF-8, else {"hex":"..."}.
Value Bytes(const std::string& s) {
    if (IsValidUtf8(s)) return Value::Str(s);
    Value o = Value::Object();
    o.Set("hex", Value::Str(Hex(s)));
    return o;
}

Value BytesList(const std::vector<std::string>& v) {
    Value a = Value::Array();
    for (const std::string& s : v) a.Push(Bytes(s));
    return a;
}

std::string FromHex(const std::string& h) {
    if (h.size() % 2) Die("odd hex string");
    const auto nib = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        Die("bad hex digit");
    };
    std::string out;
    for (size_t i = 0; i < h.size(); i += 2) out += static_cast<char>(nib(h[i]) * 16 + nib(h[i + 1]));
    return out;
}

std::string Text(const Value& v);

std::string ReplaceAll(std::string s, const std::string& what, const std::string& with) {
    for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + with.size())) s.replace(p, what.size(), with);
    return s;
}

int64_t Int(const Value& v) {
    int64_t n = 0;
    if (!v.AsInteger(n)) Die("expected an integer");
    return n;
}

// Resolves a text value (see the top of this file).
std::string Text(const Value& v) {
    if (v.IsString()) return v.AsString();
    if (v.IsArray()) {
        std::string out;
        for (const Value& item : v.Items()) out += Text(item);
        return out;
    }
    if (v.IsObject()) {
        if (const Value* h = v.Find("hex")) return FromHex(h->AsString());
        if (const Value* r = v.Find("repeat")) {
            const Value* unit = r->Find("unit");
            const Value* count = r->Find("count");
            if (!unit || !count) Die("repeat needs unit and count");
            const std::string u = Text(*unit);
            std::string out;
            for (int64_t i = 0, n = Int(*count); i < n; ++i) out += u;
            return out;
        }
        if (const Value* l = v.Find("lines")) {
            const Value* tmpl = l->Find("template");
            const Value* from = l->Find("from");
            const Value* to = l->Find("to");
            if (!tmpl || !from || !to) Die("lines needs template, from and to");
            const std::string t = Text(*tmpl);
            std::string out;
            for (int64_t n = Int(*from); n <= Int(*to); ++n) out += ReplaceAll(t, "{n}", std::to_string(n)) + "\n";
            return out;
        }
    }
    Die("not a text value");
}

const Value* Opt(const Value& o, const char* key) { return o.Find(key); }

std::string StrOr(const Value& o, const char* key, const std::string& dflt = "") {
    const Value* v = o.Find(key);
    return v ? Text(*v) : dflt;
}

bool BoolOr(const Value& o, const char* key, bool dflt) {
    const Value* v = o.Find(key);
    if (!v) return dflt;
    if (!v->IsBool()) Die(std::string(key) + " must be true or false");
    return v->AsBool();
}

int64_t IntOr(const Value& o, const char* key, int64_t dflt) {
    const Value* v = o.Find(key);
    return v ? Int(*v) : dflt;
}

std::vector<std::string> StrList(const Value* v) {
    std::vector<std::string> out;
    if (!v) return out;
    if (!v->IsArray()) Die("expected an array of text");
    for (const Value& item : v->Items()) out.push_back(Text(item));
    return out;
}

// ---- the fake platform ------------------------------------------------------------------------------------------

struct LogSpec {
    std::string path, error, text;
    bool exists = true, older = false;
};

// What the fake platform serves. The lambdas of Platform read it through a shared pointer, so an `update` step can
// change it between requests.
struct Spec {
    bool hasStatus = false, hasPlugins = false, hasSettings = false;
    mcp::StatusInfo status;
    std::vector<mcp::PluginFolder> plugins;
    std::map<std::string, LogSpec> logs;
    std::map<std::string, std::string> heads, scanError;
    std::map<std::string, uint64_t> headSize;
    size_t chunk = 4096;
    mcp::IniRead settings;
};

// The keys an update step may replace: the ones the platform's functions read at call time.
const char* const kUpdatable[] = {"status", "plugins", "logs", "heads", "head_size", "scan_error", "scan_chunk", "settings"};

Spec LoadSpec(const Value& j) {
    Spec s;
    if (const Value* st = Opt(j, "status")) {
        s.hasStatus = true;
        s.status.launcherVersion = StrOr(*st, "launcher_version");
        s.status.gameFolderFound = BoolOr(*st, "game_folder_found", false);
        s.status.gameFolder = StrOr(*st, "game_folder");
        s.status.gameBuild = StrOr(*st, "game_build");
        s.status.modInstalled = BoolOr(*st, "mod_installed", false);
        s.status.eac = StrOr(*st, "eac");
        s.status.networkBlocked = BoolOr(*st, "network_blocked", false);
        s.status.eacRename = BoolOr(*st, "eac_rename", false);
        s.status.pluginsSettingOn = BoolOr(*st, "plugins_setting_on", true);
        s.status.pluginsEnabled = static_cast<int>(IntOr(*st, "plugins_enabled", 0));
        s.status.pluginsTotal = static_cast<int>(IntOr(*st, "plugins_total", 0));
        s.status.pcChangesLeft = StrOr(*st, "pc_changes_left");
        s.status.wine = BoolOr(*st, "wine", false);
        s.status.gameRunning = BoolOr(*st, "game_running", false);
    }
    if (const Value* pl = Opt(j, "plugins")) {
        s.hasPlugins = true;
        for (const Value& item : pl->Items()) {
            const int64_t count = IntOr(item, "count", 0);
            const int64_t width = IntOr(item, "width", 1);
            const auto number = [&](int64_t n) {
                std::string t = std::to_string(n);
                while (static_cast<int64_t>(t.size()) < width) t = "0" + t;
                return t;
            };
            const auto make = [&](const std::string& n) {
                mcp::PluginFolder f;
                f.folder = ReplaceAll(StrOr(item, "folder"), "{n}", n);
                f.iniText = ReplaceAll(StrOr(item, "ini"), "{n}", n);
                f.disabled = BoolOr(item, "disabled", false);
                return f;
            };
            if (count > 0) for (int64_t n = 1; n <= count; ++n) s.plugins.push_back(make(number(n)));
            else s.plugins.push_back(make(""));
        }
    }
    if (const Value* lg = Opt(j, "logs")) {
        for (const auto& m : lg->Members()) {
            LogSpec l;
            l.path = StrOr(m.second, "path");
            l.error = StrOr(m.second, "error");
            l.text = StrOr(m.second, "text");
            l.exists = BoolOr(m.second, "exists", true);
            l.older = BoolOr(m.second, "older", false);
            s.logs[m.first] = l;
        }
    }
    if (const Value* hd = Opt(j, "heads")) for (const auto& m : hd->Members()) s.heads[m.first] = Text(m.second);
    if (const Value* hs = Opt(j, "head_size")) for (const auto& m : hs->Members()) s.headSize[m.first] = static_cast<uint64_t>(Int(m.second));
    if (const Value* se = Opt(j, "scan_error")) for (const auto& m : se->Members()) s.scanError[m.first] = Text(m.second);
    s.chunk = static_cast<size_t>(IntOr(j, "scan_chunk", 4096));
    if (s.chunk == 0) Die("scan_chunk must be positive");
    if (const Value* st = Opt(j, "settings")) {
        s.hasSettings = true;
        s.settings.path = StrOr(*st, "path");
        s.settings.exists = BoolOr(*st, "exists", true);
        s.settings.error = StrOr(*st, "error");
        s.settings.text = StrOr(*st, "text");
    }
    return s;
}

mcp::Platform MakePlatform(const Value& j, const std::shared_ptr<Spec>& spec) {
    mcp::Platform p;
    p.version = StrOr(j, "version", "2.4.1-test");
    p.profileRoots = StrList(Opt(j, "profile_roots"));
    p.accountNames = StrList(Opt(j, "account_names"));
    p.gameLog = BoolOr(j, "game_log", false);
    if (spec->hasStatus) p.status = [spec] { return spec->status; };
    if (spec->hasPlugins) p.plugins = [spec] { return spec->plugins; };
    p.readLog = [spec](const std::string& log, size_t max) {
        mcp::LogRead r;
        const auto it = spec->logs.find(log);
        if (it == spec->logs.end()) return r;
        const LogSpec& l = it->second;
        r.path = l.path;
        r.exists = l.exists;
        r.error = l.error;
        r.bytes = l.text.size() > max ? l.text.substr(l.text.size() - max) : l.text;
        r.skippedOlder = l.older || l.text.size() > max;
        return r;
    };
    p.scanLog = [spec](const std::string& log, uint64_t max, const mcp::ScanSink& sink) {
        mcp::LogScan r;
        if (const auto e = spec->scanError.find(log); e != spec->scanError.end()) {
            r.exists = true;
            r.error = e->second;
            return r;
        }
        const auto it = spec->heads.find(log);
        if (it == spec->heads.end()) return r;
        r.exists = true;
        const std::string& text = it->second;
        r.size = text.size();
        if (const auto z = spec->headSize.find(log); z != spec->headSize.end()) r.size = z->second;
        while (r.scanned < text.size() && r.scanned < max) {
            const size_t n = static_cast<size_t>(std::min<uint64_t>({spec->chunk, text.size() - r.scanned, max - r.scanned}));
            sink(text.data() + r.scanned, n);
            r.scanned += n;
        }
        return r;
    };
    if (spec->hasSettings) p.settings = [spec] { return spec->settings; };
    return p;
}

// ---- scenarios ---------------------------------------------------------------------------------------------------

Value RunSession(const Value& sc) {
    const Value* pj = sc.Find("platform");
    Value platform = pj ? *pj : Value::Object();
    auto spec = std::make_shared<Spec>(LoadSpec(platform));
    mcp::Server server(MakePlatform(platform, spec));
    Value out = Value::Object(), steps = Value::Array();
    out.Set("name", *sc.Find("name"));
    out.Set("platform", platform);
    const Value* requests = sc.Find("steps");
    if (!requests) Die("a session needs steps");
    for (const Value& step : requests->Items()) {
        Value o = Value::Object();
        if (const Value* up = step.Find("update")) {
            for (const auto& m : up->Members()) {
                bool known = false;
                for (const char* k : kUpdatable) known = known || m.first == k;
                if (!known) Die("an update step cannot change '" + m.first + "'");
                platform.Set(m.first, m.second);
            }
            *spec = LoadSpec(platform);
            o.Set("update", *up);
        } else {
            const Value* req = step.Find("request");
            if (!req) Die("a step needs request or update");
            std::string reply;
            const bool got = server.HandleLine(Text(*req), reply);
            o.Set("request", *req);
            o.Set("reply", got ? Bytes(reply) : Value());
        }
        steps.Push(std::move(o));
    }
    out.Set("steps", std::move(steps));
    return out;
}

Value RunServe(const Value& sc) {
    const Value* pj = sc.Find("platform");
    const Value platform = pj ? *pj : Value::Object();
    auto spec = std::make_shared<Spec>(LoadSpec(platform));
    mcp::Server server(MakePlatform(platform, spec));
    const std::string input = Text(*sc.Find("input"));
    const size_t chunk = static_cast<size_t>(IntOr(sc, "chunk", 65536));
    const int64_t stopAfter = IntOr(sc, "stop_after", -1);
    size_t at = 0;
    std::vector<std::string> replies;
    mcp::Serve(server,
        [&](char* buf, size_t n) {
            const size_t take = std::min({chunk, n, input.size() - at});
            std::memcpy(buf, input.data() + at, take);
            at += take;
            return take;
        },
        [&](const std::string& line) {
            replies.push_back(line);
            return stopAfter < 0 || static_cast<int64_t>(replies.size()) < stopAfter;
        });
    Value out = Value::Object();
    out.Set("name", *sc.Find("name"));
    out.Set("platform", platform);
    out.Set("chunk", Value::Int(static_cast<int64_t>(chunk)));
    out.Set("input", *sc.Find("input"));
    if (stopAfter >= 0) out.Set("stop_after", Value::Int(stopAfter));
    out.Set("output", BytesList(replies));
    return out;
}

// A redaction case: identities (from `learn` texts, or given directly as `lists`), then every input redacted.
Value RunRedact(const Value& sc) {
    red::Identities id;
    id.strict = BoolOr(sc, "strict", false);
    id.profileRoots = StrList(Opt(sc, "roots"));
    id.accountNames = StrList(Opt(sc, "names"));
    const Value* learn = Opt(sc, "learn");
    if (const Value* lists = Opt(sc, "lists")) {
        id.priority = StrList(Opt(*lists, "priority"));
        id.learned = StrList(Opt(*lists, "learned"));
        id.legacy = StrList(Opt(*lists, "legacy"));
    }
    if (learn) for (const Value& t : learn->Items()) red::Learn(id, Text(t));
    std::vector<std::string> outputs;
    const Value* inputs = sc.Find("inputs");
    if (!inputs) Die("a redaction case needs inputs");
    for (const Value& t : inputs->Items()) outputs.push_back(red::RedactWith(Text(t), id));
    Value out = sc;
    Value expected = Value::Object();
    expected.Set("outputs", BytesList(outputs));
    if (learn) {
        expected.Set("priority", BytesList(id.priority));
        expected.Set("learned", BytesList(id.learned));
        expected.Set("legacy", BytesList(id.legacy));
        expected.Set("priority_dropped", Value::Int(static_cast<int64_t>(id.priorityDropped)));
        expected.Set("legacy_dropped", Value::Int(static_cast<int64_t>(id.legacyDropped)));
    }
    out.Set("expected", std::move(expected));
    return out;
}

// ---- what tools/launcher_mcp_test.cpp passes to Redact and RedactWith -----------------------------------------------

struct Recorded {
    std::vector<Value> cases;
    std::set<std::string> seen;
    void Add(Value c) {
        const std::string key = mcp::Serialize(c);
        if (key.size() > (24u << 10) || !seen.insert(key).second) return;
        c.Set("name", Value::Str("recorded " + std::to_string(cases.size() + 1)));
        cases.push_back(std::move(c));
    }
};
Recorded g_rec;

Value RecordCase(const red::Identities& id, const std::string& input, const std::string& output) {
    Value c = Value::Object(), lists = Value::Object(), expected = Value::Object();
    if (id.strict) c.Set("strict", Value::Bool(true));
    c.Set("roots", BytesList(id.profileRoots));
    c.Set("names", BytesList(id.accountNames));
    if (!id.priority.empty() || !id.learned.empty() || !id.legacy.empty()) {
        lists.Set("priority", BytesList(id.priority));
        lists.Set("learned", BytesList(id.learned));
        lists.Set("legacy", BytesList(id.legacy));
        c.Set("lists", std::move(lists));
    }
    Value inputs = Value::Array();
    inputs.Push(Bytes(input));
    c.Set("inputs", std::move(inputs));
    expected.Set("outputs", BytesList({output}));
    c.Set("expected", std::move(expected));
    return c;
}

std::string RecRedact(const std::string& text, const std::vector<std::string>& roots, const std::vector<std::string>& names = {}) {
    red::Identities id;
    id.profileRoots = roots;
    id.accountNames = names;
    const std::string out = red::RedactWith(text, id);
    g_rec.Add(RecordCase(id, text, out));
    return out;
}

std::string RecRedactWith(const std::string& text, const red::Identities& id) {
    const std::string out = red::RedactWith(text, id);
    g_rec.Add(RecordCase(id, text, out));
    return out;
}

std::string RecIdentityKeys(const std::string& text) {
    const std::string out = red::RedactIdentityKeys(text);
    Value c = Value::Object(), expected = Value::Object();
    c.Set("key_pass", Value::Bool(true));
    Value inputs = Value::Array();
    inputs.Push(Bytes(text));
    c.Set("inputs", std::move(inputs));
    expected.Set("outputs", BytesList({out}));
    c.Set("expected", std::move(expected));
    g_rec.Add(std::move(c));
    return out;
}

} // namespace dump

// The whole check suite of the C++ tests runs with Redact, RedactWith and RedactIdentityKeys recording their calls.
#define Redact(...) dump::RecRedact(__VA_ARGS__)
#define RedactWith(...) dump::RecRedactWith(__VA_ARGS__)
#define RedactIdentityKeys(...) dump::RecIdentityKeys(__VA_ARGS__)
// launcher_mcp_test_recorded.inc is tools/launcher_mcp_test.cpp with its main() renamed sco_mcp_test_main and a return
// added (CMakeLists.txt writes it at configure time), so the checks run inside this program.
#include "launcher_mcp_test_recorded.inc"
#undef Redact
#undef RedactWith
#undef RedactIdentityKeys

namespace dump {

std::string ReadFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) Die(std::string("cannot read ") + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void Section(std::string& out, const char* name, const std::vector<Value>& items, bool last) {
    out += std::string("\"") + name + "\": [\n";
    for (size_t i = 0; i < items.size(); ++i) out += mcp::Serialize(items[i]) + (i + 1 < items.size() ? ",\n" : "\n");
    out += last ? "]\n" : "],\n";
}

std::string Build(const std::string& scenariosText) {
    const mcp::ParseResult p = mcp::Parse(scenariosText);
    if (!p.ok) Die("scenarios.json: " + p.error);
    const Value& sc = p.value;
    std::vector<Value> redact = g_rec.cases, sessions, serves;
    if (const Value* r = sc.Find("redact"))
        for (const Value& c : r->Items()) redact.push_back(RunRedact(c));
    if (const Value* s = sc.Find("sessions"))
        for (const Value& c : s->Items()) sessions.push_back(RunSession(c));
    if (const Value* s = sc.Find("serve"))
        for (const Value& c : s->Items()) serves.push_back(RunServe(c));
    std::string out = "{\n\"format\": 1,\n\"generator\": \"tools/launcher-mcp/dump.cpp (C++ launcher/mcp_server.h and launcher/redact.h)\",\n";
    Section(out, "redact", redact, false);
    Section(out, "sessions", sessions, false);
    Section(out, "serve", serves, true);
    out += "}\n";
    return out;
}

std::string Lf(const std::string& s) {
    std::string out;
    for (char c : s) if (c != '\r') out += c;
    return out;
}

} // namespace dump

int main(int argc, char** argv) {
    using namespace dump;
    const bool check = argc == 4 && std::string(argv[1]) == "--check";
    if (!(check || argc == 3)) Die("usage: launcher_mcp_dump [--check] <scenarios.json> <vectors.json>");
    const char* scenarios = argv[check ? 2 : 1];
    const char* target = argv[check ? 3 : 2];
    sco_mcp_test_main();   // the C++ checks, recording their redaction calls
    const std::string built = Build(ReadFile(scenarios));
    if (check) {
        if (Lf(ReadFile(target)) != built) {
            std::cerr << "launcher_mcp_dump: " << target << " differs from what the C++ code gives now; run the dump to rewrite it\n";
            return 1;
        }
        std::cout << "launcher mcp vectors: " << target << " matches the C++ code\n";
        return 0;
    }
    std::ofstream out(target, std::ios::binary);
    out << built;
    if (!out) Die(std::string("cannot write ") + target);
    std::cout << "launcher mcp vectors: wrote " << target << " (" << built.size() << " bytes)\n";
    return 0;
}
