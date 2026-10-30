// SPDX-License-Identifier: GPL-3.0-only
// Exercise the product row diagnostics with the real registry, scanner and callbacks.
#include "signature_rows.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
static int callbacks = 0;
static sco::SigResult Dependent(const sco::Image&, const sco::SigResolveContext& ctx) {
    ++callbacks;
    return sco::SigOk(ctx.Sig("pattern"));
}

int main() {
    sco::SignatureRegistry registry;
    const sco::SigDef defs[] = {
        {"pattern", "AA BB", 0, 0, nullptr, {}},
        {"dependent", nullptr, 0, 0, Dependent, {"pattern"}}
    };
    const sco::SignatureTable table{defs, 2};
    CHECK(registry.RegisterTables(&table, 1).status == sco::SigRegistryStatus::Ok);
    CHECK(registry.Freeze() == sco::SigRegistryStatus::Ok);
    unsigned char firstBytes[]{0xAA, 0xBB, 0x00};
    sco::Image first{}; first.base = firstBytes; first.text = {firstBytes, sizeof(firstBytes)};
    CHECK(registry.Resolve(first) == sco::SigRegistryStatus::Ok);
    auto firstSnapshot = registry.Snapshot();
    CHECK(firstSnapshot && callbacks == 1);
    const char* both[]{"pattern", "dependent"};
    char why[192]{};
    CHECK(offline::signature_rows::Ready(firstSnapshot.get(), both, 2, why, sizeof(why)));
    unsigned char secondBytes[]{0x00, 0x00, 0x00};
    sco::Image second{}; second.base = secondBytes; second.text = {secondBytes, sizeof(secondBytes)};
    CHECK(registry.Resolve(second) == sco::SigRegistryStatus::Ok);
    auto secondSnapshot = registry.Snapshot();
    CHECK(secondSnapshot && callbacks == 1); // failed dependency never invokes the callback
    CHECK(!offline::signature_rows::Ready(secondSnapshot.get(), both, 2, why, sizeof(why)));
    CHECK(std::strcmp(why, "needs pattern (MISSING)") == 0);
    const char* blocked[]{"dependent"};
    CHECK(!offline::signature_rows::Ready(secondSnapshot.get(), blocked, 1, why, sizeof(why)));
    CHECK(std::strcmp(why, "needs dependent (BLOCKED)") == 0);
    const char* unknown[]{"not.registered"};
    CHECK(!offline::signature_rows::Ready(firstSnapshot.get(), unknown, 1, why, sizeof(why)));
    CHECK(std::strcmp(why, "unknown signature not.registered") == 0);
    CHECK(!offline::signature_rows::Ready(nullptr, both, 2, why, sizeof(why)));
    CHECK(std::strcmp(why, "signature registry unavailable") == 0);
    CHECK(offline::signature_rows::Ready(firstSnapshot.get(), both, 2, why, sizeof(why)));
    std::printf("signature row diagnostics: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
