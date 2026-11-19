// SPDX-License-Identifier: GPL-3.0-only
// Launcher contract dump. Compiles the real launcher/launcher.cpp (as launcher_status_config_test does) and writes
// the results of its own functions as golden files: sc-offline.ini parse results, the in-place ini edits, the
// SC_OFFLINE_* environment block, the capability (exp_*) switches and the environment they leave, the install marker, the pc-changes.txt record, plugin.ini and [settings] parse
// results and the plugin settings store. launcher-rs/tests/golden holds the committed copy; the Rust crates must
// reproduce every file byte for byte. tools/check_launcher_contract.py runs this and diffs against that copy.
// No helper, game or network call runs. Nothing here is run in game.
#define wmain sco_launcher_unused_entrypoint
#include "../../launcher/launcher.cpp"
#undef wmain
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace fs = std::filesystem;
namespace ct = sco::launcher::contract;

static fs::path g_out, g_scratch;
static unsigned g_files = 0;

[[noreturn]] static void Die(const std::string& why) { std::cerr << "launcher_contract_dump: " << why << "\n"; std::exit(1); }

static void PutBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << bytes;
    if (!out) Die("cannot write " + path.string());
    ++g_files;
}
static void Put(const std::string& rel, const std::string& bytes) { PutBytes(g_out / rel, bytes); }

