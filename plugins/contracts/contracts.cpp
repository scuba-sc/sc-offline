// SPDX-License-Identifier: GPL-3.0-only
#include "contracts.h"
#include <sc_missions.h>
#include <cstddef>
#include <cstdio>
#define MEMBER(t, type, m) ((t) && (t)->size >= offsetof(type, m) + sizeof((t)->m) && (t)->m)
namespace sco::features::contracts {
namespace {
const sco_api* api = nullptr;
sco_plugin* self = nullptr;
const sco_plugin_info info{sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "contracts", "1.0.0", "sc-offline"};
enum Action { Summary, Offer, Mission };
struct Spec { const char* name; Action action; uint32_t nargs; };
const Spec specs[] = {{"contracts.status", Summary, 0}, {"contracts.offer", Offer, 1}, {"contracts.mission", Mission, 1}};
const sco_arg_def defs[] = {{"id_or_index", SCO_ARG_INT, 0, "Nonnegative offer index or positive mission ID"}};
template<class T> const T* Service(const char* name, uint32_t version) {
    const void* table = nullptr;
    if (!MEMBER(api, sco_api, query_service) || !self || api->query_service(name, version, &table) != SCO_OK) return nullptr;
    return static_cast<const T*>(table);
}
sco_result Execute(const sco_arg* args, uint32_t count, void* context, char* reply, uint32_t capacity) {
    if (!reply || !capacity || !context) return SCO_BAD_ARG;
    std::snprintf(reply, capacity, "Contracts provider unavailable; no private fallback");
    if (!api || !self) return SCO_UNAVAILABLE;
    const auto& spec = *static_cast<const Spec*>(context);
    if (count != spec.nargs || (count && !args)) return SCO_BAD_ARG;
    for (uint32_t i = 0; i < count; ++i) if (args[i].type != SCO_ARG_INT || args[i].v.i < 0) return SCO_BAD_ARG;
    const auto* m = Service<sc_missions_v1>(SC_MISSIONS_SERVICE_NAME, SC_MISSIONS_SERVICE_VERSION);
    sco_result result = SCO_UNAVAILABLE;
    if (spec.action == Summary || spec.action == Offer) {
        uint64_t index = count ? static_cast<uint64_t>(args[0].v.i) : 0;
        if (index >= SC_MISSIONS_MAX_OFFERS) return SCO_BAD_ARG;
        if (!MEMBER(m, sc_missions_v1, offer)) return SCO_UNAVAILABLE;
        sc_mission_offer out{}; out.size = sizeof(out);
        result = m->offer(self, static_cast<uint32_t>(index), &out);
        if (result == SCO_OK) std::snprintf(reply, capacity, "Offer %llu broker %llu generation %llu; wallet persistence pending", static_cast<unsigned long long>(out.offer_id), static_cast<unsigned long long>(out.broker_id), static_cast<unsigned long long>(out.generation));
        if (spec.action == Summary && result == SCO_NOT_FOUND) { std::snprintf(reply, capacity, "Contracts provider ready; no offers; wallet persistence pending"); return SCO_OK; }
    } else if (spec.action == Mission) {
        if (!args[0].v.i) return SCO_BAD_ARG;
        if (!MEMBER(m, sc_missions_v1, status)) return SCO_UNAVAILABLE;
        sc_mission_status out{}; out.size = sizeof(out);
        result = m->status(self, static_cast<uint64_t>(args[0].v.i), &out);
        if (result == SCO_OK) std::snprintf(reply, capacity, "Mission %llu state %u phase %u objective %u session %llu", static_cast<unsigned long long>(out.mission_id), out.state, out.phase, out.objective, static_cast<unsigned long long>(out.session));
    }
    if (result != SCO_OK && result != SCO_UNAVAILABLE) std::snprintf(reply, capacity, "Contracts operation refused (%u); effects not retried", static_cast<unsigned>(result));
    return result;
}
}
const sco_plugin_info* Query() { return &info; }
sco_result Load(const sco_api* host, sco_plugin* owner) {
    if (api) return SCO_BAD_ARG;
    if (!owner || !host || host->major != SCO_API_MAJOR || host->minor < SCO_API_MINOR || !MEMBER(host, sco_api, register_command) || !MEMBER(host, sco_api, query_service)) return SCO_UNAVAILABLE;
    api = host; self = owner;
    for (const auto& spec : specs) {
        const sco_command command{sizeof(sco_command), 0, spec.name, spec.name, "Guarded public offline contracts boundary", nullptr, spec.nargs ? defs : nullptr, spec.nargs, sizeof(sco_arg_def), Execute, const_cast<Spec*>(&spec)};
        sco_result r = api->register_command(self, &command);
        if (r != SCO_OK) { api = nullptr; self = nullptr; return r; }
    }
    return SCO_OK;
}
void Unload() { api = nullptr; self = nullptr; } // no late provider mutation; host releases commands
}
