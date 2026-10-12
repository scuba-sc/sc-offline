// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco_api.h"
// Product UI observation only. Game-thread, borrowed for plugin lifetime; reopen each use.
// Count excludes removal initiations, which never certify disappearance.
#define SC_OFFLINE_BUILD_STATE_NAME "build.state"
#define SC_OFFLINE_BUILD_STATE_VERSION 0x00010000u
struct sc_offline_build_state_v1 {
    uint32_t size;
    uint32_t (*placed_count)();
    int (*active)();
};
