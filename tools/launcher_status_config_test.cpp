// SPDX-License-Identifier: GPL-3.0-only
// Launcher logic that needs no window and no game: the clean_logs setting and its writer, the session-log
// answers, the read-only `status` command and the Status panel's checks, the Repair gating and Stop's target
// selection. The real launcher source is included, so these are the shipped functions. An optional second
// argument is the built sc-offline.exe: its `status` is then run in a scratch folder and must change nothing.
// No helper, game or network call runs; the stand-in processes are renamed copies of a system tool, ended by handle.
#define wmain sco_launcher_unused_entrypoint
#include "../launcher/launcher.cpp"
#undef wmain
#include <filesystem>
#include <fstream>
#include <iostream>
#include <atomic>
#include <map>
#include <thread>

namespace fs = std::filesystem;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static void Put(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

static std::string Get(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Every entry under root: kind, size, last write time and a hash of the content. Equal before and after
// means nothing was created, deleted, renamed or rewritten.
static std::map<std::string, std::string> Snapshot(const fs::path& root) {
    std::map<std::string, std::string> state;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        std::string row = entry.is_directory() ? "dir" : "file";
        if (entry.is_regular_file()) {
            row += " " + std::to_string(entry.file_size()) + " " + std::to_string(entry.last_write_time().time_since_epoch().count()) +
                   " " + std::to_string(std::hash<std::string>{}(Get(entry.path())));
        }
        state[fs::relative(entry.path(), root).generic_string()] = row;
    }
    return state;
}

static LogChoice ConfigChoice(const fs::path& ini, const char* value, LogChoice before) {
    Put(ini, std::string("clean_logs = ") + value + "\r\n");
    Config config;
    config.cleanLogs = before;
    CHECK(ReadConfig(ini.wstring(), config));
    return config.cleanLogs;
}

static void TestConfig(const fs::path& root) {
    const fs::path ini = root / "config.ini";
    { Config defaults; CHECK(defaults.cleanLogs == LogChoice::Ask); }
    CHECK(ConfigChoice(ini, "ask", LogChoice::Never) == LogChoice::Ask);
    CHECK(ConfigChoice(ini, "always", LogChoice::Ask) == LogChoice::Always);
    CHECK(ConfigChoice(ini, "never", LogChoice::Ask) == LogChoice::Never);
    CHECK(ConfigChoice(ini, "ALWAYS", LogChoice::Ask) == LogChoice::Always);
    CHECK(ConfigChoice(ini, "on", LogChoice::Never) == LogChoice::Ask);      // legacy on = ask
    CHECK(ConfigChoice(ini, "off", LogChoice::Ask) == LogChoice::Never);     // legacy off = never
    CHECK(ConfigChoice(ini, "yes", LogChoice::Never) == LogChoice::Ask);
    CHECK(ConfigChoice(ini, "false", LogChoice::Ask) == LogChoice::Never);
    CHECK(ConfigChoice(ini, "sometimes", LogChoice::Always) == LogChoice::Always);   // invalid: kept, warned
    CHECK(ConfigChoice(ini, "", LogChoice::Never) == LogChoice::Never);
    CHECK(!strcmp(CleanLogsWord(LogChoice::Ask), "ask") && !strcmp(CleanLogsWord(LogChoice::Always), "always") &&
          !strcmp(CleanLogsWord(LogChoice::Never), "never"));
    // The shipped default ini says ask.
    Config shipped;
    CHECK(ReadConfig((fs::weakly_canonical(fs::path(__FILE__)).parent_path().parent_path() / "launcher" / "sc-offline.ini").wstring(), shipped));
    CHECK(shipped.cleanLogs == LogChoice::Ask);
}

// The capability switches: on by default, off by the ini, a typo keeps the default, and the env block follows the ini.
static bool EnvIs(const wchar_t* name, const wchar_t* want) {   // want == nullptr: the variable must be unset
    wchar_t buf[8] = {};
    const DWORD n = GetEnvironmentVariableW(name, buf, ARRAYSIZE(buf));
    return want ? (n == 1 && !wcscmp(buf, want)) : n == 0;
}

static void TestExperimental(const fs::path& root) {
    const fs::path ini = root / "experimental.ini";
    Config defaults;
    for (size_t i = 0; i < ARRAYSIZE(kExperimental); ++i) CHECK(defaults.experimental[i] == kExperimental[i].defaultOn);
    // Every capability switch is on by default; the entity diagnostics (log-only hooks, not a feature) are off.
    CHECK(defaults.experimental[ExperimentalIndex(L"exp_loadout_async")] && defaults.experimental[ExperimentalIndex(L"exp_inventory_read")] &&
          defaults.experimental[ExperimentalIndex(L"exp_actor_stats_write")] && !defaults.experimental[ExperimentalIndex(L"exp_entities_diag")]);
    CHECK(ExperimentalIndex(L"EXP_LOADOUT_ASYNC") >= 0 && ExperimentalIndex(L"exp_nothing") < 0);

    // The shipped ini lists every key, with the same values as the defaults.
    Config shipped;
    const fs::path shippedIni = fs::weakly_canonical(fs::path(__FILE__)).parent_path().parent_path() / "launcher" / "sc-offline.ini";
    CHECK(ReadConfig(shippedIni.wstring(), shipped));
    const std::string shippedText = Get(shippedIni);
    for (size_t i = 0; i < ARRAYSIZE(kExperimental); ++i) {
        CHECK(shipped.experimental[i] == kExperimental[i].defaultOn);
        std::string key;
        for (const wchar_t* c = kExperimental[i].key; *c; ++c) key += static_cast<char>(*c);   // the keys are ASCII
        key += " = ";
        CHECK(shippedText.find("\n" + key + (kExperimental[i].defaultOn ? "on" : "off")) != std::string::npos);
    }

    // off, case and legacy words; a typo keeps the default.
    Put(ini, "exp_loadout_async = off\r\nEXP_INVENTORY_READ = No\r\nexp_actor_stats_write = maybe\r\nexp_entities_diag = yes\r\n");
    Config parsed;
    CHECK(ReadConfig(ini.wstring(), parsed));
    CHECK(!parsed.experimental[ExperimentalIndex(L"exp_loadout_async")] && !parsed.experimental[ExperimentalIndex(L"exp_inventory_read")]);
    CHECK(parsed.experimental[ExperimentalIndex(L"exp_actor_stats_write")]);   // typo: default on
    CHECK(parsed.experimental[ExperimentalIndex(L"exp_entities_diag")]);
    CHECK(parsed.experimental[ExperimentalIndex(L"exp_cvars_native")]);        // not in the file: default

    // The env block: on sets 1, off removes, including a value that was already in the environment.
    SetEnvironmentVariableW(L"SCO_LOADOUT_ASYNC", L"1");     // inherited, but the ini says off
    SetEnvironmentVariableW(L"SCO_INVENTORY_READ", L"0");    // inherited junk, ini says off
    SetEnvironmentVariableW(L"SCO_ACTOR_STATS_WRITE", nullptr);
    wstring removed;
    const wstring on = ApplyExperimentalEnv(parsed, removed);
    CHECK(EnvIs(L"SCO_LOADOUT_ASYNC", nullptr) && EnvIs(L"SCO_INVENTORY_READ", nullptr));
    CHECK(EnvIs(L"SCO_ACTOR_STATS_WRITE", L"1") && EnvIs(L"SCO_CVARS_NATIVE", L"1") && EnvIs(L"SCO_AUDIO_NATIVE", L"1") && EnvIs(L"SCO_ENTITIES_DIAG", L"1"));
    CHECK(removed == L"SCO_LOADOUT_ASYNC, SCO_INVENTORY_READ");
    CHECK(on.find(L"exp_actor_stats_write (SCO_ACTOR_STATS_WRITE=1)") != wstring::npos && on.find(L"exp_loadout_async") == wstring::npos);

    // With the defaults the capability switches are set to 1; the diagnostics switch is off, so an inherited value is removed.
    SetEnvironmentVariableW(L"SCO_ENTITIES_DIAG", L"1");
    wstring removed2;
    const wstring onDefaults = ApplyExperimentalEnv(defaults, removed2);
    CHECK(EnvIs(L"SCO_LOADOUT_ASYNC", L"1") && EnvIs(L"SCO_INVENTORY_READ", L"1") && EnvIs(L"SCO_ACTOR_STATS_WRITE", L"1"));
    CHECK(EnvIs(L"SCO_ENTITIES_DIAG", nullptr) && removed2 == L"SCO_ENTITIES_DIAG");
    CHECK(onDefaults.find(L"SCO_ENTITIES_DIAG") == wstring::npos && onDefaults.find(L"SCO_LOADOUT_ASYNC=1") != wstring::npos);

    // All off: nothing listed, nothing set.
    Config none;
    for (bool& b : none.experimental) b = false;
    wstring removed3;
    CHECK(ApplyExperimentalEnv(none, removed3).empty() && !removed3.empty());   // the defaults set above are removed
    CHECK(ApplyExperimentalEnv(none, removed3).empty() && removed3.empty());    // a second pass finds nothing left
    for (const ExperimentalSwitch& s : kExperimental) CHECK(EnvIs(s.var, nullptr));
}

// The in-game menu key: imgui by default and in the shipped ini, rust only when asked, any spelling of case, a
// typo keeps what was there, and the play environment carries it as SC_OFFLINE_MENU (always set, last).
static void TestMenuKey(const fs::path& root) {
    const fs::path ini = root / "menu.ini";
    { Config defaults; CHECK(defaults.menu == L"imgui"); }
    Config shipped;
    const fs::path shippedIni = fs::weakly_canonical(fs::path(__FILE__)).parent_path().parent_path() / "launcher" / "sc-offline.ini";
    CHECK(ReadConfig(shippedIni.wstring(), shipped) && shipped.menu == L"imgui");
    CHECK(Get(shippedIni).find("\nmenu = imgui") != std::string::npos);
    const auto read = [&](const char* text, const wstring& before) {
        Put(ini, text);
        Config c;
        c.menu = before;
        CHECK(ReadConfig(ini.wstring(), c));
        return c.menu;
    };
    CHECK(read("menu = rust\r\n", L"imgui") == L"rust");
    CHECK(read("menu = RUST\r\n", L"imgui") == L"rust");     // stored lower case, whatever the spelling
    CHECK(read("MENU = Imgui\r\n", L"rust") == L"imgui");
    CHECK(read("menu = egui\r\n", L"rust") == L"rust");      // invalid: kept, warned
    CHECK(read("menu =\r\n", L"rust") == L"rust");
    CHECK(read("menu = rust\r\nmenu = imgui\r\n", L"imgui") == L"imgui");   // the last valid line wins
    // the warning names the key and the choices
    {
        const fs::path log = root / "menu.log";
        FILE* saved = g_log;
        g_log = _wfsopen(log.c_str(), L"w+b", _SH_DENYNO);
        Config c;
        Put(ini, "menu = egui\r\n");
        CHECK(ReadConfig(ini.wstring(), c));
        std::fflush(g_log); std::fclose(g_log); g_log = saved;
        CHECK(Get(log) == "[!] sc-offline.ini line 1: menu must be imgui or rust\n");
    }
    // the play environment: SC_OFFLINE_MENU is the last variable, imgui unless the ini says rust
    for (const wchar_t* choice : { L"imgui", L"rust" }) {
        sco::launcher::contract::PlayEnvInput in;
        in.menu = choice;
        const auto env = sco::launcher::contract::PlayEnv(in);
        CHECK(env.back().first == L"SC_OFFLINE_MENU" && env.back().second == choice);
        CHECK(env.size() == 15);
    }
    { sco::launcher::contract::PlayEnvInput in; CHECK(sco::launcher::contract::PlayEnv(in).back().second == L"imgui"); }
}

// An update adds missing keys to an old ini commented out, with the line above each key, so the
// "not run in game" sentence is not lost for upgraded players.
static void TestMergeIniKeepsNotes(const fs::path& root) {
    const fs::path dir = root / "merge";
    fs::create_directories(dir);
    { std::ofstream o(dir / "sc-offline.ini", std::ios::binary); o << "clean_logs = off\r\n"; }
    const fs::path shipped = fs::weakly_canonical(fs::path(__FILE__)).parent_path().parent_path() / "launcher" / "sc-offline.ini";
    MergeIni(dir.wstring(), shipped.wstring(), "v-test");
    const std::string merged = Get(dir / "sc-offline.ini");
    CHECK(merged.find("# exp_loadout_async = on") != std::string::npos);
    const size_t note = merged.find("# exp_loadout_async: ");
    CHECK(note != std::string::npos && note < merged.find("# exp_loadout_async = on"));
    CHECK(merged.find("Seen working in game", note) < merged.find("# exp_loadout_async = on"));   // the one switch seen working; the others say "Not run in game."
    Config c;
    CHECK(ReadConfig((dir / "sc-offline.ini").wstring(), c) && c.experimental[ExperimentalIndex(L"exp_loadout_async")]);
}

static void TestAnswers() {
    CHECK(ParseLogAnswer("y\n") == LogAnswer::Delete && ParseLogAnswer("Y") == LogAnswer::Delete && ParseLogAnswer("yes\r\n") == LogAnswer::Delete);
    CHECK(ParseLogAnswer("n\n") == LogAnswer::Keep && ParseLogAnswer("\n") == LogAnswer::Keep && ParseLogAnswer("") == LogAnswer::Keep);
    CHECK(ParseLogAnswer("always\n") == LogAnswer::Always && ParseLogAnswer("  Always \r\n") == LogAnswer::Always && ParseLogAnswer("a") == LogAnswer::Always);
    CHECK(ParseLogAnswer("never\n") == LogAnswer::Never && ParseLogAnswer("NEVER") == LogAnswer::Never);
    CHECK(ParseLogAnswer("nope\n") == LogAnswer::Keep && ParseLogAnswer("alwaysx\n") == LogAnswer::Keep);   // anything unclear keeps
}

static void TestIniWriter(const fs::path& root) {
    const fs::path ini = root / "writer.ini";
    const std::string original = "# sc-offline.ini\r\ngame = X:\\Games\\StarCitizen\r\n\r\n"
                                 "# clean_logs: ask, always or never\r\nclean_logs = ask\r\nclean_logs_extra = 1\r\nplugins = on\r\n";
    Put(ini, original);
    CHECK(!RememberLogChoice((root / "no-such-folder").wstring(), LogChoice::Never));   // a failed write is reported, not hidden
    // The real call site passes the folder that holds sc-offline.ini.
    const fs::path here = root / "writer-here";
    Put(here / "sc-offline.ini", original);
    CHECK(RememberLogChoice(here.wstring(), LogChoice::Always));
    std::string expected = original;
    expected.replace(expected.find("clean_logs = ask"), 16, "clean_logs = always");
    CHECK(Get(here / "sc-offline.ini") == expected);          // only that line changed; comments, other keys, CRLF intact
    CHECK(RememberLogChoice(here.wstring(), LogChoice::Never));
    expected.replace(expected.find("clean_logs = always"), 19, "clean_logs = never");
    CHECK(Get(here / "sc-offline.ini") == expected);
    Config config;
    CHECK(ReadConfig((here / "sc-offline.ini").wstring(), config) && config.cleanLogs == LogChoice::Never);
    // No clean_logs line yet (only a comment): one is appended, nothing else moves.
    const fs::path bare = root / "bare";
    const std::string text = "# clean_logs = ask\r\nplugins = on\r\n";
    Put(bare / "sc-offline.ini", text);
    CHECK(RememberLogChoice(bare.wstring(), LogChoice::Always));
    const std::string after = Get(bare / "sc-offline.ini");
    CHECK(after.compare(0, text.size(), text) == 0 && after.find("clean_logs = always") != std::string::npos);
    Config appended;
    CHECK(ReadConfig((bare / "sc-offline.ini").wstring(), appended) && appended.cleanLogs == LogChoice::Always && appended.plugins);

    // Duplicate uncommented lines: the reader keeps the last one, so every one must carry the saved value.
    const fs::path dup = root / "dup";
    Put(dup / "sc-offline.ini", "clean_logs = ask\r\nplugins = on\r\n  CLEAN_LOGS=off\r\n# clean_logs = ask\r\nclean_logs = never");
    CHECK(RememberLogChoice(dup.wstring(), LogChoice::Always));
    CHECK(Get(dup / "sc-offline.ini") == "clean_logs = always\r\nplugins = on\r\nclean_logs = always\r\n# clean_logs = ask\r\nclean_logs = always");
    Config duplicated;
    CHECK(ReadConfig((dup / "sc-offline.ini").wstring(), duplicated) && duplicated.cleanLogs == LogChoice::Always);

    // An ini that exists but can't be read is never replaced with a one-line file.
    const fs::path locked = root / "locked";
    const std::string precious = "game = X:\\Games\r\n# my notes\r\nplugins = off\r\n";
    Put(locked / "sc-offline.ini", precious);
    HANDLE hold = CreateFileW((locked / "sc-offline.ini").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);                                      // no sharing: ReadAll fails
    CHECK(!RememberLogChoice(locked.wstring(), LogChoice::Never));
    CloseHandle(hold);
    CHECK(Get(locked / "sc-offline.ini") == precious);
    CHECK(RememberLogChoice(locked.wstring(), LogChoice::Never));              // readable again: it works
    CHECK(Get(locked / "sc-offline.ini").compare(0, precious.size(), precious) == 0);
}

// A game folder the launcher accepts: <root>/game/LIVE/Bin64/StarCitizen.exe, and a build_manifest.id beside it.
static fs::path MakeGame(const fs::path& root) {
    const fs::path bin = root / "game" / "LIVE" / "Bin64";
    Put(bin / "StarCitizen.exe", "not a real executable");
    Put(bin.parent_path() / "build_manifest.id", "{\"Data\":{\"Version\":\"0.0.0-test\",\"RequestedP4ChangeNum\":\"1\"}}");
    return bin;
}

static void TestStatusReadOnly(const fs::path& root) {
    const fs::path here = root / "status-here";
    const fs::path bin = MakeGame(root / "status-game");
    // Everything play would write or migrate is armed: a legacy discord_presence line, update checks on, no
    // remembered game path, no data folder at all.
    Put(here / "sc-offline.ini", "game = " + bin.parent_path().parent_path().string() + "\r\ncheck_updates = on\r\ndiscord_presence = off\r\n");
    Put(here / "data" / "plugins" / "discord" / "plugin.ini", "name = Discord\r\n");
    const auto before = Snapshot(here);

    CHECK(RunStatus(here.wstring(), L"") == kExitOk);                       // game = from the ini
    CHECK(Snapshot(here) == before);
    CHECK(RunStatus(here.wstring(), bin.wstring()) == kExitOk);              // --game <folder>
    CHECK(Snapshot(here) == before);
    CHECK(!fs::exists(here / "data" / "launcher.log") && !fs::exists(here / "data" / "game-path.txt"));
    CHECK(!fs::exists(here / "data" / "plugins" / "discord" / "disabled"));
    CHECK(Get(here / "sc-offline.ini").find("discord_presence = off") != std::string::npos);
    // A bad --game folder fails without writing either.
    CHECK(RunStatus(here.wstring(), (root / "nowhere").wstring()) == kExitError);
    CHECK(Snapshot(here) == before);
    // Even a bare folder with no data\ stays bare.
    const fs::path bare = root / "status-bare";
    Put(bare / "sc-offline.ini", "game = " + bin.parent_path().parent_path().string() + "\r\n");
    const auto bareBefore = Snapshot(bare);
    CHECK(RunStatus(bare.wstring(), L"") == kExitOk);
    CHECK(Snapshot(bare) == bareBefore && !fs::exists(bare / "data"));
}

static void TestStatusReport(const fs::path& root) {
    const fs::path here = root / "report-here";
    const fs::path bin = MakeGame(root / "report-game");
    Put(here / "sc-offline.ini", "game = " + bin.parent_path().parent_path().string() + "\r\nblock_network = off\r\nplugins = on\r\n");
    Put(here / "data" / "plugins" / "one" / "plugin.ini", "name = One\r\n");
    Put(here / "data" / "plugins" / "two" / "plugin.ini", "name = Two\r\n");
    Put(here / "data" / "plugins" / "two" / "disabled", "");
    Put(here / "data" / "plugins" / "three" / "readme.txt", "no plugin.ini: the host skips it");
    const auto before = Snapshot(here);
    const auto beforeGame = Snapshot(root / "report-game");

    StatusReport r = CollectStatus(here.wstring());
    CHECK(r.checked && r.folderKnown);
    CHECK(fs::equivalent(fs::path(r.bin), bin));
    CHECK(r.build == "0.0.0-test (CL 1)");
    CHECK(!r.modInstalled);
    CHECK(!r.networkBlocked && r.pluginsOn);
    CHECK(r.pluginsTotal == 2 && r.pluginsEnabled == 1);
    CHECK(Snapshot(here) == before && Snapshot(root / "report-game") == beforeGame);   // read-only

    Put(bin / "sc-offline.installed", "time=now\n");                          // the mod's marker is in Bin64
    CHECK(CollectStatus(here.wstring()).modInstalled);

    Put(here / "sc-offline.ini", "game = " + (root / "nowhere").string() + "\r\n");   // a game = that does not resolve
    r = CollectStatus(here.wstring());
    CHECK(r.checked && !r.folderKnown && r.build.empty() && !r.modInstalled);
}

static void TestSessionLogs(const fs::path& root) {
    const fs::path channel = root / "logs-channel";
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const ULONGLONG since = FileTimeU64(now) - 600ull * 10000000ull;           // this session began 10 minutes ago
    const auto layout = [&] {
        Put(channel / "Game.log", "session");
        Put(channel / "logbackups" / "Game Build(1).log", "session backup");
        Put(channel / "logbackups" / "older.log", "online play before this session");
        Put(channel / "Crashes" / "crash-1" / "crash.xml", "session crash");
        // The old one: last written an hour before the session began.
        HANDLE f = CreateFileW((channel / "logbackups" / "older.log").c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(f != INVALID_HANDLE_VALUE);
        ULARGE_INTEGER old; old.QuadPart = since - 3600ull * 10000000ull;
        FILETIME oldTime{ old.LowPart, old.HighPart };
        CHECK(SetFileTime(f, nullptr, nullptr, &oldTime));
        CloseHandle(f);
    };
    const fs::path here = root / "logs-here";
    Put(here / "sc-offline.ini", "clean_logs = always\r\n");
    const std::string ini = Get(here / "sc-offline.ini");

    layout();
    CleanSessionLogs(here.wstring(), channel.wstring(), since, LogChoice::Never, true);    // never: nothing is touched
    CHECK(fs::exists(channel / "Game.log") && fs::exists(channel / "Crashes" / "crash-1" / "crash.xml"));
    CleanSessionLogs(here.wstring(), channel.wstring(), since, LogChoice::Ask, false);     // ask with nobody to answer: keeps
    CHECK(fs::exists(channel / "Game.log") && fs::exists(channel / "logbackups" / "Game Build(1).log"));
    CleanSessionLogs(here.wstring(), channel.wstring(), since, LogChoice::Always, false);  // always: no question, this session only
    CHECK(!fs::exists(channel / "Game.log") && !fs::exists(channel / "logbackups" / "Game Build(1).log") &&
          !fs::exists(channel / "Crashes" / "crash-1" / "crash.xml"));
    CHECK(fs::exists(channel / "logbackups" / "older.log"));                               // older logs are never touched
    CHECK(Get(here / "sc-offline.ini") == ini);                                            // and the setting is not rewritten
}

// A harmless short-lived stand-in for the game: a copy of ping.exe named StarCitizen.exe in <root>/<name>/Bin64.
// A real launcher on the same PC would take it for the game while it lives, so it lives about 15 s at most
// (ping -n 15) and every stand-in is ended by its handle: by Reap, or by the exit handler below when a CHECK
// fails (CHECK exits without unwinding). The test asserts at its end that none is left.
struct Dummy { fs::path bin; HANDLE process = nullptr; DWORD pid = 0; ULONGLONG created = 0; };

static std::wstring Self() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
    CHECK(n && n < ARRAYSIZE(buf));
    return std::wstring(buf, n);
}

static std::vector<HANDLE>& LiveStandIns() { static std::vector<HANDLE> live; return live; }
static void ReapAllStandIns() {   // by handle only, never by image name
    for (HANDLE h : LiveStandIns()) TerminateProcess(h, 1);
}

// A stand-in StarCitizen.exe: a renamed copy of `tool` (a system tool by default) started with `args`.
static Dummy SpawnDummy(const fs::path& root, const char* name, fs::path tool = {}, std::wstring args = L"-n 15 127.0.0.1") {
    Dummy d;
    d.bin = root / name / "Bin64";
    fs::create_directories(d.bin);
    if (tool.empty()) tool = fs::path(EnvOr(L"SystemRoot", L"C:\\Windows")) / "System32" / "ping.exe";
    CHECK(fs::exists(tool));
    fs::copy_file(tool, d.bin / "StarCitizen.exe");
    wstring cmd = L"\"" + (d.bin / "StarCitizen.exe").wstring() + L"\" " + args;
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    CHECK(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    CloseHandle(pi.hThread);
    d.process = pi.hProcess;
    d.pid = pi.dwProcessId;
    static const bool handlerSet = (std::atexit(ReapAllStandIns), true);
    (void)handlerSet;
    LiveStandIns().push_back(d.process);
    FILETIME c{}, e{}, k{}, u{};
    CHECK(GetProcessTimes(d.process, &c, &e, &k, &u));
    d.created = FileTimeU64(c);
    return d;
}

static bool Alive(const Dummy& d) { return WaitForSingleObject(d.process, 0) == WAIT_TIMEOUT; }

// Ends a stand-in by its handle (never by image name) and waits for it.
static void Reap(Dummy& d) {
    if (d.process) {
        TerminateProcess(d.process, 1); WaitForSingleObject(d.process, 10000);
        auto& live = LiveStandIns();
        live.erase(std::remove(live.begin(), live.end(), d.process), live.end());
        CloseHandle(d.process); d.process = nullptr;
    }
}

// A directory junction (no privilege needed), made by the system's own cmd.exe; removed with RemoveDirectoryW,
// which takes the junction away and leaves its target alone.
static bool MakeJunction(const fs::path& link, const fs::path& target) {
    const fs::path cmdExe = fs::path(EnvOr(L"SystemRoot", L"C:\\Windows")) / "System32" / "cmd.exe";
    wstring cmd = L"\"" + cmdExe.wstring() + L"\" /c mklink /J \"" + link.wstring() + L"\" \"" + target.wstring() + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return false;
    CloseHandle(pi.hThread);
    DWORD code = 1;
    WaitForSingleObject(pi.hProcess, 20000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return code == 0 && fs::exists(link);
}

static bool Has(const std::vector<GameProc>& v, DWORD pid) {
    for (const GameProc& p : v) if (p.pid == pid) return true;
    return false;
}

static void TestStopTargets(const fs::path& root) {
    Gui& g = g_gui;
    Dummy a = SpawnDummy(root, "stop-a"), b = SpawnDummy(root, "stop-b");   // two installs, each with a StarCitizen.exe
    // Target selection is by the exact image path of this bin, never by file name.
    std::vector<GameProc> found = FindGameProcs(a.bin.wstring());
    CHECK(found.size() == 1 && found[0].pid == a.pid && found[0].created == a.created);
    CHECK(Has(FindGameProcs(b.bin.wstring()), b.pid) && !Has(FindGameProcs(b.bin.wstring()), a.pid));
    CHECK(FindGameProcs(L"").empty());                                          // no folder: nothing matches
    CHECK(FindGameProcs((root / "stop-none" / "Bin64").wstring()).empty());     // a folder with no process
    HANDLE self = GetCurrentProcess();
    CHECK(!ImageIsGame(self, a.bin.wstring()) && !ImageIsGame(a.process, L"") && ImageIsGame(a.process, a.bin.wstring()));
    CHECK(!ImageIsGame(a.process, b.bin.wstring()));
    // The window's "game is running" is this install's exe, not any process of that name: another install's
    // stand-in, an unknown folder and a folder without a process are all "not running"; the broad check (kept
    // for the update's swap guard only) still sees any of them.
    CHECK(GameRunningIn(a.bin.wstring()) && GameRunningIn(b.bin.wstring()));
    CHECK(!GameRunningIn((root / "stop-none" / "Bin64").wstring()) && !GameRunningIn(L""));
    CHECK(GameRunning());
    CHECK(GameMayBeRunningIn(a.bin.wstring()) && !GameMayBeRunningIn((root / "stop-none" / "Bin64").wstring()));

    // The same install spelled through a junction: the resolved image path of the process does not equal that text,
    // but it is the same file. Stop's selection, the window and the conservative guards all still see it, and the
    // other install's process is still not matched.
    {
        const fs::path link = root / "stop-junction";
        CHECK(MakeJunction(link, a.bin.parent_path()));
        const wstring viaLink = (link / "Bin64").wstring();
        CHECK(FullPath(viaLink) != FullPath(a.bin.wstring()));
        CHECK(ImageIsGame(a.process, viaLink) && !ImageIsGame(b.process, viaLink));
        const std::vector<GameProc> via = FindGameProcs(viaLink);
        CHECK(via.size() == 1 && via[0].pid == a.pid);
        CHECK(GameRunningIn(viaLink) && GameMayBeRunningIn(viaLink));
        RemoveDirectoryW(link.c_str());
        CHECK(!fs::exists(link) && fs::exists(a.bin / "StarCitizen.exe"));
    }

    // EndTargets refuses a target whose creation time differs (a reused id), a target of another folder, and no bin.
    int ended = 0, failed = 0;
    GameProc stale = found[0]; stale.created += 12345;
    EndTargets({ stale }, a.bin.wstring(), ended, failed);
    CHECK(ended == 0 && failed == 1 && Alive(a) && Alive(b));
    ended = failed = 0;
    EndTargets(found, b.bin.wstring(), ended, failed);                          // a's process, b's folder
    CHECK(ended == 0 && failed == 1 && Alive(a) && Alive(b));
    ended = failed = 0;
    EndTargets(found, L"", ended, failed);
    CHECK(ended == 0 && failed == 1 && Alive(a));

    // The Stop button's path. The ini names install a, so a is the game; b is another install.
    const fs::path here = root / "stop-here";
    Put(here / "sc-offline.ini", "game = " + a.bin.parent_path().string() + "\r\n");
    g.here = here.wstring();
    g.status = StatusReport{};
    CHECK(_wcsicmp(StopBin().c_str(), a.bin.wstring().c_str()) == 0 || fs::equivalent(fs::path(StopBin()), a.bin));
    g.gameUp = true;
    StopGame();                       // no windows exist for either process: still recorded, so End can find it
    CHECK(g.stopState == 1 && g.stopTargets.size() == 1 && g.stopTargets[0].pid == a.pid);
    CHECK(g.stopNote == T(LocaleKey::StopNoWindow));
    CHECK(Alive(a) && Alive(b));      // asking to close ended nothing
    g.stopAt -= kStopWaitSeconds + 1;
    TickStop();
    CHECK(g.stopState == 2);          // 60 s later the confirm is offered
    EndGame();                        // the confirm said yes
    CHECK(WaitForSingleObject(a.process, 10000) == WAIT_OBJECT_0);
    CHECK(Alive(b));                  // another install's process was never touched
    CHECK(g.stopState == 0 && g.stopTargets.empty() && g.note == T(LocaleKey::EndDone));

    // End with no recorded target does not scan for one.
    EndGame();
    CHECK(Alive(b) && g.note == T(LocaleKey::EndFailed));

    // No folder known (no game = and no panel result): Stop refuses and touches nothing.
    Put(here / "sc-offline.ini", "# no game here\r\n");
    g.status = StatusReport{};
    CHECK(StopBin().empty());
    StopGame();
    CHECK(g.stopState == 0 && g.stopTargets.empty() && g.note == T(LocaleKey::StopNoFolder) && Alive(b));
    // A game = that doesn't resolve also refuses, even if the panel found something else.
    Put(here / "sc-offline.ini", "game = " + (root / "nowhere").string() + "\r\n");
    g.status.bin = b.bin.wstring(); g.status.folderKnown = true;
    CHECK(StopBin().empty());
    StopGame();
    CHECK(g.stopState == 0 && Alive(b));
    // The panel's folder is used when the ini names none (remembered path / detection).
    Put(here / "sc-offline.ini", "# no game here\r\n");
    StopGame();
    CHECK(g.stopTargets.size() == 1 && g.stopTargets[0].pid == b.pid && Alive(b));   // recorded, not ended
    g.stopState = 0; g.stopTargets.clear(); g.gameUp = false; g.status = StatusReport{};

    Reap(a); Reap(b);
}

// Stop's request to the mod. The test plays the mod: it creates the two named events of the stand-in's pid and a
// helper thread answers the stop event. data/launcher.log must show each step, with the mod's answer before WM_CLOSE.
static std::string::size_type At(const std::string& text, const char* part) { return text.find(part); }

struct FakeMod {
    HANDLE stop = nullptr, stopped = nullptr;
    std::thread thread;
    std::atomic<bool> sawStop{false}, closeSeenEarly{false};
    std::atomic<ULONGLONG> ackTick{0};
    // `closeFile`: when it exists at the moment of the answer, a WM_CLOSE already reached the game window.
    FakeMod(DWORD pid, DWORD answerDelayMs, bool answers, fs::path closeFile = {}) {
        wchar_t name[64];
        sc_offline::stop_channel::EventName(name, L"stop", pid);
        stop = CreateEventW(nullptr, TRUE, FALSE, name);
        sc_offline::stop_channel::EventName(name, L"stopped", pid);
        stopped = CreateEventW(nullptr, TRUE, FALSE, name);
        CHECK(stop && stopped);
        if (answers)
            thread = std::thread([this, answerDelayMs, closeFile] {
                if (WaitForSingleObject(stop, 10000) != WAIT_OBJECT_0) return;
                sawStop = true;
                Sleep(answerDelayMs);   // the exit work takes a moment
                if (!closeFile.empty() && fs::exists(closeFile)) closeSeenEarly = true;
                ackTick = GetTickCount64();
                SetEvent(stopped);
            });
    }
    ~FakeMod() {
        SetEvent(stop);   // releases a helper still waiting
        if (thread.joinable()) thread.join();
        CloseHandle(stop); CloseHandle(stopped);
    }
};

// Stop is pressed and the window loop runs: StopGame returns at once and TickStop finishes the wait.
static ULONGLONG RunStop(ULONGLONG* returnedAfterMs = nullptr) {
    const ULONGLONG t0 = GetTickCount64();
    StopGame();
    if (returnedAfterMs) *returnedAfterMs = GetTickCount64() - t0;
    while (g_gui.stopState == 3) {
        TickStop();
        Sleep(5);
        CHECK(GetTickCount64() - t0 < 30000);
    }
    return GetTickCount64() - t0;
}

static void PointAt(const fs::path& here, const Dummy& d) {
    Put(here / "sc-offline.ini", "game = " + d.bin.parent_path().string() + "\r\n");
}

// The stand-in game with a real top-level window (this test's own exe run as StarCitizen.exe): it records, in
// closed.txt, the tick at which it receives WM_CLOSE.
static LRESULT CALLBACK StandinProc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_CLOSE) {
        wchar_t path[MAX_PATH];
        if (GetEnvironmentVariableW(L"SCO_STANDIN_DIR", path, MAX_PATH)) {
            Put(fs::path(path) / "closed.txt", std::to_string(GetTickCount64()));
        }
        DestroyWindow(w);
        return 0;
    }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(w, m, wp, lp);
}

static int RunStandinWindow(const fs::path& dir) {
    SetEnvironmentVariableW(L"SCO_STANDIN_DIR", dir.c_str());
    WNDCLASSW wc{};
    wc.lpfnWndProc = StandinProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ScoStopStandin";
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"Star Citizen stand-in", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 200, 100,
                             nullptr, nullptr, wc.hInstance, nullptr);
    if (!w) return 3;
    Put(dir / "ready.txt", "ready");
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}

