// SPDX-License-Identifier: GPL-3.0-only
#include "npc.h"
#include "sc_actors.h"
#include "sc_entities.h"
#include "sc_spatial.h"
#include <cstddef>
#include <cmath>
#include <limits>
#include <cstdio>

namespace sco::features::npc {
namespace {
// Short-circuit before dereferencing a partial function-pointer field.
#define HAS_FN(table, type, member) \
    ((table) && (table)->size >= offsetof(type, member) + sizeof((table)->member) && (table)->member)
struct Entry { uint64_t id = 0; bool owned = true; };
struct State {
    const sco_api* api = nullptr;
    sco_plugin* self = nullptr;
    Entry entries[SC_ACTORS_MAX_NPCS]{};
    uint32_t count = 0;
} g;
const sco_plugin_info kInfo = {sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR, "npc", "1.1.0", "sc-offline"};

template<typename T> const T* Open(const char* name) {
    const void* table = nullptr;
    if (!HAS_FN(g.api, sco_api, query_service) || g.api->query_service(name, 0x00010000u, &table) != SCO_OK)
        return nullptr;
    return static_cast<const T*>(table);
}

sco_result Answer(sco_result result, char* reply, uint32_t size, const char* text) {
    if (size && reply) std::snprintf(reply, size, "%s", text);
    if (HAS_FN(g.api, sco_api, log)) g.api->log(g.self, result == SCO_OK ? SCO_LOG_INFO : SCO_LOG_WARN, text);
    return result;
}

sco_result Spawn(const sco_arg* args, uint32_t nargs, void*, char* reply, uint32_t size) {
    if (!g.api || !g.self) return SCO_UNAVAILABLE;
    if (!args || nargs != 2 || args[0].type != SCO_ARG_STRING || args[1].type != SCO_ARG_INT ||
        !args[0].v.s || !*args[0].v.s || args[1].v.i < 1 || args[1].v.i > 10 || (!reply && size))
        return Answer(SCO_BAD_ARG, reply, size, "Supply an NPC archetype and count (1 to 10)");
    const auto* actors = Open<sc_actors_v1>(SC_ACTORS_NAME);
    const auto* spatial = Open<sc_spatial_v1>(SC_SPATIAL_SERVICE_NAME);
    if (!HAS_FN(actors, sc_actors_v1, spawn_npc) || !HAS_FN(actors, sc_actors_v1, despawn) ||
        !HAS_FN(spatial, sc_spatial_v1, player_pose) || !g.api->has("game.actors.spawn_npc") ||
        !g.api->has("game.actors.despawn"))
        return Answer(SCO_UNAVAILABLE, reply, size, "Owned NPC spawn/removal or player pose is unavailable");
    const double unknown = std::numeric_limits<double>::quiet_NaN();
    double pos[3] = {unknown, unknown, unknown}, rot[4] = {unknown, unknown, unknown, unknown};
    uint64_t zone = 0;
    if (!spatial->player_pose(pos, rot, &zone) || !zone)
        return Answer(SCO_FAILED, reply, size, "No fresh player pose in a streamed zone");
    for (const auto value : pos) if (!std::isfinite(value))
        return Answer(SCO_BAD_ARG, reply, size, "Player location is not finite");
    double norm = 0;
    for (const auto value : rot) {
        if (!std::isfinite(value)) return Answer(SCO_BAD_ARG, reply, size, "Player rotation is not finite");
        norm += value * value;
    }
    if (!std::isfinite(norm) || norm <= 0)
        return Answer(SCO_BAD_ARG, reply, size, "Player rotation is invalid");
    for (auto& value : rot) value /= std::sqrt(norm);
    const double right[3] = {1-2*(rot[1]*rot[1]+rot[2]*rot[2]), 2*(rot[0]*rot[1]+rot[3]*rot[2]), 2*(rot[0]*rot[2]-rot[3]*rot[1])};
    const double forward[3] = {2*(rot[0]*rot[1]-rot[3]*rot[2]), 1-2*(rot[0]*rot[0]+rot[2]*rot[2]), 2*(rot[1]*rot[2]+rot[3]*rot[0])};
    const double up[3] = {2*(rot[0]*rot[2]+rot[3]*rot[1]), 2*(rot[1]*rot[2]-rot[3]*rot[0]), 1-2*(rot[0]*rot[0]+rot[1]*rot[1])};
    uint32_t spawned = 0;
    sco_result result = SCO_OK;
    for (int64_t i = 0; i < args[1].v.i; ++i) {
        if (g.count == SC_ACTORS_MAX_NPCS) { result = SCO_TOO_MANY; break; }
        const double side = (static_cast<double>(i) - (static_cast<double>(args[1].v.i)-1)*.5)*1.2;
        double near[3]{};
        for (int axis = 0; axis < 3; ++axis) near[axis] = pos[axis] + 3*forward[axis] + side*right[axis] + .2*up[axis];
        if (!std::isfinite(near[0]) || !std::isfinite(near[1]) || !std::isfinite(near[2])) { result = SCO_BAD_ARG; break; }
        uint64_t id = 0;
        result = actors->spawn_npc(g.self, args[0].v.s, zone, near, &id);
        if (result != SCO_OK) break;
        bool duplicate = false;
        for (uint32_t n = 0; n < g.count; ++n) if (g.entries[n].id == id) duplicate = true;
        if (!id || duplicate) { result = SCO_FAILED; break; }
        g.entries[g.count++] = {id, true};
        ++spawned;
        char text[96];
        std::snprintf(text, sizeof(text), "NPC id=%llu pending streaming", static_cast<unsigned long long>(id));
        g.api->log(g.self, SCO_LOG_INFO, text);
    }
    char text[128];
    std::snprintf(text, sizeof(text), "Spawn requested for %u of %lld owned NPCs (result=%u)",
                  spawned, static_cast<long long>(args[1].v.i), static_cast<unsigned>(result));
    // Partial success is still a failed batch: the reply states how many are already owned.
    return Answer(result, reply, size, text);
}

sco_result RemoveEntry(Entry& entry, const sc_actors_v1* actors) {
    if (!entry.owned) return SCO_NOT_FOUND;
    if (!HAS_FN(actors, sc_actors_v1, despawn) || !g.api->has("game.actors.despawn")) return SCO_UNAVAILABLE;
    const sco_result r = actors->despawn(g.self, entry.id);
    if (r == SCO_OK || r == SCO_NOT_FOUND) entry.owned = false;
    return r;
}

sco_result Remove(const sco_arg* args, uint32_t nargs, void*, char* reply, uint32_t size) {
    if (!g.api || !g.self) return SCO_UNAVAILABLE;
    if (!args || nargs != 1 || args[0].type != SCO_ARG_INT || !args[0].v.i || (!reply && size))
        return Answer(SCO_BAD_ARG, reply, size, "Supply a nonzero NPC session id");
    // Preserve all 64 bits, including IDs whose top bit is set.
    const uint64_t id = static_cast<uint64_t>(args[0].v.i);
    for (uint32_t i = 0; i < g.count; ++i) {
        auto& entry = g.entries[i];
        if (entry.id != id || !entry.owned) continue;
        const sco_result r = RemoveEntry(entry, Open<sc_actors_v1>(SC_ACTORS_NAME));
        return Answer(r, reply, size, r == SCO_OK ? "Removal started; world disappearance is not confirmed" :
                      r == SCO_NOT_FOUND ? "Provider no longer owns this NPC" : "Removal refused; still tracked for retry");
    }
    return Answer(SCO_NOT_FOUND, reply, size, "Not an NPC owned by this plugin");
}

sco_result Clear(const sco_arg*, uint32_t nargs, void*, char* reply, uint32_t size) {
    if (!g.api || !g.self) return SCO_UNAVAILABLE;
    if (nargs || (!reply && size)) return SCO_BAD_ARG;
    const auto* actors = Open<sc_actors_v1>(SC_ACTORS_NAME);
    uint32_t requested = 0, refused = 0;
    sco_result result = SCO_OK;
    for (uint32_t i = 0; i < g.count; ++i) {
        auto& entry = g.entries[i];
        if (!entry.owned) continue;
        const sco_result r = RemoveEntry(entry, actors);
        if (r == SCO_OK) ++requested;
        else if (r != SCO_NOT_FOUND) { ++refused; result = r; }
    }
    char text[128];
    std::snprintf(text, sizeof(text), "Removal started for %u owned NPCs; refused=%u (no crew or foreign NPCs)", requested, refused);
    return Answer(result, reply, size, text);
}

sco_result Observe(const sco_arg*, uint32_t nargs, void*, char* reply, uint32_t size) {
    if (!g.api || !g.self) return SCO_UNAVAILABLE;
    if (nargs || (!reply && size)) return SCO_BAD_ARG;
    const auto* entities = Open<sc_entities_v1>(SC_ENTITIES_NAME);
    if (!HAS_FN(entities, sc_entities_v1, alive) || !HAS_FN(entities, sc_entities_v1, get_transform) ||
        !g.api->has("game.entities.transform"))
        return Answer(SCO_UNAVAILABLE, reply, size, "NPC visibility/transform observation unavailable; ownership unchanged");
    uint32_t pending = 0, visible = 0, removing = 0, unknown = 0;
    for (uint32_t i = 0; i < g.count; ++i) {
        const auto& entry = g.entries[i];
        if (!entry.owned) { ++removing; continue; }
        // alive=0 cannot prove disappearance, absence of capability or correct threading.
        if (!entities->alive(entry.id)) { ++pending; continue; }
        const double unknown_value = std::numeric_limits<double>::quiet_NaN();
        double pos[3] = {unknown_value, unknown_value, unknown_value};
        double rot[4] = {unknown_value, unknown_value, unknown_value, unknown_value};
        uint64_t zone = 0;
        if (entities->get_transform(entry.id, pos, rot, &zone) != SCO_OK || !zone ||
            !std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2]) ||
            !std::isfinite(rot[0]) || !std::isfinite(rot[1]) || !std::isfinite(rot[2]) || !std::isfinite(rot[3]) ||
            (rot[0] == 0 && rot[1] == 0 && rot[2] == 0 && rot[3] == 0)) { ++unknown; continue; }
        ++visible;
        char text[192];
        std::snprintf(text, sizeof(text), "NPC id=%llu visible zone=%llu position=(%.17g,%.17g,%.17g)",
                      static_cast<unsigned long long>(entry.id), static_cast<unsigned long long>(zone), pos[0], pos[1], pos[2]);
        g.api->log(g.self, SCO_LOG_INFO, text);
    }
    char text[160];
    std::snprintf(text, sizeof(text), "NPC observation: pending=%u visible=%u removing=%u unknown=%u; removal is not disappearance",
                  pending, visible, removing, unknown);
    return Answer(SCO_OK, reply, size, text);
}
}  // namespace

