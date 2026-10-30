// SPDX-License-Identifier: GPL-3.0-only
// Exercise the product's actual preflight before core can install host hooks.
#include "offline_host.h"
#include "sco/log.h"
#include "sco/signature_registry.h"
#include <string>

namespace gh = sco::game::host;
static int inspections = 0;
static std::string diagnostic;
static void RecordLog(const char* line) { if (line) diagnostic += line; }
static gh::Admission Inspect() {
    ++inspections;
    return {gh::Session::Offline, gh::Check::Clear, gh::Check::Clear, gh::Check::Clear};
}

int main() {
    unsigned char bytes[8192]{};
    sco::Image image{bytes, {bytes, 4096}, {bytes + 4096, 4096}, {}, 0, sizeof(bytes)};
    gh::Options options;
    options.inspect = Inspect;
    options.image = &image;
    options.platform.gameServices = true;
    sco::SetLogSink(RecordLog);
    if (offline_host::Prepare(options) != sco::Result::Failed) return 1;
    if (inspections != 0 || gh::Status().phase != gh::Phase::Idle) return 2;
    if (sco::AcquireHostSignatureRegistry()) return 3;
    if (diagnostic.find("native reader barrier") == std::string::npos) return 4;
    return 0;
}
