// SPDX-License-Identifier: GPL-3.0-only
// Public SDK consumer only. The game pack owns admission, cells and retained hooks.
#include "sco_api.h"
#include "sco_settings.h"
#include "sc_mining.h"
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {
#define HAS(t, type, member) ((t) && (t)->size >= offsetof(type, member) + sizeof((t)->member) && (t)->member)
const sco_api* api = nullptr;
sco_plugin* self = nullptr;
bool requested = false, debug = false, settingsReady = false;
uint32_t traceAt = 0;
uint64_t lastCells = 0;
bool traceStarted = false;
const sco_plugin_info info{sizeof(info), SCO_API_MAJOR, SCO_API_MINOR, "mining", "1.0.0", "sc-offline"};

template<class T> const T* Open(const char* name, uint32_t version) {
    const void* table = nullptr;
    return HAS(api, sco_api, query_service) && api->query_service(name, version, &table) == SCO_OK
        ? static_cast<const T*>(table) : nullptr;
}
bool ReadBool(const sco_settings_v1* st, const char* name, bool& value) {
    int32_t v = 0;
    value = false;
    if (!HAS(st, sco_settings_v1, get_bool) || st->get_bool(self, name, &v) != SCO_OK || (v != 0 && v != 1)) return false;
    value = v == 1;
    return true;
}
// Never read environment/ini or write host-owned settings keys from plugin code.
// Failed settings reads actively withdraw the request whenever the service is reachable.
sco_result Refresh() {
    const auto* st = Open<sco_settings_v1>(SCO_SETTINGS_NAME, SCO_SETTINGS_VERSION_1_0);
    bool enabled = false, trace = false;
    const bool e = ReadBool(st, "enabled", enabled), d = ReadBool(st, "debug", trace);
    settingsReady = e && d;
    requested = settingsReady && enabled;
    debug = settingsReady && trace;
    return sc_mining_request(Open<sc_mining_v1>(SC_MINING_SERVICE_NAME, SC_MINING_SERVICE_VERSION), self, requested ? 1u : 0u);
}
const char* Text(const sc_mining_state& s) {
    if (s.active) return "active";
    switch (s.block) {
        case SC_MINING_BLOCK_OFF: return "off";
        case SC_MINING_BLOCK_ADMISSION: return "waiting for verified offline admission";
        case SC_MINING_BLOCK_RESTART: return "disabled after a fault; restart required";
        default: return "unavailable: native engine bridge not verified";
    }
}
bool Valid(const sc_mining_state& s) {
    return s.size >= sizeof(s) && s.requested <= 1 && s.active <= 1 && s.block <= SC_MINING_BLOCK_RESTART &&
           (!s.active || (s.requested == 1 && s.block == SC_MINING_BLOCK_OFF));
}
sco_result Status(const sco_arg*, uint32_t nargs, void*, char* reply, uint32_t capacity) {
    if (nargs || (!reply && capacity)) return SCO_BAD_ARG;
    if (!api || !self) return SCO_UNAVAILABLE;
    const auto applied = Refresh();
    sc_mining_state state{}; state.size = sizeof(state);
    const auto read = sc_mining_read_state(Open<sc_mining_v1>(SC_MINING_SERVICE_NAME, SC_MINING_SERVICE_VERSION), self, &state);
    if (applied != SCO_OK || read != SCO_OK || !Valid(state)) {
        if (capacity) std::snprintf(reply, capacity, "Natural mining unavailable: public service or requested configuration refused; requested=%s settings=%s",
                                  requested ? "on" : "off", settingsReady ? "ready" : "unavailable");
        return SCO_UNAVAILABLE;
    }
    if (capacity) std::snprintf(reply, capacity, "Natural mining %s; requested=%s debug=%s settings=%s; %llu cells seen, %llu promoted, %llu read faults",
        Text(state), requested ? "on" : "off", debug ? "on" : "off", settingsReady ? "ready" : "unavailable",
        static_cast<unsigned long long>(state.cells), static_cast<unsigned long long>(state.promoted), static_cast<unsigned long long>(state.faults));
    // Accepted owner intent is not engine acceptance. Diagnostics work while off.
    return !settingsReady || (requested && !state.active) ? SCO_UNAVAILABLE : SCO_OK;
}
void Tick(const char*, const void* data, void*) {
    Refresh();
    if (!debug || !data) return;
    uint32_t now = 0; std::memcpy(&now, data, sizeof(now));
    if (!traceStarted) { traceAt = now; traceStarted = true; return; }
    if (now - traceAt < 1000u) return;
    sc_mining_state state{}; state.size = sizeof(state);
    if (sc_mining_read_state(Open<sc_mining_v1>(SC_MINING_SERVICE_NAME, SC_MINING_SERVICE_VERSION), self, &state) != SCO_OK ||
        !Valid(state) || state.cells == lastCells) return;
    traceAt = now; lastCells = state.cells;
    char msg[256];
    std::snprintf(msg, sizeof(msg), "[mining/trace] active=%u cells=%llu promoted=%llu readFaults=%llu",
        state.active, static_cast<unsigned long long>(state.cells), static_cast<unsigned long long>(state.promoted),
        static_cast<unsigned long long>(state.faults));
    api->log(self, SCO_LOG_INFO, msg);
}
void Unload() {
    if (api && self) sc_mining_request(Open<sc_mining_v1>(SC_MINING_SERVICE_NAME, SC_MINING_SERVICE_VERSION), self, 0);
    api = nullptr; self = nullptr; requested = debug = settingsReady = traceStarted = false; traceAt = 0; lastCells = 0;
    // Host release dispatcher additionally clears this owner's request on faulted unload.
}
sco_result Load(const sco_api* host, sco_plugin* owner) {
    if (!host || !owner || api) return SCO_BAD_ARG;
    if (!HAS(host, sco_api, register_command) || !HAS(host, sco_api, subscribe) || !HAS(host, sco_api, log) ||
        host->major != SCO_API_MAJOR || host->minor < SCO_API_MINOR) return SCO_UNAVAILABLE;
    api = host; self = owner;
    const sco_command command{sizeof(command), 0, "mining.status", "Natural mining status",
        "Requested settings and copied game-pack mining state; unavailable never falls back", nullptr, nullptr, 0,
        sizeof(sco_arg_def), Status, nullptr};
    auto result = api->register_command(self, &command);
    if (result == SCO_OK) result = api->subscribe(self, "tick", Tick, nullptr);
    if (result != SCO_OK) { Unload(); return result; }
    Refresh();
    return SCO_OK; // Status remains reachable with missing game services.
}
#undef HAS
}
extern "C" {
SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) { return &info; }
SCO_EXPORT sco_result sco_plugin_load(const sco_api* host, sco_plugin* owner) { return Load(host, owner); }
SCO_EXPORT void sco_plugin_unload(void) { Unload(); }
}
