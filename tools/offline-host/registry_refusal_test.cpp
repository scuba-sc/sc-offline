// SPDX-License-Identifier: GPL-3.0-only
// Real pinned core plus the product seam; all image bytes and callbacks are synthetic.
// This checks refusal and registry non-interference, not native reader exclusion.
#include "../../src/offline_host.h"
#include "sco/signature_registry.h"
#include <cstdio>
#include <memory>

namespace gh = sco::game::host;
namespace {
int inspectCalls = 0;
int barrierCalls = 0;
gh::Admission Inspect() {
    ++inspectCalls;
    return {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
}
// Deliberately optimistic trap: product preflight must refuse without invoking it.
// This callback is not a native barrier and is never supplied by the product.
bool Barrier(void*) { ++barrierCalls; return true; }
int failures = 0;
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

void CheckRefusal(const gh::Options& options) {
    CHECK(offline_host::Prepare(options) == sco::Result::Unavailable);
    CHECK(offline_host::Start() == sco::Result::Unavailable);
    CHECK(offline_host::Tick(1) == sco::Result::Unavailable);
    CHECK(!offline_host::Running());
    CHECK(!offline_host::RestartRequired());
    CHECK(inspectCalls == 0 && barrierCalls == 0);
    const auto status = gh::Status();
    CHECK(status.phase == gh::Phase::Idle);
    CHECK(!status.signaturesResolved && !status.quitHookInstalled && !status.dataCoreActive);
}
}

int main() {
    gh::Options options;
    options.inspect = Inspect;
    options.platform.gameServices = true;
    options.platform.gameReadersQuiesced = Barrier;
    options.preEngineQuiesced = true;
    options.nativeDataCoreReads = true;

    CHECK(!sco::AcquireHostSignatureSnapshot());
    CheckRefusal(options);
    CHECK(!sco::AcquireHostSignatureSnapshot());

    auto registry = std::make_shared<sco::SignatureRegistry>();
    const sco::SigDef row{"synthetic.row", "AA BB", 0, 0, nullptr, {}};
    const sco::SignatureTable table{&row, 1};
    unsigned char bytes[]{0xAA, 0xBB};
    sco::Image image{}; image.base = bytes; image.size = sizeof(bytes); image.text = {bytes, sizeof(bytes)};
    CHECK(registry->RegisterTables(&table, 1).status == sco::SigRegistryStatus::Ok);
    CHECK(registry->Freeze() == sco::SigRegistryStatus::Ok);
    CHECK(registry->Resolve(image) == sco::SigRegistryStatus::Ok);
    CHECK(sco::InstallHostSignatureRegistry(registry, nullptr));
    auto snapshot = sco::AcquireHostSignatureSnapshot();
    CHECK(snapshot && snapshot->Lookup("synthetic.row") &&
          snapshot->Lookup("synthetic.row")->state == sco::SigState::Ok);

    CheckRefusal(options);
    CHECK(sco::AcquireHostSignatureRegistry() == registry);
    CHECK(sco::AcquireHostSignatureSnapshot() == snapshot);
    CHECK(snapshot && snapshot->Lookup("synthetic.row") &&
          snapshot->Lookup("synthetic.row")->at == bytes);
    CHECK(sco::ClearHostSignatureRegistry(registry, nullptr));
    CHECK(!sco::AcquireHostSignatureSnapshot());
    std::printf("product registry refusal: %d failure(s) (synthetic)\n", failures);
    return failures ? 1 : 0;
}
