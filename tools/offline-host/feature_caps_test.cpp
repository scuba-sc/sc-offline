// SPDX-License-Identifier: GPL-3.0-only
// The product's capabilities (src/feature_caps.h) against Core's status registry.
//
//   feature_caps_test <path to Core's sdk/status_registry.json>
//
// Core lists the product capabilities ("teleport", "spawn.ship") with default ON, and its SDK docs
// tell plugins to gate on has("teleport") / has("spawn.ship"). So each must follow the rows behind
// it, never a fixed OFF, and a capability the product holds off on purpose must say so. Synthetic:
// no game, no Windows. Not run in game.
#include "feature_caps.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fc = sc_offline::feature_caps;

namespace {
int failed = 0;
#define CHECK(x) do { if (!(x)) { ++failed; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

// What sco::caps::Set was asked for. The fake refuses a name the product does not own, so a
// misspelt capability name is a failure here, not a silent yes.
struct Set { std::string name; bool ready; std::string reason; bool hadReason; };
std::vector<Set> sets;
// Names the fake answers yes to: every name in Core's registry, plus kProductPrivate. Nothing else,
// so a misspelt capability name comes back NotFound instead of passing.
std::vector<std::string> knownNames;
// Product capabilities Core's registry has no entry for. A name added to src/feature_caps.h must
// be in Core's registry or be listed here on purpose, so a typo cannot hide behind this list.
const char* const kProductPrivate[] = { "offline", "npc", "quantum", "contracts" };
}  // namespace

namespace sco::caps {
Result Set(const char* name, bool ready, const char* reason) {
    for (const std::string& known : knownNames)
        if (known == name) { sets.push_back({ name, ready, reason ? reason : "", reason != nullptr }); return Result::Ok; }
    return Result::NotFound;
}
}  // namespace sco::caps

