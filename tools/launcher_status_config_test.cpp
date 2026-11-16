// SPDX-License-Identifier: GPL-3.0-only
// Launcher logic that needs no window and no game: the clean_logs setting and its writer, the session-log
// answers, the read-only `status` command and the Status panel's checks, and the Repair/Stop gating. The real
// launcher source is included, so these are the shipped functions. No child, helper, game or network call runs.
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

static void ResetHome() {
    Gui& g = g_gui;
    g.running.clear(); g.tail.clear(); g.question.clear(); g.questionKind = 0;
    g.gameUp = g.pcLeft = g.hasLeftovers = false;
}

static void TestHomeState() {
    Gui& g = g_gui;
    ResetHome();
    // Repair (the old Uninstall) needs something left to undo and no game; the crash banner shows the same condition.
    CHECK(!CommandEnabled(kActRepair));
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
    CHECK(argc == 2);
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
    TestHomeState();
    CHECK(fs::remove_all(owned) > 0);
    std::cout << "launcher status/config: " << checks << " checks passed; no child, helper, game or network command executed\n";
    return 0;
}