static std::string Slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) Die("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Raw string literals take their line endings from the source file, so every multi-line literal goes through
// Lf or Crlf, which make the ending explicit.
static std::string Lf(const std::string& s) {
    std::string o;
    for (char c : s) if (c != '\r') o += c;
    return o;
}

static std::string Crlf(const std::string& s) {
    std::string o;
    for (char c : Lf(s)) { if (c == '\n') o += "\r\n"; else o += c; }
    return o;
}

static const char* OnOff(bool b) { return b ? "on" : "off"; }

static const char* LanguageWord(sco::launcher::locale::Language l) {
    using sco::launcher::locale::Language;
    return l == Language::Auto ? "auto" : l == Language::Russian ? "ru" : "en";
}

// --- sc-offline.ini -----------------------------------------------------------------------------

static std::string ConfigDump(const Config& c, bool returned, const std::string& messages) {
    std::string t;
    t += std::string("returned=") + (returned ? "true" : "false") + "\n";
    t += "game=" + Narrow(c.game) + "\n";
    t += std::string("language=") + LanguageWord(c.language) + "\n";
    t += "channel=" + Narrow(c.channel) + "\n";
    t += "boot_map=" + Narrow(c.bootMap) + "\n";
    t += "start_ship=" + Narrow(c.startShip) + "\n";
    t += "start=" + Narrow(c.start) + "\n";
    t += "registry_probe=" + Narrow(c.registryProbe) + "\n";
    t += std::string("block_network=") + OnOff(c.firewall) + "\n";
    t += std::string("eac_hosts=") + OnOff(c.eacHosts) + "\n";
    t += std::string("eac_rename=") + OnOff(c.eacRename) + "\n";
    t += std::string("clean_logs=") + CleanLogsWord(c.cleanLogs) + "\n";
    t += std::string("check_updates=") + OnOff(c.checkUpdates) + "\n";
    t += std::string("update_channel=") + (c.prereleases ? "prerelease" : "stable") + "\n";
    t += std::string("crash_reports=") + OnOff(c.crashReports) + "\n";
    t += std::string("discord_off=") + (c.discordOff ? "true" : "false") + "\n";
    t += std::string("plugins=") + OnOff(c.plugins) + "\n";
    t += std::string("relocation=") + OnOff(c.relocation) + "\n";
    t += std::string("multiplayer=") + OnOff(c.multiplayer) + "\n";
    t += "multiplayer_allow=" + Narrow(c.multiplayerAllow) + "\n";
    t += std::string("asop=") + OnOff(c.asop) + "\n";
    t += "asop_fleet_list=" + Narrow(c.asopFleetList) + "\n";
    t += "menu=" + Narrow(c.menu) + "\n";
    for (size_t i = 0; i < ARRAYSIZE(kExperimental); ++i)
        t += Narrow(kExperimental[i].key) + "=" + OnOff(c.experimental[i]) + "\n";
    for (size_t pos = 0; pos < messages.size();) {
        size_t nl = messages.find('\n', pos);
        if (nl == std::string::npos) nl = messages.size();
        t += "message=" + messages.substr(pos, nl - pos) + "\n";
        pos = nl + 1;
    }
    return t;
}

// The environment block `play` sets, one NAME=value line each; an empty value means the variable is unset.
static std::string EnvDump(const Config& c) {
    ct::PlayEnvInput in;
    in.bootMap = c.bootMap; in.start = c.start; in.startShip = c.startShip;
    in.multiplayerAllow = c.multiplayerAllow; in.asopFleetList = c.asopFleetList; in.registryProbe = c.registryProbe; in.menu = c.menu;
    in.data = L"C:\\sc-offline\\data";
    in.userDir = L"C:\\Games\\StarCitizen\\LIVE\\user\\client\\0";
    in.plugins = c.plugins; in.relocation = c.relocation; in.multiplayer = c.multiplayer; in.asop = c.asop;
    std::string t;
    for (const auto& kv : ct::PlayEnv(in)) t += Narrow(kv.first) + "=" + (kv.second.empty() ? "<unset>" : Narrow(kv.second)) + "\n";
    return t;
}

// The capability switches through the real ApplyExperimentalEnv: first every switch variable is set to the given
// inherited value (empty = set to the empty string), then the function runs and the process environment is read
// back. A line per switch: NAME=1 when its key is on, NAME=<unset> when it is off. Then the "on" summary the launcher
// prints and the variables it reports as removed (non-empty although the key is off).
static std::string CapsDump(const Config& c, const std::map<std::string, std::string>& inherited) {
    for (const auto& s : kExperimental) SetEnvironmentVariableW(s.var, nullptr);
    for (const auto& kv : inherited) {
        std::wstring name(kv.first.begin(), kv.first.end()), value(kv.second.begin(), kv.second.end());
        SetEnvironmentVariableW(name.c_str(), value.c_str());
    }
    std::wstring removed;
    const std::wstring on = ApplyExperimentalEnv(c, removed);
    std::string t;
    for (const auto& s : kExperimental) {
        wchar_t buf[64] = {};
        SetLastError(0);
        const DWORD n = GetEnvironmentVariableW(s.var, buf, ARRAYSIZE(buf));
        const bool set = n > 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
        t += Narrow(s.var) + "=" + (set ? Narrow(buf) : std::string("<unset>")) + "\n";
    }
    t += "on=" + Narrow(on) + "\n";
    t += "removed=" + Narrow(removed) + "\n";
    for (const auto& s : kExperimental) SetEnvironmentVariableW(s.var, nullptr);
    return t;
}

static std::string RunReadConfig(const fs::path& file, Config& c, bool& returned) {
    const fs::path logPath = g_scratch / "x.log";
    g_log = _wfsopen(logPath.c_str(), L"w+b", _SH_DENYNO);   // binary: the messages end in \n, not \r\n
    if (!g_log) Die("cannot open the scratch log");
    returned = ReadConfig(file.wstring(), c);
    std::fflush(g_log);
    std::fclose(g_log);
    g_log = nullptr;
    return Slurp(logPath);
}

static void ParseCase(const std::string& name, const std::string& bytes) {
    const fs::path file = g_scratch / "x.ini";
    PutBytes(file, bytes); --g_files;
    Config c;
    bool returned = false;
    const std::string messages = RunReadConfig(file, c, returned);
    Put("ini/" + name + ".ini", bytes);
    Put("ini/" + name + ".parsed.txt", ConfigDump(c, returned, messages));
    if (returned) {
        Put("ini/" + name + ".env.txt", EnvDump(c));
        // Every switch variable is inherited as "yes" here: an on key must replace it with 1, an off key must remove it.
        std::map<std::string, std::string> inherited;
        for (const auto& s : kExperimental) inherited[Narrow(s.var)] = "yes";
        Put("ini/" + name + ".caps.txt", CapsDump(c, inherited));
    }
}

static void MissingParseCase() {
    Config c;
    bool returned = true;
    const std::string messages = RunReadConfig(g_scratch / "does-not-exist.ini", c, returned);
    Put("ini/missing-file.parsed.txt", ConfigDump(c, returned, messages));
}

static void IniParseCases(const fs::path& repo) {
    std::vector<std::pair<std::string, std::string>> cases;
    cases.push_back({ "empty", "" });
    cases.push_back({ "shipped", Crlf(Slurp(repo / "launcher" / "sc-offline.ini")) });
    const std::string everyKey = Lf(R"(game = D:\Games\Roberts Space Industries
language = ru
channel = PTU
boot_map = PU
start_ship = ANVL_Carrack
start = Daymar
block_network = off
eac_hosts = no
eac_rename = 0
crash_reports = false
discord_presence = off
plugins = off
relocation = off
multiplayer = off
multiplayer_allow = 100.64.0.0/10, 192.168.0.0/16
asop = off
asop_fleet_list = game
mining = on
mining_debug = on
check_updates = off
update_channel = prerelease
clean_logs = always
registry_probe = 1
)");
    cases.push_back({ "every-key-crlf", Crlf(everyKey) });
    cases.push_back({ "every-key-lf", everyKey });
    cases.push_back({ "on-off-spellings", Crlf(R"(plugins = ON
relocation = Yes
multiplayer = TRUE
asop = 1
block_network = No
eac_hosts = FALSE
eac_rename = 0
crash_reports = oFf
check_updates = on
)") });
    cases.push_back({ "uppercase-keys", Crlf(R"(GAME = D:\Upper
Plugins = off
CLEAN_LOGS = NEVER
Update_Channel = PRERELEASE
Asop_Fleet_List = GAME
BLOCK_NETWORK = off
)") });
    cases.push_back({ "bom-crlf", "\xEF\xBB\xBF" + Crlf("plugins = off\nasop = off\n") });
    cases.push_back({ "bom-lf", "\xEF\xBB\xBF" "plugins = off\nasop = off\n" });
    cases.push_back({ "mixed-endings", "plugins = off\r\nasop = off\nrelocation = off\r\n" });
    cases.push_back({ "no-trailing-newline", "plugins = off\r\nasop = off" });
    cases.push_back({ "comments", Crlf(R"(# comment
   # indented comment
game = D:\Games#One\StarCitizen # not a comment
plugins = off
; semicolon line
; asop = off
#plugins = on
)") });
    cases.push_back({ "unknown-keys", Crlf("foo = bar\nanother_key=1\nplugins = off\nplugins_x = on\n") });
    cases.push_back({ "malformed-values", Crlf(R"(block_network = maybe
eac_hosts = perhaps
eac_rename = 2
crash_reports = sometimes
plugins = enabled
relocation = nope
multiplayer = ?
asop = 11
check_updates = auto
language = fr
clean_logs = sometimes
update_channel = beta
asop_fleet_list = both
)") });
    cases.push_back({ "no-equals-and-empty-key", Crlf("just text\n= value\n   \nplugins\n  = off\nasop = off\n") });
    cases.push_back({ "quotes", Crlf("game = \"D:\\Games\\Star Citizen\"\nboot_map = \"PU\"\nstart = \"\"\"a\"\"\"\nstart_ship = \"x y\n") });
    cases.push_back({ "duplicates", Crlf("plugins = off\nplugins = on\nchannel = PTU\nchannel =\nlanguage = ru\nlanguage = en\n") });
    cases.push_back({ "equals-in-value", Crlf("game = D:\\a=b\nboot_map = a = b\nstart = x==y\n") });
    cases.push_back({ "tabs-and-spaces", "\tplugins\t=\toff\t\r\n  asop   =   off   \r\n" });
    cases.push_back({ "mining-legacy", Crlf("mining = on\nmining_debug = 1\nplugins = off\n") });
    cases.push_back({ "discord-legacy-off", Crlf("discord_presence = off\n") });
    cases.push_back({ "discord-legacy-on", Crlf("discord_presence = off\ndiscord_presence = on\n") });
    cases.push_back({ "discord-legacy-bad", Crlf("discord_presence = off\ndiscord_presence = maybe\n") });
    cases.push_back({ "game-nonascii", Crlf("game = D:\\Spiele\\Sch\xC3\x9C" "ff\\StarCitizen\nstart = Caf\xC3\xA9\n") });
    cases.push_back({ "registry-probe", Crlf("registry_probe = 1\nstart = Daymar\n") });
    cases.push_back({ "language-en", "language = en\r\n" });
    cases.push_back({ "language-ru", "language = ru\r\n" });
    cases.push_back({ "language-auto", "language = auto\r\n" });
    cases.push_back({ "language-upper", "language = RU\r\n" });
    cases.push_back({ "language-empty", "language =\r\n" });
    // The in-game menu key: each value, spelling, malformed, duplicates.
    cases.push_back({ "menu-rust", "menu = rust\r\n" });
    cases.push_back({ "menu-imgui", "menu = imgui\r\n" });
    cases.push_back({ "menu-upper", "menu = RUST\r\n" });
    cases.push_back({ "menu-bad", "menu = egui\r\n" });
    cases.push_back({ "menu-empty", "menu =\r\n" });
    cases.push_back({ "menu-bad-after-rust", "menu = rust\r\nmenu = nope\r\n" });
    cases.push_back({ "menu-rust-then-imgui", "menu = rust\r\nmenu = imgui\r\n" });
    // The capability switches (exp_* keys): defaults, each key on and off, spellings, malformed, duplicates.
    for (const auto& s : kExperimental) {
        const std::string key = Narrow(s.key);
        cases.push_back({ key + "-off", Crlf(key + " = off\n") });
        cases.push_back({ key + "-on", Crlf(key + " = on\n") });
        cases.push_back({ key + "-malformed", Crlf(key + " = maybe\n") });
        cases.push_back({ key + "-empty", Crlf(key + " =\n") });
        cases.push_back({ key + "-dup-off-on", Crlf(key + " = off\n" + key + " = on\n") });
        cases.push_back({ key + "-dup-on-off", Crlf(key + " = on\n" + key + " = off\n") });
        cases.push_back({ key + "-off-then-malformed", Crlf(key + " = off\n" + key + " = maybe\n") });
        cases.push_back({ key + "-on-then-malformed", Crlf(key + " = on\n" + key + " = nope\n") });
        cases.push_back({ key + "-upper", Crlf("EXP" + key.substr(3) + " = OFF\n") });
    }
    {
        std::string allOff, allOn;
        for (const auto& s : kExperimental) { allOff += Narrow(s.key) + " = off\n"; allOn += Narrow(s.key) + " = on\n"; }
        cases.push_back({ "exp-all-off-crlf", Crlf(allOff) });
        cases.push_back({ "exp-all-off-lf", Lf(allOff) });
        cases.push_back({ "exp-all-on-crlf", Crlf(allOn) });
        cases.push_back({ "exp-all-off-bom-crlf", "\xEF\xBB\xBF" + Crlf(allOff) });
        cases.push_back({ "exp-all-on-bom-lf", "\xEF\xBB\xBF" + Lf(allOn) });
        cases.push_back({ "exp-all-off-no-trailing-newline", Crlf(allOff).substr(0, Crlf(allOff).size() - 2) });
    }
    cases.push_back({ "exp-spellings", Crlf(R"(exp_loadout_async = 0
exp_inventory_read = No
exp_actor_stats_write = FALSE
exp_cvars_native = 1
exp_audio_native = YES
exp_entities_diag = True
)") });
    cases.push_back({ "exp-malformed-all", Crlf(R"(exp_loadout_async = 2
exp_inventory_read = enabled
exp_actor_stats_write = ""
exp_cvars_native = on off
exp_audio_native = ?
exp_entities_diag = 11
)") });
    cases.push_back({ "exp-comments-and-unknown", Crlf(R"(# exp_loadout_async = off
; exp_inventory_read = off
exp_loadout_async_x = off
exp_unknown = off
loadout_async = off
exp_cvars_native = off
)") });
    cases.push_back({ "exp-quotes-and-spaces", Crlf("exp_loadout_async = \"off\"\n\texp_inventory_read\t=\toff\t\n  exp_audio_native   =   no   \n") });
    cases.push_back({ "exp-mixed-endings", "exp_loadout_async = off\r\nexp_inventory_read = off\nexp_actor_stats_write = off\r\n" });
    const std::pair<const char*, const char*> logs[] = {
        { "ask", "ask" }, { "always", "always" }, { "never", "never" }, { "always-upper", "ALWAYS" }, { "on", "on" },
        { "off", "off" }, { "yes", "yes" }, { "no", "no" }, { "true", "true" }, { "false", "false" }, { "one", "1" },
        { "zero", "0" }, { "invalid", "sometimes" }, { "empty", "" } };
    for (const auto& l : logs)
        cases.push_back({ std::string("clean-logs-") + l.first, std::string("clean_logs = ") + l.second + "\r\n" });
    const std::pair<const char*, const char*> allow[] = {
        { "ok-three", "100.64.0.0/10, 192.168.1.0/24,10.0.0.0/8" },
        { "ok-leading-zeros", "010.000.000.000/8" },
        { "ok-loopback", "127.0.0.0/8" },
        { "ok-link-local", "169.254.0.0/16" },
        { "ok-172", "172.16.0.0/12" },
        { "ok-host", "192.168.1.7/32" },
        { "ok-eight", "10.0.0.0/8,10.1.0.0/16,10.2.0.0/16,10.3.0.0/16,10.4.0.0/16,10.5.0.0/16,10.6.0.0/16,10.7.0.0/16" },
        { "bad-nine", "10.0.0.0/8,10.1.0.0/16,10.2.0.0/16,10.3.0.0/16,10.4.0.0/16,10.5.0.0/16,10.6.0.0/16,10.7.0.0/16,10.8.0.0/16" },
        { "bad-public", "8.8.8.0/24" },
        { "bad-host-bits", "192.168.1.5/24" },
        { "bad-prefix-7", "10.0.0.0/7" },
        { "bad-prefix-33", "10.0.0.0/33" },
        { "bad-three-octets", "1.2.3/24" },
        { "bad-trailing-text", "10.0.0.0/8 extra" },
        { "bad-trailing-comma", "10.0.0.0/8," },
        { "bad-empty-item", "192.168.0.0/16,,10.0.0.0/8" },
        { "bad-outside-172", "172.32.0.0/12" },
        { "bad-octet-256", "10.0.0.256/8" },
        { "bad-four-digit", "0010.0.0.0/8" },
        { "empty", "" },
        { "bad-spaces-only", "   " } };
    for (const auto& a : allow)
        cases.push_back({ std::string("allow-") + a.first, std::string("multiplayer_allow = ") + a.second + "\r\n" });
    for (const auto& c : cases) ParseCase(c.first, c.second);
    MissingParseCase();
}

// Inherited environment against the capability switches: what is left in the game's environment, and what the launcher reports.
static void CapsScenarioCases(const fs::path& repo) {
    struct Scenario { const char* name; std::string ini; std::vector<std::pair<std::string, std::string>> inherited; };
    std::string allOff;
    for (const auto& s : kExperimental) allOff += Narrow(s.key) + " = off\n";
    const std::string shipped = Crlf(Slurp(repo / "launcher" / "sc-offline.ini"));
    const std::vector<Scenario> cases = {
        { "defaults-nothing-inherited", "", {} },
        { "shipped-nothing-inherited", shipped, {} },
        { "shipped-inherited-zero", shipped, { { "SCO_LOADOUT_ASYNC", "0" }, { "SCO_ENTITIES_DIAG", "0" } } },
        { "shipped-inherited-diag-and-odd", shipped, { { "SCO_ENTITIES_DIAG", "1" }, { "SCO_AUDIO_NATIVE", "2" } } },
        { "all-off-nothing-inherited", Crlf(allOff), {} },
        { "all-off-inherited-one", Crlf(allOff), { { "SCO_LOADOUT_ASYNC", "1" }, { "SCO_INVENTORY_READ", "1" }, { "SCO_ACTOR_STATS_WRITE", "1" },
                                                  { "SCO_CVARS_NATIVE", "1" }, { "SCO_AUDIO_NATIVE", "1" }, { "SCO_ENTITIES_DIAG", "1" } } },
        { "all-off-inherited-odd-values", Crlf(allOff), { { "SCO_LOADOUT_ASYNC", "true" }, { "SCO_INVENTORY_READ", "on" }, { "SCO_ACTOR_STATS_WRITE", " 1" } } },
        { "all-off-inherited-empty", Crlf(allOff), { { "SCO_LOADOUT_ASYNC", "" }, { "SCO_CVARS_NATIVE", "" } } },
        { "one-off-inherited-one", Crlf("exp_cvars_native = off\n"), { { "SCO_CVARS_NATIVE", "1" }, { "SCO_AUDIO_NATIVE", "1" } } },
        { "diag-on-nothing-inherited", Crlf("exp_entities_diag = on\n"), {} },
        { "diag-on-inherited-yes", Crlf("exp_entities_diag = on\n"), { { "SCO_ENTITIES_DIAG", "yes" } } },
    };
    std::string list;
    for (const Scenario& sc : cases) {
        const fs::path file = g_scratch / "caps.ini";
        PutBytes(file, sc.ini); --g_files;
        Config c;
        bool returned = false;
        RunReadConfig(file, c, returned);
        std::map<std::string, std::string> inherited(sc.inherited.begin(), sc.inherited.end());
        std::string in;
        for (const auto& kv : sc.inherited) in += kv.first + "=" + kv.second + "\n";
        Put(std::string("caps/") + sc.name + ".ini", sc.ini);
        Put(std::string("caps/") + sc.name + ".inherited", in);
        Put(std::string("caps/") + sc.name + ".result.txt", CapsDump(c, inherited));
        list += std::string(sc.name) + "\n";
    }
    Put("caps/cases.txt", list);
}

// --- in-place ini edits ---------------------------------------------------------------------------

struct EditCase { std::string name, op, key, value, input; bool inputPresent; };

static void IniEditCases() {
    const std::vector<EditCase> cases = {
        { "drop-discord-crlf", "drop", "discord_presence", "", "plugins = on\r\ndiscord_presence = off\r\nasop = on\r\n", true },
        { "drop-discord-lf", "drop", "discord_presence", "", "plugins = on\ndiscord_presence = off\nasop = on\n", true },
        { "drop-indented-and-case", "drop", "discord_presence", "", "  \tDiscord_Presence=off\r\nplugins = on\r\n", true },
        { "drop-spaced-equals", "drop", "discord_presence", "", "discord_presence \t = off\r\nplugins = on\r\n", true },
        { "drop-keeps-comment", "drop", "discord_presence", "", "# discord_presence = off\r\ndiscord_presence = off\r\n", true },
        { "drop-keeps-longer-key", "drop", "discord_presence", "", "discord_presence_x = 1\r\ndiscord_presence = off\r\n", true },
        { "drop-duplicates", "drop", "discord_presence", "", "discord_presence = off\r\nplugins = on\r\ndiscord_presence = on\r\n", true },
        { "drop-last-line-no-newline", "drop", "discord_presence", "", "plugins = on\r\ndiscord_presence = off", true },
        { "drop-absent-key", "drop", "discord_presence", "", "plugins = on\r\nasop = off\r\n", true },
        { "drop-empty-file", "drop", "discord_presence", "", "", true },
        { "drop-missing-file", "drop", "discord_presence", "", "", false },
        { "set-clean-logs-replace-crlf", "set", "clean_logs", "never", "plugins = on\r\nclean_logs = ask\r\nasop = on\r\n", true },
        { "set-clean-logs-replace-lf", "set", "clean_logs", "always", "plugins = on\nclean_logs = ask\nasop = on\n", true },
        { "set-clean-logs-duplicates", "set", "clean_logs", "never", "clean_logs = ask\r\nplugins = on\r\nclean_logs = always\r\n", true },
        { "set-clean-logs-append-crlf", "set", "clean_logs", "never", "plugins = on\r\nasop = on\r\n", true },
        { "set-clean-logs-append-no-newline", "set", "clean_logs", "never", "plugins = on", true },
        { "set-clean-logs-append-lf", "set", "clean_logs", "never", "plugins = on\n", true },
        { "set-clean-logs-comment-only", "set", "clean_logs", "never", "# clean_logs = ask\r\nplugins = on\r\n", true },
        { "set-clean-logs-indented-case", "set", "clean_logs", "ask", "\t Clean_Logs=always # keep\r\nplugins = on\r\n", true },
        { "set-clean-logs-longer-key", "set", "clean_logs", "never", "clean_logs_extra = 1\r\n", true },
        { "set-clean-logs-empty-file", "set", "clean_logs", "never", "", true },
        { "set-clean-logs-missing-file", "set", "clean_logs", "never", "", false },
        { "set-plugins-off", "set", "plugins", "off", Crlf("# header\nplugins = on\nplugins_dir = x\n"), true },
        { "set-language-keeps-others", "set", "language", "ru", Crlf("# a\n\nlanguage = auto\n# b\nasop = on\n"), true },
        { "set-bom-file", "set", "clean_logs", "never", "\xEF\xBB\xBF" "clean_logs = ask\r\n", true },
    };
    std::string list;
    for (const EditCase& c : cases) {
        const fs::path file = g_scratch / "edit.ini";
        std::error_code ec;
        fs::remove(file, ec);
        if (c.inputPresent) { PutBytes(file, c.input); --g_files; Put("ini-edit/" + c.name + ".in.ini", c.input); }
        const bool returned = c.op == "drop" ? DropIniKey(file.wstring(), c.key.c_str())
                                             : SetIniValue(file.wstring(), c.key.c_str(), c.value.c_str());
        const bool exists = fs::exists(file);
        if (returned && exists) Put("ini-edit/" + c.name + ".out.ini", Slurp(file));
        list += c.name + "\t" + c.op + "\t" + c.key + "\t" + c.value + "\t" + (c.inputPresent ? "present" : "missing") + "\t" +
                (returned ? "true" : "false") + "\t" + (returned && exists ? "written" : "unwritten") + "\n";
    }
    Put("ini-edit/cases.txt", list);
}

// --- install marker, key=value records, pc-changes.txt ----------------------------------------------

static void MarkerCases() {
    struct M { const char* name, *version, *sha, *mode, *when, *xml; };
    const M cases[] = {
        { "play-backup", "0.7.8.3", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "play", "2026-11-17 09:05:03", "backup" },
        { "install-none", "sc-offline 0.2.0-rc4", "ABCDEF", "install", "2026-01-02 03:04:05", "none" },
        { "play-skip", "", "", "play", "2026-12-31 23:59:59", "skip" },
    };
    std::string list;
    for (const M& m : cases) {
        Put(std::string("marker/") + m.name + ".installed", ct::MarkerText(m.version, m.sha, m.mode, m.when, m.xml));
        list += std::string(m.name) + "\t" + m.version + "\t" + m.sha + "\t" + m.mode + "\t" + m.when + "\t" + m.xml + "\n";
    }
    Put("marker/cases.txt", list);
    Put("marker/times.txt", "marker=" + ct::MarkerTime(2026, 1, 2, 3, 4, 5) + "\nmarker=" + ct::MarkerTime(2026, 11, 17, 9, 5, 3) + "\n" +
                            "pc=" + ct::PcTime(2026, 1, 2, 3, 4) + "\npc=" + ct::PcTime(2026, 11, 17, 9, 5) + "\n");
}

static void FieldCases() {
    struct F { const char* name, *text, *key; };
    const std::vector<F> cases = {
        { "marker", "version=0.7.8.3\nsha256=abc\nmode=play\ntime=2026-11-17 09:05:03\nxml=backup\n", "xml" },
        { "marker-time", "version=0.7.8.3\nsha256=abc\nmode=play\ntime=2026-11-17 09:05:03\nxml=backup\n", "time" },
        { "crlf", "xml=backup\r\nmode=play\r\n", "xml" },
        { "trailing-spaces", "xml=backup   \nmode=play\n", "xml" },
        { "trailing-tab-kept", "xml=backup\t\nmode=play\n", "xml" },
        { "duplicate-first-wins", "xml=none\nxml=backup\n", "xml" },
        { "missing", "mode=play\n", "xml" },
        { "prefix-is-not-key", "xml2=backup\nmode=play\n", "xml" },
        { "indented-is-not-key", " xml=backup\nmode=play\n", "xml" },
        { "equals-in-value", "xml=a=b\n", "xml" },
        { "empty-value", "xml=\nmode=play\n", "xml" },
        { "last-line-no-newline", "mode=play\nxml=backup", "xml" },
        { "empty-text", "", "xml" },
        { "case-sensitive", "XML=backup\n", "xml" },
    };
    std::string list;
    for (const F& f : cases) {
        Put(std::string("field/") + f.name + ".in", f.text);
        list += std::string(f.name) + "\t" + f.key + "\t" + ct::RecordField(f.text, f.key) + "\n";
    }
    Put("field/cases.txt", list);
}

static void PcRecordCases() {
    struct P { const char* name, *leftover; std::vector<std::vector<std::string>> steps; };
    const std::vector<P> cases = {
        { "fresh-all", "", { { "time", "2026", "11", "17", "9", "5" }, { "add", "firewall", "added" }, { "add", "fw_launcher", "added" },
                              { "add", "fw_crash", "added" }, { "add", "fw_mp", "added" }, { "add", "hosts", "added" }, { "add", "eac", "renamed" } } },
        { "fresh-hosts-only", "", { { "time", "2026", "1", "2", "3", "4" }, { "add", "hosts", "added" } } },
        { "leftover-keeps-time", "time=2026-11-16 20:00\nfirewall=added\n",
          { { "time", "2026", "11", "17", "9", "5" }, { "add", "firewall", "added" }, { "add", "hosts", "added" } } },
        { "leftover-crlf-same-value", "time=2026-11-16 20:00\r\nhosts=added\r\n", { { "add", "hosts", "added" }, { "add", "eac", "renamed" } } },
        { "leftover-other-value", "eac=skipped\n", { { "add", "eac", "renamed" } } },
        { "leftover-no-newline", "time=2026-11-16 20:00", { { "add", "hosts", "added" } } },
    };
    std::string list;
    for (const P& p : cases) {
        std::string rec = p.leftover;
        for (const auto& s : p.steps) {
            if (s[0] == "time") {
                if (rec.find("time=") == std::string::npos)
                    ct::RecordAdd(rec, "time", ct::PcTime(std::stoul(s[1]), std::stoul(s[2]), std::stoul(s[3]), std::stoul(s[4]), std::stoul(s[5])).c_str());
            } else ct::RecordAdd(rec, s[1].c_str(), s[2].c_str());
        }
        Put(std::string("pc-changes/") + p.name + ".leftover", p.leftover);
        std::string steps;
        for (const auto& s : p.steps) { for (size_t i = 0; i < s.size(); ++i) steps += (i ? "\t" : "") + s[i]; steps += "\n"; }
        Put(std::string("pc-changes/") + p.name + ".steps", steps);
        Put(std::string("pc-changes/") + p.name + ".record", rec);
        list += std::string(p.name) + "\n";
    }
    Put("pc-changes/cases.txt", list);
}

// Names the launcher keeps in the code, written out so the Rust constants can be compared with them.
static void ConstantsAndPaths() {
    std::string t;
    t += "game_exe=" + Narrow(kGameExe) + "\n";
    t += std::string("exit.ok=") + std::to_string(kExitOk) + "\nexit.error=" + std::to_string(kExitError) + "\nexit.eac=" +
         std::to_string(kExitEac) + "\nexit.running=" + std::to_string(kExitRunning) + "\nexit.restart=" + std::to_string(kExitRestart) + "\n";
    t += std::string("eac_host=") + kEacHost + "\n";
    t += std::string("hosts_tag=") + kHostsTag + "\n";
    for (const FwRule& r : kFwRules) t += std::string("fw_rule.") + r.key + "=" + Narrow(r.name) + "\n";
    t += "pc_changes_rel=sc-offline\\pc-changes.txt\n";
    for (const auto& s : kExperimental)
        t += "exp." + Narrow(s.key) + "=" + Narrow(s.var) + "," + OnOff(s.defaultOn) + "\n";
    Put("constants.txt", t);

    const GamePaths g(L"C:\\Games\\StarCitizen\\LIVE\\Bin64");
    std::string p;
    p += "dll=" + Narrow(g.dll) + "\ndll_backup=" + Narrow(g.dllBackup) + "\nmarker=" + Narrow(g.marker) + "\nuser_dir=" +
         Narrow(g.userDir) + "\nxml=" + Narrow(g.xml) + "\nxml_backup=" + Narrow(g.xmlBackup) + "\n";
    Put("paths.txt", p);

    const wchar_t* owned[] = { L"sc-offline.ini", L"SC-OFFLINE.INI", L"data\\wallet.txt", L"DATA\\Wallet.TXT", L"data\\spawn.txt", L"data\\bookmarks.txt",
        L"data\\locations_found.txt", L"data\\buildables.txt", L"data\\game-path.txt", L"data\\game-build.txt", L"data\\mod.log",
        L"data\\launcher.log", L"data\\registry-dump.txt", L"data\\update\\applied.txt", L"DATA\\UPDATE\\x", L"data\\update",
        L"data\\ships.txt", L"data\\plugins\\spawn\\plugin.ini", L"sc-offline.exe", L"dinput8.dll", L"data\\launcher.log.1",
        L"data/wallet.txt", L"sc-offline.ini.bak", L"data\\OfflineDB\\default_1.xml" };
    std::string o;
    for (const wchar_t* rel : owned) o += Narrow(rel) + "\t" + (PlayerOwned(rel) ? "true" : "false") + "\n";
    Put("player-owned.txt", o);
}

// --- plugin.ini and [settings] ------------------------------------------------------------------------

static std::string Bool(bool b) { return b ? "true" : "false"; }

static std::string PluginDump(const PluginEntry& p) {
    std::string t;
    t += "id=" + p.id + "\nname=" + p.name + "\nversion=" + p.version + "\nauthor=" + p.author + "\nabout=" + p.about + "\nkind=" + p.kind + "\n";
    t += "settings_bad=" + Bool(p.settingsBad) + "\nsettings=" + std::to_string(p.settings.size()) + "\n";
    for (size_t i = 0; i < p.settings.size(); ++i) {
        const PluginSetting& s = p.settings[i];
        const std::string k = "s" + std::to_string(i) + ".";
        t += k + "name=" + s.name + "\n" + k + "type=" + s.type + "\n" + k + "label=" + s.label + "\n" + k + "help=" + s.help + "\n" +
             k + "default=" + s.def + "\n" + k + "has_min=" + Bool(s.hasMin) + "\n" + k + "has_max=" + Bool(s.hasMax) + "\n";
        t += k + "min=" + (s.type == "float" ? FloatText(s.minF) : std::to_string(s.minI)) + "\n";
        t += k + "max=" + (s.type == "float" ? FloatText(s.maxF) : std::to_string(s.maxI)) + "\n";
        std::string ch;
        for (size_t c = 0; c < s.choices.size(); ++c) ch += (c ? "|" : "") + s.choices[c];
        t += k + "choices=" + ch + "\n" + k + "value=" + s.value + "\n" + k + "kept=" + Bool(s.kept) + "\n";
    }
    return t;
}

static PluginEntry ParsePlugin(const std::string& folder, const std::string& text) {
    PluginEntry p;
    p.folder = Wide(folder);
    ParsePluginIni(text, p);
    return p;
}

static std::string ManySettings(int n) {
    std::string t = "name = Many\r\n[settings]\r\n";
    for (int i = 0; i < n; ++i) t += "s" + std::to_string(i) + " = bool default true\r\n";
    return t;
}

static std::string AllTypes() {
    return Crlf(R"(name = All Types
version = 1.2.3
author = Example Author
description = Settings of every type.
kind = native

[settings]
enabled = bool default true label "Enabled" help "Turns the plugin on."
count = int default 5 min 1 max 10 label "Count"
speed = float default 0.1 min 0 max 2.5 label "Speed" help "Meters per second."
title = string default "Hello world" label "Title"
mode = enum(fast,slow,off) default slow label "Mode"
plain = int
ratio = float default 1e3
)");
}

static void PluginCases() {
    const std::vector<std::pair<std::string, std::string>> cases = {
        { "minimal", "name = Minimal\r\n" },
        { "empty", "" },
        { "no-name-uses-folder", "version = 2\r\n" },
        { "empty-name-uses-folder", "name =\r\nversion = 3\r\n" },
        { "full-meta", "name = Full\r\nversion = 1.0\r\nauthor = A. Author\r\ndescription = About text\r\nkind = lua\r\n" },
        { "bom-crlf", "\xEF\xBB\xBF" "name = Bom\r\nversion = 9\r\n" },
        { "lf-endings", "name = Lf\nversion = 9\n[settings]\nflag = bool\n" },
        { "comments", "; head\r\n# head 2\r\nname = Cm ; trailing\r\nversion = 1 # trailing\r\nauthor = A#B\r\ndescription = \"a # b\"\r\n" },
        { "duplicate-keys-last-wins", "name = One\r\nname = Two\r\nversion = 1\r\nversion = 2\r\n" },
        { "other-section-ignored", "name = Top\r\n[meta]\r\nname = Meta\r\nversion = 7\r\n[settings]\r\nflag = bool\r\n[more]\r\nauthor = Z\r\n" },
        { "keys-case-sensitive", "Name = Upper\r\nVERSION = 1\r\nname = Lower\r\n" },
        { "spaces-and-tabs", "\tname\t=\tTabbed\t\r\n  version  =  1.5  \r\n" },
        { "all-types", AllTypes() },
        { "settings-defaults-clamped", "[settings]\r\nbig = int default 99 min 1 max 10\r\nsmall = int default -5 min 0\r\nfl = float default 7.5 max 2\r\n"
                                       "ch = enum(a,b) default z\r\nnone = enum(x,y)\r\nb = bool default maybe\r\ns = string default \"ok\"\r\n" },
        { "settings-float-text", "[settings]\r\na = float default 0.1\r\nb = float default 1e-7\r\nc = float default 123456789012345678\r\n"
                                 "d = float default -2.5e+3\r\ne = float default 0.30000000000000004\r\nf = float min 0.1 max 0.7\r\n" },
        { "settings-min-max-text", "[settings]\r\na = int min abc max 12abc\r\nb = int min -3 max +9\r\nc = float min xyz max 1e2\r\n" },
        { "settings-bad-type", "[settings]\r\na = number\r\nb = bool\r\n" },
        { "settings-bad-token", "[settings]\r\na = bool junk\r\n" },
        { "settings-bad-option", "[settings]\r\na = bool color \"red\"\r\n" },
        { "settings-quoted-first", "[settings]\r\na = \"bool\"\r\n" },
        { "settings-min-on-bool", "[settings]\r\na = bool min 1\r\n" },
        { "settings-enum-needs-list", "[settings]\r\na = enum\r\nb = enum()\r\nc = bool(x)\r\nd = enum(a,b\r\n" },
        { "settings-enum-spaces", "[settings]\r\na = enum( a , b ,, c ) default b\r\n" },
        { "settings-unterminated-quote", "[settings]\r\na = string label \"open\r\n" },
        { "settings-empty-rhs", "[settings]\r\na =\r\nb = bool\r\n" },
        { "settings-limit-32", ManySettings(32) },
        { "settings-over-32", ManySettings(33) },
        { "settings-section-case", "[Settings]\r\nflag = bool\r\nname = Not A Setting\r\n" },
        { "settings-then-top-keys", "[settings]\r\nflag = bool\r\n[other]\r\nname = Ignored\r\n" },
        { "settings-duplicate-names", "[settings]\r\nflag = bool default true\r\nflag = bool default false\r\n" },
        { "settings-label-defaults-to-name", "[settings]\r\nspeed = float\r\n" },
        { "settings-help-and-label-quoted", "[settings]\r\na = string label \"Two words\" help \"has ; and # inside\"\r\n" },
    };
    for (const auto& c : cases) {
        const PluginEntry p = ParsePlugin(c.first, c.second);
        Put("plugins/" + c.first + ".ini", c.second);
        Put("plugins/" + c.first + ".parsed.txt", PluginDump(p));
    }
}

struct CanonCase { const char* decl; const char* text; };

static void CanonCases() {
    const std::vector<CanonCase> cases = {
        { "bool", "true" }, { "bool", "false" }, { "bool", "True" }, { "bool", "1" }, { "bool", "" },
        { "int", "5" }, { "int", "+5" }, { "int", "-0" }, { "int", "007" }, { "int", "5.0" }, { "int", "" }, { "int", "-" },
        { "int", "999999999999999999" }, { "int", "1000000000000000000" }, { "int", "-999999999999999999" }, { "int", "12a" }, { "int", " 5" },
        { "int min 1 max 10", "0" }, { "int min 1 max 10", "1" }, { "int min 1 max 10", "10" }, { "int min 1 max 10", "11" },
        { "int min -5", "-5" }, { "int min -5", "-6" }, { "int max 0", "1" },
        { "float", "1" }, { "float", "1.50" }, { "float", "1e2" }, { "float", ".5" }, { "float", "5." }, { "float", "-0" },
        { "float", "0.1" }, { "float", "+0.25" }, { "float", "1e400" }, { "float", "00012" }, { "float", "1E-3" }, { "float", "abc" },
        { "float", "1e" }, { "float", "" }, { "float", "123456789012345678901234567890" }, { "float", "0.30000000000000004" },
        { "float", "1e21" }, { "float", "1e16" }, { "float", "1e17" }, { "float", "123456789012345678" }, { "float", "0.0001" }, { "float", "0.00001" },
        { "float min 0 max 2.5", "-0.1" }, { "float min 0 max 2.5", "2.5" }, { "float min 0 max 2.5", "2.6" }, { "float min 0.1", "0.1" },
        { "string", "hello" }, { "string", "" }, { "string", "with space" }, { "string", "a\x01" "b" }, { "string", "caf\xC3\xA9" },
        { "string", "~" }, { "string", "\x7f" },
        { "enum(a,b,c)", "a" }, { "enum(a,b,c)", "c" }, { "enum(a,b,c)", "A" }, { "enum(a,b,c)", "d" }, { "enum(a,b,c)", "" },
    };
    std::string t;
    for (const CanonCase& c : cases) {
        PluginEntry p;
        if (!ParseSettingLine("x", c.decl, p)) Die(std::string("bad canon declaration ") + c.decl);
        std::string out, why;
        const bool ok = CanonSetting(p.settings[0], c.text, out, why);
        t += std::string(c.decl) + "\t" + c.text + "\t" + (ok ? "ok" : "fail") + "\t" + (ok ? out : "") + "\n";
    }
    // The string limit is 255 characters.
    for (size_t n : { size_t(255), size_t(256) }) {
        PluginEntry p;
        ParseSettingLine("x", "string", p);
        std::string out, why;
        const bool ok = CanonSetting(p.settings[0], std::string(n, 'a'), out, why);
        t += "string\t<" + std::to_string(n) + " x a>\t" + (ok ? "ok" : "fail") + "\t" + (ok ? "len " + std::to_string(out.size()) : "") + "\n";
    }
    Put("plugins/canon.txt", t);
}

static std::string KvRows(const fs::path& db) {
    sqlite3* h = nullptr;
    if (sqlite3_open_v2(db.string().c_str(), &h, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) Die("cannot open the store " + db.string());
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(h, "SELECT key, value FROM sco_kv ORDER BY key", -1, &st, nullptr) != SQLITE_OK) Die("cannot read the store");
    std::string rows;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char* k = reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        const char* v = static_cast<const char*>(sqlite3_column_blob(st, 1));
        rows += std::string(k) + "\t" + std::string(v ? v : "", size_t(sqlite3_column_bytes(st, 1))) + "\n";
    }
    sqlite3_finalize(st);
    sqlite3_close(h);
    return rows;
}

// The settings store: key sco.settings.<name>, value <type>:<canonical text>, in data\storage\<id>.db.
static void StoreCases() {
    const fs::path here = g_scratch / "store-root";
    std::error_code ec;
    fs::remove_all(here, ec);
    fs::create_directories(here);   // SavePluginSettings makes data\storage, not the launcher folder
    const wstring wHere = here.wstring();
    const std::string decl = AllTypes();
    PluginEntry p = ParsePlugin("storedemo", decl);
    const auto setEdit = [&](PluginEntry& e, const char* name, const char* text) {
        for (PluginSetting& s : e.settings) if (s.name == name) std::snprintf(s.edit, sizeof(s.edit), "%s", text);
    };
    std::string t;
    std::string err;
    // 1. Edit four values; unchanged ones are not written.
    setEdit(p, "enabled", "false"); setEdit(p, "count", "007"); setEdit(p, "speed", "0.7"); setEdit(p, "title", "New title");
    const bool saved = SavePluginSettings(wHere, p, false, err);
    t += "save_ok=" + Bool(saved) + "\n";
    const fs::path db = here / "data" / "storage" / "storedemo.db";
    t += "rows:\n" + KvRows(db);
    // 2. Load them back into a fresh parse.
    PluginEntry q = ParsePlugin("storedemo", decl);
    LoadKept(wHere, q);
    t += "loaded:\n";
    for (const PluginSetting& s : q.settings) t += s.name + "\t" + s.value + "\t" + Bool(s.kept) + "\n";
    // 3. An invalid edit writes nothing and fails.
    PluginEntry r = ParsePlugin("storedemo", decl);
    setEdit(r, "count", "99");
    const bool bad = SavePluginSettings(wHere, r, false, err);
    t += "invalid_save_ok=" + Bool(bad) + "\nrows_after_invalid:\n" + KvRows(db);
    // 4. A kept value that no longer fits its declaration is ignored on load.
    {
        PluginEntry s2 = ParsePlugin("storedemo", "[settings]\r\ncount = int default 5 min 1 max 3\r\nenabled = string\r\n");
        LoadKept(wHere, s2);
        t += "loaded_after_redeclare:\n";
        for (const PluginSetting& s : s2.settings) t += s.name + "\t" + s.value + "\t" + Bool(s.kept) + "\n";
    }
    // 5. Reset removes the kept rows.
    const bool reset = SavePluginSettings(wHere, q, true, err);
    t += "reset_ok=" + Bool(reset) + "\nrows_after_reset:\n" + KvRows(db);
    Put("plugins/store.txt", t);
}

int main(int argc, char** argv) {
    if (argc != 3) Die("usage: launcher_contract_dump <output folder> <sc-offline repository root>");
    std::freopen("NUL", "w", stdout);   // the launcher's own messages go to the scratch log, not the console
    g_out = fs::absolute(argv[1]);
    const fs::path repo = fs::absolute(argv[2]);
    g_scratch = fs::temp_directory_path() / ("sco-contract-dump-" + std::to_string(GetCurrentProcessId()));
    fs::create_directories(g_scratch);
    fs::create_directories(g_out);
    IniParseCases(repo);
    CapsScenarioCases(repo);
    IniEditCases();
    MarkerCases();
    FieldCases();
    PcRecordCases();
    ConstantsAndPaths();
    PluginCases();
    CanonCases();
    StoreCases();
    std::error_code ec;
    fs::remove_all(g_scratch, ec);
    std::cerr << "launcher_contract_dump: wrote " << g_files << " files to " << g_out.string() << "\n";
    return 0;
}