const sco_plugin_info* Query() { return &kInfo; }

sco_result Load(const sco_api* api, sco_plugin* self) {
    if (!api || !self || g.api) return SCO_BAD_ARG;
    if (!HAS_FN(api, sco_api, has) || !HAS_FN(api, sco_api, log) || !HAS_FN(api, sco_api, register_command) ||
        !HAS_FN(api, sco_api, query_service) || api->major != SCO_API_MAJOR || api->minor < SCO_API_MINOR) return SCO_UNAVAILABLE;
    g.api = api; g.self = self;
    const sco_arg_def spawn[2] = {{"archetype", SCO_ARG_STRING, 0, "Caller-supplied NPC class"}, {"count", SCO_ARG_INT, 0, "1 to 10"}};
    const sco_arg_def remove = {"id", SCO_ARG_INT, 0, "Owned NPC session id (never persist it)"};
    const sco_command commands[] = {
        {sizeof(sco_command), 0, "npc.spawn", "Spawn owned NPCs", "Spawn near a fresh player pose", "game.actors.spawn_npc", spawn, 2, sizeof(sco_arg_def), Spawn, &g},
        {sizeof(sco_command), 0, "npc.observe", "Observe owned NPCs", "Read streamed transforms; no mutation", nullptr, nullptr, 0, sizeof(sco_arg_def), Observe, &g},
        {sizeof(sco_command), 0, "npc.remove", "Remove owned NPC", "Request removal of one owned session id", "game.actors.despawn", &remove, 1, sizeof(sco_arg_def), Remove, &g},
        {sizeof(sco_command), 0, "npc.clear", "Remove owned NPCs", "Only this plugin's NPCs, never crew-wide clear", "game.actors.despawn", nullptr, 0, sizeof(sco_arg_def), Clear, &g},
    };
    for (const auto& command : commands) {
        const sco_result r = api->register_command(self, &command);
        if (r != SCO_OK) { Unload(); return r; }
    }
    return SCO_OK;
}

void Unload() {
    if (!g.api) return;
    char reply[160]{};
    const sco_result r = Clear(nullptr, 0, nullptr, reply, sizeof(reply));
    if (r != SCO_OK) g.api->log(g.self, SCO_LOG_WARN, "NPC cleanup refused; host owner-release cleanup is still required");
    // The host releases commands and any remaining actor ownership after unload returns.
    // No provider table, position or volatile zone is retained between SDK calls.
    g = {};
}
#undef HAS_FN
}  // namespace sco::features::npc
