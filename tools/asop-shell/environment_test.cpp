// SPDX-License-Identifier: GPL-3.0-only
// Process-local environment and admission policy fixture; no game or native hooks.
#include "../../src/asop_platform.cpp"
#include <cstdio>
#include <string>

namespace offline::asop {
static Options captured;
static int calls = 0;
static bool passedOffline = false, passedEac = false;
bool ParseShips(const std::string&, std::vector<std::string>&) { return false; }
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
    CHECK(Set(mode, "")); CHECK(Set(fleet, ""));
    std::printf("ASOP environment policy: %d failures\n", failures);
    return failures ? 1 : 0;
}