static void TestLauncherLogAppend(const fs::path& root) {
    Gui& g = g_gui;
    const fs::path here = root / "log-here", data = here / "data";
    Put(data / "launcher.log", "old session\n");
    g.here = here.wstring();
    // The play child: one truncation at the start, then every write appends. Stop's lines go into the same
    // file between its writes, and neither side may overwrite the other.
    OpenLog(data.wstring(), false, "play");
    CHECK(g_log);
    Out("child line 1\n");
    StopLog("stop line 1");
    Out("child line 2\n");
    StopLog("stop line 2");
    Out("child line 3\n");
    std::fclose(g_log); g_log = nullptr;
    const std::string log = Get(data / "launcher.log");
    CHECK(At(log, "old session") == std::string::npos);   // the previous run was set aside, not mixed in
    CHECK(Get(data / "launcher.log.1") == "old session\n");
    CHECK(At(log, "child line 1") < At(log, "stop line 1") && At(log, "stop line 1") < At(log, "child line 2"));
    CHECK(At(log, "child line 2") < At(log, "stop line 2") && At(log, "stop line 2") < At(log, "child line 3"));
    // The helper appends too (append = true): still nothing is lost.
    OpenLog(data.wstring(), true, "helper");
    Out("helper line\n");
    StopLog("stop line 3");
    Out("helper line 2\n");
    std::fclose(g_log); g_log = nullptr;
    const std::string both = Get(data / "launcher.log");
    CHECK(Get(data / "launcher.log.1") == "old session\n");   // appending (the helper) rotates nothing
    CHECK(At(both, "child line 3") != std::string::npos && At(both, "stop line 3") != std::string::npos &&
          At(both, "helper line 2") != std::string::npos && At(both, "stop line 1") != std::string::npos);
}

