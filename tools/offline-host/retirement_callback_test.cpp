// SPDX-License-Identifier: GPL-3.0-only
// Product callback behavior, independent of the game or live native detours.
#include "offline_host.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
    const bool begin = argc == 2 && std::strcmp(argv[1], "after-mutation") == 0;
    if (begin && !offline_host::BeginPersistentMutation()) return 1;
    if (offline_host::RetireProductConsumer(nullptr) != sco::game::host::ConsumerRetirement::Retained) return 2;
    if (offline_host::BeginPersistentMutation()) return 3;
    std::puts("product retirement retains backing and closes the mutation gate");
    return 0;
}