namespace {
// Product capabilities in Core's registry: the entries whose display says "the product's
// capability" (not game.* service capabilities, not Core's own such as mining.cell), with default.
std::map<std::string, std::string> RegistryProductCaps(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::map<std::string, std::string> out;
    static const std::regex entry(R"re(\{"name":\s*"([^"]+)"[^{}]*?"display":\s*"[^"]*product's capability[^"]*"[^{}]*?"default":\s*"(ON|OFF)")re");
    for (std::sregex_iterator it(text.begin(), text.end(), entry), end; it != end; ++it) {
        const std::string name = (*it)[1];
        if (name.rfind("game.", 0) != 0) out[name] = (*it)[2];
    }
    return out;
}

// Every capability name Core's registry lists (product capabilities and game.* alike).
std::vector<std::string> RegistryNames(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> out;
    static const std::regex name(R"re("name":\s*"([^"]+)")re");
    for (std::sregex_iterator it(text.begin(), text.end(), name), end; it != end; ++it) out.push_back((*it)[1]);
    return out;
}

const fc::State* Find(const fc::State* states, size_t n, const char* name) {
    for (size_t i = 0; i < n; ++i) if (!std::strcmp(states[i].name, name)) return &states[i];
    return nullptr;
}

bool Held(const char* name) {
    for (const char* held : fc::kHeldOff) if (!std::strcmp(held, name)) return true;
    return false;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::puts("usage: feature_caps_test <status_registry.json>"); return 2; }
    const auto registry = RegistryProductCaps(argv[1]);
    // The two product capabilities Core documents today. A third appearing in the registry must be
    // handled here and in feature_caps.h, so this fails until it is.
    if (registry.size() != 2)
        std::printf("FAIL: Core's registry now lists %zu product capabilities; add the new one to src/feature_caps.h and this test\n", registry.size());
    CHECK(registry.size() == 2);
    CHECK(registry.count("teleport") == 1 && registry.count("spawn.ship") == 1);

    fc::Readiness allReady; allReady.offline = allReady.teleportRows = allReady.spawner = allReady.contracts = true;
    fc::Readiness noneReady;
    fc::State ready[fc::kMaxStates], notReady[fc::kMaxStates];
    const size_t nReady = fc::States(allReady, ready), nNot = fc::States(noneReady, notReady);
    CHECK(nReady == nNot && nReady > 0 && nReady <= fc::kMaxStates);

    for (const auto& [name, def] : registry) {
        const fc::State* on = Find(ready, nReady, name.c_str());
        const fc::State* off = Find(notReady, nNot, name.c_str());
        CHECK(on && off);
        if (!on || !off) continue;
        if (name == "teleport") {                          // the one registry capability the product holds off by decision
            CHECK(Held("teleport") && !on->ready && on->reason && std::strstr(on->reason, "held off by the product"));
            continue;
        }
        CHECK(!Held(name.c_str()));                       // Core promises it; the product does not hold it off
        CHECK((def == "ON") == on->ready);                 // rows resolved: the registry default
        CHECK(on->reason == nullptr);
        CHECK(!off->ready && off->reason && *off->reason); // rows missing: off, with a reason
    }

    // Every capability the product holds off on purpose says so, never blames the game build;
    // every other one is ready with its rows and carries a reason when it is not.
    for (size_t i = 0; i < nNot; ++i) {
        const fc::State& off = notReady[i];
        CHECK(off.reason && *off.reason);
        CHECK(!ready[i].ready == Held(off.name));
        if (Held(off.name)) {
            CHECK(std::strstr(off.reason, "held off by the product") != nullptr);
            CHECK(!ready[i].ready && ready[i].reason && *ready[i].reason);
        } else {
            CHECK(std::strstr(off.reason, "held off") == nullptr);
            CHECK(ready[i].ready && ready[i].reason == nullptr);
        }
    }
    // The product capability "teleport" stays off even when its rows resolve (product decision).
    fc::Readiness onlyTeleport; onlyTeleport.teleportRows = true;
    fc::State partial[fc::kMaxStates];
    const size_t nPartial = fc::States(onlyTeleport, partial);
    CHECK(!Find(partial, nPartial, "teleport")->ready);
    CHECK(!Find(partial, nPartial, "spawn.ship")->ready);
    fc::Readiness onlySpawner; onlySpawner.spawner = true;
    const size_t nSpawner = fc::States(onlySpawner, partial);
    CHECK(!Find(partial, nSpawner, "teleport")->ready);
    CHECK(Find(partial, nSpawner, "spawn.ship")->ready && Find(partial, nSpawner, "npc")->ready);

    // Apply sets each name exactly once through sco::caps::Set, and only names the product owns.
    knownNames = RegistryNames(argv[1]);
    CHECK(knownNames.size() > 20);                       // the registry was really read
    for (const char* priv : kProductPrivate) knownNames.push_back(priv);
    // Every product name is in Core's registry or deliberately product-private, none of both.
    for (size_t i = 0; i < nReady; ++i) {
        size_t hits = 0;
        for (const std::string& known : knownNames) if (known == ready[i].name) ++hits;
        if (hits == 0) std::printf("FAIL: product capability '%s' is neither in Core's registry nor product-private\n", ready[i].name);
        CHECK(hits >= 1);
    }
    for (const char* priv : kProductPrivate) CHECK(registry.count(priv) == 0);
    fc::Apply(allReady, nullptr);
    CHECK(sets.size() == nReady);
    for (size_t i = 0; i < sets.size() && i < nReady; ++i) {
        CHECK(sets[i].name == ready[i].name && sets[i].ready == ready[i].ready);
        CHECK(sets[i].hadReason == !ready[i].ready);
    }
    sets.clear();
    // A name Core does not know (a misspelling) is refused and reaches the log callback.
    knownNames.erase(std::find(knownNames.begin(), knownNames.end(), std::string("npc")));
    static int refused = 0;
    fc::Apply(allReady, [](const char*, sco::Result r) { if (r == sco::Result::NotFound) ++refused; });
    CHECK(refused == 1);
    CHECK(sets.size() == nReady - 1);

    if (failed) { std::printf("feature_caps_test: %d failure(s)\n", failed); return 1; }
    std::puts("product capabilities follow the registry defaults and say why when held off: PASS (synthetic, not run in game)");
    return 0;
}
