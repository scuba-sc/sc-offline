// SPDX-License-Identifier: GPL-3.0-only
#include "../src/menu_input.h"
#include <cstdio>
#include <vector>
int main() {
    offline::menu::DeferredInput input;
    std::vector<uint32_t> seen;
    // Synchronous messages delivered while game-thread frame owns ImGui.
    input.Push(0x201, 1, 2); // WM_LBUTTONDOWN
    input.Push(0x202, 0, 2); // WM_LBUTTONUP
    input.Push(0x100, 65, 0); // WM_KEYDOWN
    input.Push(0x102, 65, 0); // WM_CHAR
    input.Drain([&](const auto& message) { seen.push_back(message.message); });
    const bool pass = seen == std::vector<uint32_t>{0x201, 0x202, 0x100, 0x102} && input.empty();
    input.Drain([&](const auto&) { seen.push_back(0); });
    if (!pass || seen.size() != 4) { std::puts("FAIL deferred menu input ordering/exactly-once"); return 1; }
    for (unsigned i=0;i<1025;++i) input.Push(0x200,i,0);
    if (!input.TakeOverflow() || !input.empty() || input.TakeOverflow()) { std::puts("FAIL deferred menu input overflow reset"); return 1; }
    input.Push(0x202,0,0);
    input.Drain([&](const auto& message) { seen.push_back(message.message); });
    if (seen.size()!=5 || seen.back()!=0x202) { std::puts("FAIL deferred input recovery"); return 1; }
    std::puts("PASS deferred menu input ordering/exactly-once/bounded overflow recovery"); return 0;
}