// A new (non-status) run keeps the last session's lines, Stop/End among them, as launcher.log.1 and starts a new
// file; a second new run replaces .1 with the session before it. `status` never touches either (it does not open
// the log at all: TestStatusReadOnly).
static void TestLogRotation(const fs::path& root) {
    Gui& g = g_gui;
    const fs::path here = root / "rot-here", data = here / "data";
    g.here = here.wstring();
    fs::create_directories(data);
    OpenLog(data.wstring(), false, "run one");
    CHECK(g_log && !fs::exists(data / "launcher.log.1"));   // no earlier file: nothing to keep
    Out("session one\n");
    StopLog("Stop pressed: session one");
    std::fclose(g_log); g_log = nullptr;
    OpenLog(data.wstring(), false, "run two");
    Out("session two\n");
    std::fclose(g_log); g_log = nullptr;
    std::string kept = Get(data / "launcher.log.1"), now = Get(data / "launcher.log");
    CHECK(At(kept, "session one") != std::string::npos && At(kept, "Stop pressed: session one") != std::string::npos);
    CHECK(At(now, "session two") != std::string::npos && At(now, "session one") == std::string::npos);
    OpenLog(data.wstring(), false, "run three");
    std::fclose(g_log); g_log = nullptr;
    kept = Get(data / "launcher.log.1");
    CHECK(At(kept, "session two") != std::string::npos && At(kept, "session one") == std::string::npos);   // .1 is replaced
    // A held-open file that can't be renamed is appended to, never wiped.
    FILE* hold = _wfsopen((data / "launcher.log").c_str(), L"a", _SH_DENYNO);
    CHECK(hold);
    std::fputs("held line\n", hold); std::fflush(hold);
    OpenLog(data.wstring(), false, "run four");
    std::fclose(g_log); g_log = nullptr;
    std::fclose(hold);
    CHECK(At(Get(data / "launcher.log"), "held line") != std::string::npos);
}

