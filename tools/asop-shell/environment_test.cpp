// SPDX-License-Identifier: GPL-3.0-only
// Process-local environment and admission policy fixture; no game or native hooks.
#include "../../src/asop_platform.cpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
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
struct ProductPaths {
    enum class Outcome { Resolved, Failed, Empty, Unterminated } outcome = Outcome::Resolved;
    std::filesystem::path root;
    int calls = 0;
    std::string requested;
};
static bool ResolveProductPath(void* context, char* path, std::size_t capacity, const char* file) {
    auto& paths = *static_cast<ProductPaths*>(context);
    ++paths.calls; paths.requested = file;
    if (paths.outcome == ProductPaths::Outcome::Failed) return false;
    if (paths.outcome == ProductPaths::Outcome::Empty) { if (capacity) *path = '\0'; return true; }
    if (paths.outcome == ProductPaths::Outcome::Unterminated) { std::memset(path, 'x', capacity); return true; }
    const auto resolved = (paths.root / file).string();
    const int length = std::snprintf(path, capacity, "%s", resolved.c_str());
    return length >= 0 && static_cast<std::size_t>(length) < capacity;
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

    std::filesystem::path dir;
    std::random_device random;
    for (int i = 0; i < 100; ++i) {
        const auto candidate = std::filesystem::temp_directory_path() / ("sco-asop-environment-test-" + std::to_string(random()));
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec)) { dir = candidate; break; }
        if (ec) { std::fprintf(stderr, "Cannot create test directory: %s\n", ec.message().c_str()); return 1; }
    }
    if (dir.empty()) { std::fprintf(stderr, "Cannot allocate test directory\n"); return 1; }
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

    // A pending replacement marker refuses startup: the file may be mid-commit.
    WriteFile(file, "Aurora\n");
    before = calls;
    WriteFile(dir / "asop_ships.txt.sco-1234-1.pending", "marker");
    CHECK(StartFromEnvironment(true, true, file.c_str()) == Result::InvalidConfig && calls == before);
    std::filesystem::remove(file);
    CHECK(StartFromEnvironment(true, true, file.c_str()) == Result::InvalidConfig && calls == before);
    std::filesystem::remove(dir / "asop_ships.txt.sco-1234-1.pending");
    CHECK(StartFromEnvironment(true, true, file.c_str()) == Result::Ok && captured.ships.empty() && calls == before + 1);
    before = calls;

    // The actual product startup seam distinguishes the owned subset from the
    // complete copied catalogue, while the general arbitrary-path API is retained.
    const auto catalogue = dir / "ships.txt";
    WriteFile(catalogue, "CatalogueOnly\nAnotherCatalogueClass\n");
    WriteFile(file, "OwnedAurora\nOwnedHornet\n");
    ProductPaths paths; paths.root = dir;
    CHECK(StartFromEnvironment(true, true, catalogue.string().c_str()) == Result::Ok &&
          captured.ships == std::vector<std::string>({"CatalogueOnly", "AnotherCatalogueClass"}) && calls == before + 1);
    before = calls;
    CHECK(StartFromProductEnvironment(true, true, ResolveProductPath, &paths) == Result::Ok && calls == before + 1);
    const std::vector<std::string> owned{"OwnedAurora", "OwnedHornet"};
    CHECK(paths.calls == 1 && paths.requested == "asop_ships.txt" && captured.ships == owned);
    CHECK(captured.enabled && captured.catalog == sco::game::asop_host::CatalogMode::Ships && passedOffline && passedEac);
    before = calls;
    CHECK(StartFromProductEnvironment(true, true, nullptr) == Result::InvalidConfig && calls == before);
    for (const auto outcome : {ProductPaths::Outcome::Failed, ProductPaths::Outcome::Empty, ProductPaths::Outcome::Unterminated}) {
        paths.outcome = outcome;
        const int resolvedBefore = paths.calls;
        CHECK(StartFromProductEnvironment(true, true, ResolveProductPath, &paths) == Result::InvalidConfig &&
              calls == before && paths.calls == resolvedBefore + 1);
        CHECK(captured.ships == owned && captured.enabled && passedOffline && passedEac &&
              captured.catalog == sco::game::asop_host::CatalogMode::Ships);
    }
    // Invalid policy and admission refusal precede path resolution and provider Start.
    const int resolvedBefore = paths.calls;
    CHECK(StartFromProductEnvironment(false, true, ResolveProductPath, &paths) == Result::AdmissionRefused && calls == before);
    CHECK(StartFromProductEnvironment(true, false, ResolveProductPath, &paths) == Result::AdmissionRefused && calls == before);
    CHECK(Set(mode, "invalid"));
    CHECK(StartFromProductEnvironment(true, true, ResolveProductPath, &paths) == Result::InvalidConfig && calls == before);
    CHECK(Set(mode, "")); CHECK(Set(fleet, "invalid"));
    CHECK(StartFromProductEnvironment(true, true, ResolveProductPath, &paths) == Result::InvalidConfig && calls == before);
    CHECK(paths.calls == resolvedBefore && captured.ships == owned);
    // Disabled configuration still reaches Start with the original admission flags,
    // and Game mode never needs a selection resolver at all.
    CHECK(Set(mode, "off")); CHECK(Set(fleet, "ships"));
    CHECK(StartFromProductEnvironment(false, false, ResolveProductPath, &paths) == Result::Ok && calls == before + 1);
    CHECK(!captured.enabled && !passedOffline && !passedEac && captured.ships.empty() && paths.calls == resolvedBefore);
    before = calls;
    CHECK(Set(fleet, "invalid"));
    CHECK(StartFromProductEnvironment(false, false, ResolveProductPath, &paths) == Result::InvalidConfig && calls == before);
    CHECK(Set(mode, "")); CHECK(Set(fleet, "game"));
    CHECK(StartFromProductEnvironment(true, true, nullptr) == Result::Ok && calls == before + 1);
    CHECK(captured.enabled && captured.catalog == sco::game::asop_host::CatalogMode::Game && captured.ships.empty());
    CHECK(paths.calls == resolvedBefore);

    std::filesystem::remove_all(dir);
    CHECK(Set(mode, "")); CHECK(Set(fleet, ""));
    std::printf("ASOP environment policy: %d failures\n", failures);
    return failures ? 1 : 0;
}
