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
#include <map>

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

// A harmless long-running stand-in for the game: a copy of ping.exe named StarCitizen.exe in <root>/<name>/Bin64.
struct Dummy { fs::path bin; HANDLE process = nullptr; DWORD pid = 0; ULONGLONG created = 0; };

static Dummy SpawnDummy(const fs::path& root, const char* name) {
    Dummy d;
    d.bin = root / name / "Bin64";
    fs::create_directories(d.bin);
    const fs::path tool = fs::path(EnvOr(L"SystemRoot", L"C:\\Windows")) / "System32" / "ping.exe";
    CHECK(fs::exists(tool));
    fs::copy_file(tool, d.bin / "StarCitizen.exe");
    wstring cmd = L"\"" + (d.bin / "StarCitizen.exe").wstring() + L"\" -n 120 127.0.0.1";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    CHECK(CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    CloseHandle(pi.hThread);
    d.process = pi.hProcess;
    d.pid = pi.dwProcessId;
    FILETIME c{}, e{}, k{}, u{};
    CHECK(GetProcessTimes(d.process, &c, &e, &k, &u));
    d.created = FileTimeU64(c);
    return d;
}

static bool Alive(const Dummy& d) { return WaitForSingleObject(d.process, 0) == WAIT_TIMEOUT; }

// Ends a stand-in by its handle (never by image name) and waits for it.
static void Reap(Dummy& d) {
    if (d.process) { TerminateProcess(d.process, 1); WaitForSingleObject(d.process, 10000); CloseHandle(d.process); d.process = nullptr; }
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

int wmain(int argc, wchar_t** argv) {
    CHECK(argc == 2 || argc == 3);
    SetEnvironmentVariableW(L"SC_OFFLINE_GUI", L"1");   // a failing `status` must not wait for Enter in a console
    fs::path owned(argv[1]);
    owned /= L"status-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    CHECK(fs::create_directories(owned));
    TestConfig(owned);
    TestAnswers();
    TestIniWriter(owned);
    TestStatusReadOnly(owned);
    TestStatusReport(owned);
    TestSessionLogs(owned);
    TestStopTargets(owned);
    TestHomeState();
    if (argc == 3) TestStatusExe(owned, fs::path(argv[2]));
    CHECK(fs::remove_all(owned) > 0);
    std::cout << "launcher status/config: " << checks << " checks passed; no child, helper, game or network command executed\n";
    return 0;
}