static void TestStopChannel(const fs::path& root) {
    Gui& g = g_gui;
    Dummy a = SpawnDummy(root, "chan-a");
    const fs::path here = root / "chan-here";
    PointAt(here, a);
    fs::create_directories(here / "data");
    const fs::path logFile = here / "data" / "launcher.log";
    g.here = here.wstring();
    g.status = StatusReport{};
    g.gameUp = true;
    auto fresh = [&] { fs::remove(logFile); };
    const DWORD savedWait = g_stopAckWaitMs;
    const fs::path marker = a.bin / "sc-offline.installed";   // what Play leaves in the game folder
    ULONGLONG returned = 0, took = 0;
    std::string log;

    // No events (the mod is not loaded in the process): no wait, WM_CLOSE goes out directly, and it is logged.
    fresh();
    StopGame();
    CHECK(g.stopState == 1 && g.stopWait.empty());
    log = Get(logFile);
    CHECK(log.find("Stop pressed: 1 target(s), pid " + std::to_string(a.pid)) != std::string::npos);
    CHECK(At(log, "Stop pressed") < At(log, "no stop event") && At(log, "no stop event") < At(log, "WM_CLOSE posted to 0 window(s)"));
    CHECK(g.stopNote == T(LocaleKey::StopNoWindow) && !g.stopNoAnswer && Alive(a));

    // The same, but the mod is installed in the folder (this play): the window says the mod didn't answer.
    Put(marker, "time=now\n");
    fresh();
    StopGame();
    CHECK(g.stopState == 1 && g.stopNoAnswer);
    CHECK(g.stopNote == std::string(T(LocaleKey::StopNoAnswer)) + " " + T(LocaleKey::StopNoWindow));
    log = Get(logFile);
    CHECK(At(log, "no stop event") < At(log, "the mod did not answer; closing without its save") &&
          At(log, "the mod did not answer") < At(log, "WM_CLOSE posted"));

    // The mod answers after 300 ms. StopGame returns at once (the window never blocks), and WM_CLOSE goes out only
    // after the answer. No note about a missing answer.
    fresh();
    {
        FakeMod mod(a.pid, 300, true);
        g_stopAckWaitMs = 5000;
        took = RunStop(&returned);
        CHECK(returned < 150);
        CHECK(mod.sawStop && took >= 250 && took < 4000);
        CHECK(WaitForSingleObject(mod.stopped, 0) == WAIT_OBJECT_0);   // it was the mod's answer that ended the wait
        log = Get(logFile);
        CHECK(At(log, "mod acknowledged after") != std::string::npos);
        CHECK(At(log, "Stop pressed") < At(log, "mod acknowledged after") && At(log, "mod acknowledged after") < At(log, "WM_CLOSE posted"));
        CHECK(At(log, "no stop event") == std::string::npos && At(log, "did not answer") == std::string::npos);
        CHECK(g.stopState == 1 && !g.stopNoAnswer && g.stopNote == T(LocaleKey::StopNoWindow) && Alive(a));
    }
    // While waiting, the note says Saving and a second press does nothing.
    {
        FakeMod mod(a.pid, 200, true);
        StopGame();
        CHECK(g.stopState == 3 && g.stopNote == T(LocaleKey::StopSaving));
        StopGame();
        CHECK(g.stopState == 3 && g.stopWait.size() == 1);
        while (g.stopState == 3) { TickStop(); Sleep(5); }
        CHECK(g.stopState == 1);
    }

    // The mod never answers: Stop gives up after the wait, says so (the mod is expected here), and still posts WM_CLOSE.
    fresh();
    {
        FakeMod mod(a.pid, 0, false);
        g_stopAckWaitMs = 400;
        took = RunStop();
        CHECK(took >= 350);
        CHECK(WaitForSingleObject(mod.stop, 0) == WAIT_OBJECT_0 && WaitForSingleObject(mod.stopped, 0) == WAIT_TIMEOUT);
        log = Get(logFile);
        CHECK(At(log, "no acknowledgement after") < At(log, "WM_CLOSE posted"));
        CHECK(g.stopState == 1 && g.stopNoAnswer && g.stopNote.find(T(LocaleKey::StopNoAnswer)) == 0 && Alive(a));
    }
    g_stopAckWaitMs = savedWait;
    fs::remove(marker);

    // The rest of the sequence is logged: the End confirm after the wait, the answer, the process ended.
    g.stopAt -= kStopWaitSeconds + 1;
    TickStop();
    CHECK(g.stopState == 2);
    EndGame();
    CHECK(WaitForSingleObject(a.process, 10000) == WAIT_OBJECT_0);
    log = Get(logFile);
    CHECK(At(log, "End confirm shown") < At(log, "End answered: yes") && At(log, "End answered: yes") < At(log, "process ended"));
    CHECK(log.find("pid " + std::to_string(a.pid) + ": process ended") != std::string::npos);
    Reap(a);

    // A game that dies while Stop waits ends the wait at once, however long the wait would have been.
    Dummy c = SpawnDummy(root, "chan-c");
    PointAt(here, c);
    fresh();
    {
        FakeMod mod(c.pid, 0, false);
        g_stopAckWaitMs = 20000;
        StopGame();
        CHECK(g.stopState == 3);
        const ULONGLONG t0 = GetTickCount64();
        TerminateProcess(c.process, 1);   // by handle: our own stand-in
        while (g.stopState == 3) { TickStop(); Sleep(5); CHECK(GetTickCount64() - t0 < 5000); }
        log = Get(logFile);
        CHECK(At(log, "the process exited after") != std::string::npos && At(log, "the process exited") < At(log, "WM_CLOSE posted"));
        CHECK(g.stopState == 1);
    }
    g_stopAckWaitMs = savedWait;
    Reap(c);

    // A real top-level window: no WM_CLOSE may reach it before the mod's answer. The window process records the tick
    // at which WM_CLOSE arrives; the mod stand-in records the tick of its answer and whether a WM_CLOSE had already
    // arrived when it answered.
    const fs::path wdir = root / "chan-window";
    fs::create_directories(wdir);
    Dummy w = SpawnDummy(root, "chan-w", fs::path(Self()), L"--standin-window \"" + wdir.wstring() + L"\"");
    PointAt(here, w);
    for (int i = 0; i < 400 && !fs::exists(wdir / "ready.txt"); ++i) Sleep(25);
    CHECK(fs::exists(wdir / "ready.txt"));
    fresh();
    {
        FakeMod mod(w.pid, 400, true, wdir / "closed.txt");
        g_stopAckWaitMs = 8000;
        RunStop();
        CHECK(mod.sawStop && !mod.closeSeenEarly);
        CHECK(WaitForSingleObject(w.process, 10000) == WAIT_OBJECT_0);   // the window received WM_CLOSE and closed
        const ULONGLONG closedAt = std::stoull(Get(wdir / "closed.txt"));
        CHECK(mod.ackTick != 0 && closedAt >= mod.ackTick);
        log = Get(logFile);
        CHECK(At(log, "mod acknowledged after") < At(log, "WM_CLOSE posted to 1 window(s)"));
    }
    g_stopAckWaitMs = savedWait;
    Reap(w);

    g.stopState = 0; EndStopWait(); g.stopTargets.clear(); g.gameUp = false; g.status = StatusReport{};
}

