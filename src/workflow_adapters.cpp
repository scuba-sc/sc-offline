// SPDX-License-Identifier: GPL-3.0-only
// Host-only registration and optional product menu coupling. Never part of a plugin module.
#include "sco/plugins.h"
#include "../plugins/npc/npc.h"
#include "../plugins/inspector/inspector.h"

void RegisterNpcsTab(const sco_api* api, sco_plugin* self);
void ResetNpcsTab();

namespace {
sco_result LoadNpc(const sco_api* api, sco_plugin* self) {
    const sco_result r = sco::features::npc::Load(api, self);
    if (r == SCO_OK) RegisterNpcsTab(api, self);
    return r;
}
void UnloadNpc() {
    ResetNpcsTab();
    sco::features::npc::Unload();
}
}

extern const sco::plugins::Builtin kNpcBuiltin = {"npc", sco::features::npc::Query, LoadNpc, UnloadNpc};
extern const sco::plugins::Builtin kInspectorBuiltin = {
    "inspector", sco::features::inspector::Query, sco::features::inspector::Load, sco::features::inspector::Unload,
};
