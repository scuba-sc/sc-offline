// SPDX-License-Identifier: GPL-3.0-only
// Process-local environment and admission policy fixture; no game or native hooks.
#include "../../src/asop_platform.cpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace offline::asop {
static Options captured;
static int calls = 0;
static bool passedOffline = false, passedEac = false;
sco::game::asop_host::Result Start(const Options& options, bool offline, bool eac) {
    captured = options; passedOffline = offline; passedEac = eac; ++calls;
    return sco::game::asop_host::Result::Ok;
}
}
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL line %d: %s\n", __LINE__, #x); ++failures; } } while (0)
static bool Set(const char* key, const std::string& value) {
#ifdef _WIN32
    return _putenv_s(key, value.c_str()) == 0;
#else
    return value.empty() ? unsetenv(key) == 0 : setenv(key, value.c_str(), 1) == 0;
#endif
}
static void WriteFile(const std::filesystem::path& file, const std::string& bytes) {
    std::ofstream out(file, std::ios::binary);
    out << bytes;
}
int main() {
    using namespace offline::asop;
    using Result = sco::game::asop_host::Result;
    const char* mode = "SC_OFFLINE_ASOP";
    const char* fleet = "SC_OFFLINE_ASOP_FLEET_LIST";
    std::string value = "stale";
    CHECK(Set(mode, "")); CHECK(Set(fleet, ""));
    CHECK(Environment(mode, value) && value.empty());
    CHECK(Set(mode, "MiXeD")); CHECK(Environment(mode, value) && value == "mixed");
    CHECK(Set(mode, std::string(31, 'A'))); CHECK(Environment(mode, value) && value == std::string(31, 'a'));
    CHECK(Set(mode, std::string(32, 'A'))); CHECK(!Environment(mode, value));
    calls = 0; CHECK(StartFromEnvironment(true, true, nullptr) == Result::InvalidConfig && calls == 0);
    CHECK(Set(mode, "")); CHECK(Set(fleet, std::string(32, 'A')));
    CHECK(StartFromEnvironment(true, true, nullptr) == Result::InvalidConfig && calls == 0);
    CHECK(Set(fleet, ""));
    CHECK(StartFromEnvironment(false, true, nullptr) == Result::AdmissionRefused && calls == 0);
    CHECK(StartFromEnvironment(true, false, nullptr) == Result::AdmissionRefused && calls == 0);
    CHECK(StartFromEnvironment(true, true, nullptr) == Result::Ok && calls == 1);
    CHECK(captured.enabled && passedOffline && passedEac);
    CHECK(Set(mode, "OFF"));
    CHECK(StartFromEnvironment(false, false, nullptr) == Result::Ok && calls == 2);
    CHECK(!captured.enabled && !passedOffline && !passedEac);

    // Ships mode: strict parsing, then file-state rules. A missing file in an existing directory is an explicit empty selection.
    CHECK(Set(mode, "")); CHECK(Set(fleet, "ships"));
    std::vector<std::string> names;
    CHECK(ParseShips("", names) && names.empty());
    CHECK(ParseShips("# only comments\n\n \t \n", names) && names.empty());
    CHECK(ParseShips("  alpha \t\r\nbeta # note\r\n", names) && names == std::vector<std::string>({"alpha", "beta"}));
    CHECK(ParseShips("Hornet\nhornet\nAURORA\naurora\n", names) && names == std::vector<std::string>({"Hornet", "AURORA"}));
    CHECK(ParseShips(std::string(63, 'a') + "\n", names) && names.size() == 1);
    CHECK(!ParseShips(std::string(64, 'a') + "\n", names));
    CHECK(!ParseShips("two words\n", names));
    CHECK(!ParseShips("caf\xC3\xA9\n", names));
    CHECK(!ParseShips("\xEF\xBB\xBF" "alpha\n", names));
    CHECK(!ParseShips("good\nbad name\n", names));

    const auto dir = std::filesystem::temp_directory_path() / "sco-asop-environment-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const std::string file = (dir / "asop_ships.txt").string();
    int before = calls;
    CHECK(StartFromEnvironment(true, true, nullptr) == Result::InvalidConfig && calls == before);
    CHECK(StartFromEnvironment(true, true, "") == Result::InvalidConfig && calls == before);
    CHECK(StartFromEnvironment(false, true, nullptr) == Result::AdmissionRefused && calls == before);
    CHECK(StartFromEnvironment(true, true, (dir / "missing" / "asop_ships.txt").string().c_str()) == Result::InvalidConfig && calls == before);
    std::filesystem::create_directory(dir / "isdir");
    CHECK(StartFromEnvironment(true, true, (dir / "isdir").string().c_str()) == Result::InvalidConfig && calls == before);
    CHECK(StartFromEnvironment(true, true, file.c_str()) == Result::Ok && captured.ships.empty() && calls == before + 1);
    before = calls;
    WriteFile(file, "Aurora\r\nhornet # note\n");
    CHECK(StartFromEnvironment(true, true, file.c_str()) == Result::Ok && captured.ships == std::vector<std::string>({"Aurora", "hornet"}) && calls == before + 1);
    before = calls;
    WriteFile(file, std::string(kMaxSelectionBytes, '#'));
    CHECK(StartFromEnvironment(true, true, file.c_str()) == Result::Ok && calls == before + 1);
    before = calls;
    WriteFile(file, std::string(kMaxSelectionBytes + 1, '#'));
    CHECK(StartFromEnvironment(true, true, file.c_str()) == Result::InvalidConfig && calls == before);
    std::string full;
    for (int i = 0; i < 2048; ++i) full += "ship" + std::to_string(i) + "\n";
    before = calls;
    WriteFile(file, full + "SHIP0\n");
    CHECK(StartFromEnvironment(true, true, file.c_str()) == Result::Ok && captured.ships.size() == 2048 && calls == before + 1);
    before = calls;
    WriteFile(file, full + "extra\n");
    CHECK(StartFromEnvironment(true, true, file.c_str()) == Result::InvalidConfig && calls == before);
    std::filesystem::remove_all(dir);
    CHECK(Set(mode, "")); CHECK(Set(fleet, ""));
    std::printf("ASOP environment policy: %d failures\n", failures);
    return failures ? 1 : 0;
}