// The built sc-offline.exe, copied into a scratch folder with a minimal package layout: `status` must leave the
// whole folder byte-identical (this covers the order of things in wmain, which RunStatus alone can't).
static void TestStatusExe(const fs::path& root, const fs::path& exe) {
    const fs::path here = root / "exe-here";
    const fs::path bin = MakeGame(root / "exe-game");
    fs::create_directories(here);
    fs::copy_file(exe, here / "sc-offline.exe");
    Put(here / "sc-offline.ini", "game = " + bin.parent_path().parent_path().string() + "\r\ncheck_updates = on\r\ndiscord_presence = off\r\n");
    Put(here / "data" / "plugins" / "discord" / "plugin.ini", "name = Discord\r\n");
    Put(here / "data" / "ships.txt", "ship\r\n");
    const auto before = Snapshot(here);
    wstring cmd = L"\"" + (here / "sc-offline.exe").wstring() + L"\" status";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    CHECK(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, here.c_str(), &si, &pi));
    CloseHandle(pi.hThread);
    const DWORD waited = WaitForSingleObject(pi.hProcess, 60000);
    if (waited != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);               // by handle: it is our own child
    CHECK(waited == WAIT_OBJECT_0);
    DWORD code = 99;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CHECK(code == 0);
    CHECK(Snapshot(here) == before);
    CHECK(!fs::exists(here / "data" / "launcher.log") && !fs::exists(here / "data" / "game-path.txt"));
    // And with no data folder at all.
    fs::remove_all(here / "data");
    const auto bare = Snapshot(here);
    cmd = L"\"" + (here / "sc-offline.exe").wstring() + L"\" status";
    PROCESS_INFORMATION p2{};
    CHECK(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, here.c_str(), &si, &p2));
    CloseHandle(p2.hThread);
    CHECK(WaitForSingleObject(p2.hProcess, 60000) == WAIT_OBJECT_0);
    CloseHandle(p2.hProcess);
    CHECK(Snapshot(here) == bare && !fs::exists(here / "data"));
}

