// SPDX-License-Identifier: GPL-3.0-only
// Exercise product logic when a retired/replaced registry refuses a capability write.
#include "world_caps.h"
#include <cstdio>

namespace {
sco::Result setResult = sco::Result::Unavailable;
bool ready = true;
int hasCalls = 0;
const char* const rows[]{"synthetic.row"};
const sco::game::world::Capability fixtureCaps[]{{"synthetic.cap", rows, 1}};
}
namespace sco::game::world {
const Capability* Capabilities(size_t& n) { n = 1; return fixtureCaps; }
}
namespace sco::caps {
Result SetFromSignatures(const char*, const char* const*, size_t) { return setResult; }
bool Has(const char*) { ++hasCalls; return ready; }
}
int main() {
    if (WorldCapability("synthetic.cap") || hasCalls) return 1;
    setResult = sco::Result::Ok;
    if (!WorldCapability("synthetic.cap") || hasCalls != 1) return 2;
    ready = false;
    if (WorldCapability("synthetic.cap") || hasCalls != 2) return 3;
    if (WorldCapability("unknown") || hasCalls != 2) return 4;
    std::puts("stale capability refusal: PASS (synthetic)");
}