static void ResetHome() {
    Gui& g = g_gui;
    g.running.clear(); g.tail.clear(); g.question.clear(); g.questionKind = 0;
    g.gameUp = g.pcLeft = g.hasLeftovers = false;
}

static void TestHomeState() {
    Gui& g = g_gui;
    ResetHome();
    // Repair (uninstall, safe to repeat) is usable whenever no command runs and the game is closed, leftovers or not.
    CHECK(CommandEnabled(kActRepair));
    g.pcLeft = true;
    CHECK(CommandEnabled(kActRepair));
    g.pcLeft = false; g.hasLeftovers = true;
    CHECK(CommandEnabled(kActRepair));
    g.gameUp = true;
    CHECK(!CommandEnabled(kActRepair) && !CommandEnabled(kActPlay) && !CommandEnabled(kActUpdate));
    g.gameUp = false; g.running = L"play";
    CHECK(!CommandEnabled(kActRepair) && !CommandEnabled(kActPlay));
    g.running.clear();
    CHECK(CommandEnabled(kActPlay) && CommandEnabled(kActUpdate));
    // Only Play and Update are home commands now; Install, Uninstall and Status are gone from the list.
    CHECK(ARRAYSIZE(kGuiCommands) == 2 && !wcscmp(kGuiCommands[0].cmd, L"play") && !wcscmp(kGuiCommands[1].cmd, L"update"));

    // The child's two prompts: yes/no, and the four-answer session-log prompt.
    AppendOutput(L"Delete these files? [y/N/always/never] ");
    CHECK(g.question == "Delete these files?" && g.questionKind == 1);
    AppendOutput(L"\nAre you sure? [y/N] ");
    CHECK(g.question == "Are you sure?" && g.questionKind == 0);
    AppendOutput(L"\nLogs:     kept\n");
    CHECK(g.question.empty() && g.questionKind == 0);
    ResetHome();
}

// The Status panel during a play session: the mod and the PC changes read as this session, from the live light
// state even when the last panel check is older than Play; and when a re-check is due.
static void TestStatusRows() {
    StatusReport r;
    r.checked = true;
    CHECK(ModRow(r, false, false).text == T(LocaleKey::ModNotInstalled) && !ModRow(r, false, false).warn);
    r.modInstalled = true;
    CHECK(ModRow(r, false, false).text == T(LocaleKey::ModInstalled) && ModRow(r, false, false).warn);   // left behind: a warning
    CHECK(ModRow(r, true, false).text == T(LocaleKey::ModInstalledSession) && !ModRow(r, true, false).warn);
    r.modInstalled = false;   // a panel check older than Play: the live state still says the mod is there
    CHECK(ModRow(r, true, true).text == T(LocaleKey::ModInstalledSession) && !ModRow(r, true, true).warn);
    CHECK(ModRow(r, true, false).text == T(LocaleKey::ModNotInstalled));
    CHECK(PcRow(r, false, false).text == T(LocaleKey::PcNone) && !PcRow(r, false, false).warn);
    CHECK(PcRow(r, true, true).text == T(LocaleKey::PcApplied) && !PcRow(r, true, true).warn);
    CHECK(PcRow(r, false, true).text == T(LocaleKey::PcNone));   // not playing: the live flag alone is not shown here
    r.pcLeft = "time=now\n";
    CHECK(PcRow(r, false, true).warn && PcRow(r, false, true).text != T(LocaleKey::PcApplied));
    CHECK(PcRow(r, true, true).text == T(LocaleKey::PcApplied) && !PcRow(r, true, true).warn);
    CHECK(std::string(T(LocaleKey::ModInstalledSession)) == "Installed (this session)" &&
          std::string(T(LocaleKey::PcApplied)) == "Applied for this session; undone when the game closes");

    // Re-check on any change of the game, a command, the leftovers or the PC record; while playing every 5 s.
    CHECK(!StatusRefreshWanted(false, false, false, false, false, false, false, false, 100000, 0));
    CHECK(StatusRefreshWanted(false, true, false, false, false, false, false, false, 10, 10));
    CHECK(StatusRefreshWanted(true, false, false, false, false, false, false, false, 10, 10));
    CHECK(StatusRefreshWanted(false, false, false, true, false, false, false, false, 10, 10));
    CHECK(StatusRefreshWanted(false, false, true, false, false, false, false, false, 10, 10));
    CHECK(StatusRefreshWanted(false, true, false, false, false, true, false, false, 10, 10));
    CHECK(StatusRefreshWanted(false, true, false, false, false, false, false, true, 10, 10));
    CHECK(!StatusRefreshWanted(true, true, false, false, false, false, false, false, 4000, 0));
    CHECK(StatusRefreshWanted(true, true, false, false, false, false, false, false, 5000, 0));
    CHECK(StatusRefreshWanted(false, false, true, true, false, false, false, false, 5000, 0));
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 3 && !wcscmp(argv[1], L"--standin-window")) return RunStandinWindow(argv[2]);   // the stand-in game of TestStopChannel
    CHECK(argc == 2 || argc == 3);
    SetEnvironmentVariableW(L"SC_OFFLINE_GUI", L"1");   // a failing `status` must not wait for Enter in a console
    fs::path owned(argv[1]);
    owned /= L"status-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    CHECK(fs::create_directories(owned));
    TestConfig(owned);
    TestExperimental(owned);
    TestMenuKey(owned);
    TestMergeIniKeepsNotes(owned);
    TestAnswers();
    TestIniWriter(owned);
    TestStatusReadOnly(owned);
    TestStatusReport(owned);
    TestSessionLogs(owned);
    TestStopTargets(owned);
    TestLauncherLogAppend(owned);
    TestLogRotation(owned);
    TestStopChannel(owned);
    TestHomeState();
    TestStatusRows();
    CHECK(LiveStandIns().empty());   // every stand-in was ended; none is left for a real launcher to find
    if (argc == 3) TestStatusExe(owned, fs::path(argv[2]));
    CHECK(fs::remove_all(owned) > 0);
    std::cout << "launcher status/config: " << checks << " checks passed; no child, helper, game or network command executed\n";
    return 0;
}
