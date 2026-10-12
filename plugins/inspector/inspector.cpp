// SPDX-License-Identifier: GPL-3.0-only
#include "inspector.h"
#include "sc_actors.h"
#include "sc_entities.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>

namespace sco::features::inspector {
namespace {

// Test the complete member, not just its first byte; short-circuit before reading it.
#define HAS_FN(table, type, member) \
    ((table) && (table)->size >= offsetof(type, member) + sizeof((table)->member) && (table)->member)

const sco_api* g_api = nullptr;
sco_plugin* g_self = nullptr;
const sco_plugin_info kInfo = { sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
                               "inspector", "1.0.0", "sc-offline" };

const char* Missing(sco_result result) { return result == SCO_UNAVAILABLE ? "unavailable" : "unknown"; }

sco_result Read(const sco_arg*, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    if (nargs || (!reply && reply_size)) return SCO_BAD_ARG;
    if (!g_api || !g_self) return SCO_UNAVAILABLE;

    char actor_text[32] = "unavailable", entity_text[32] = "unavailable";
    char current_text[48] = "unavailable", maximum_text[48] = "unavailable";
    char zone_text[32] = "unavailable", position_text[160] = "unavailable", rotation_text[216] = "unavailable";
    uint64_t actor = 0, entity = 0;
    const void* table = nullptr;
    const sc_actors_v1* actors = nullptr;
    if (HAS_FN(g_api, sco_api, query_service) &&
        g_api->query_service(SC_ACTORS_NAME, SC_ACTORS_VERSION_1_0, &table) == SCO_OK)
        actors = static_cast<const sc_actors_v1*>(table);
    if (HAS_FN(actors, sc_actors_v1, local_player) && HAS_FN(g_api, sco_api, has) &&
        g_api->has("game.actors.local_player")) {
        const sco_result r = actors->local_player(&actor, &entity);
        std::snprintf(actor_text, sizeof(actor_text), "%s", Missing(r));
        std::snprintf(entity_text, sizeof(entity_text), "%s", Missing(r));
        if (r == SCO_OK) {
            if (actor) std::snprintf(actor_text, sizeof(actor_text), "%llu", static_cast<unsigned long long>(actor));
            if (entity) std::snprintf(entity_text, sizeof(entity_text), "%llu", static_cast<unsigned long long>(entity));
        } else {
            actor = 0;
            entity = 0;
        }
    }
    if (HAS_FN(actors, sc_actors_v1, health) && HAS_FN(g_api, sco_api, has) && g_api->has("game.actors.health")) {
        float current = std::numeric_limits<float>::quiet_NaN(), maximum = current;
        const sco_result r = entity ? actors->health(entity, &current, &maximum) : SCO_NOT_FOUND;
        std::snprintf(current_text, sizeof(current_text), "%s", Missing(r));
        std::snprintf(maximum_text, sizeof(maximum_text), "%s", Missing(r));
        if (r == SCO_OK) {
            if (std::isfinite(current)) std::snprintf(current_text, sizeof(current_text), "%.9g", current);
            // The SDK explicitly defines zero maximum as unknown, not a zero-sized pool.
            if (std::isfinite(maximum) && maximum > 0) std::snprintf(maximum_text, sizeof(maximum_text), "%.9g", maximum);
        }
    }
    table = nullptr;
    const sc_entities_v1* entities = nullptr;
    if (HAS_FN(g_api, sco_api, query_service) &&
        g_api->query_service(SC_ENTITIES_NAME, SC_ENTITIES_VERSION_1_0, &table) == SCO_OK)
        entities = static_cast<const sc_entities_v1*>(table);
    if (HAS_FN(entities, sc_entities_v1, get_transform) && HAS_FN(g_api, sco_api, has) &&
        g_api->has("game.entities.transform")) {
        const double unknown = std::numeric_limits<double>::quiet_NaN();
        double pos[3] = { unknown, unknown, unknown }, rot[4] = { unknown, unknown, unknown, unknown };
        uint64_t zone = 0;
        const sco_result r = entity ? entities->get_transform(entity, pos, rot, &zone) : SCO_NOT_FOUND;
        std::snprintf(zone_text, sizeof(zone_text), "%s", Missing(r));
        std::snprintf(position_text, sizeof(position_text), "%s", Missing(r));
        std::snprintf(rotation_text, sizeof(rotation_text), "%s", Missing(r));
        if (r == SCO_OK && zone) {
            std::snprintf(zone_text, sizeof(zone_text), "%llu", static_cast<unsigned long long>(zone));
            if (std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2]))
                std::snprintf(position_text, sizeof(position_text), "(%.17g,%.17g,%.17g)", pos[0], pos[1], pos[2]);
            if (std::isfinite(rot[0]) && std::isfinite(rot[1]) && std::isfinite(rot[2]) && std::isfinite(rot[3]) &&
                (rot[0] != 0 || rot[1] != 0 || rot[2] != 0 || rot[3] != 0))
                std::snprintf(rotation_text, sizeof(rotation_text), "(%.17g,%.17g,%.17g,%.17g)", rot[0], rot[1], rot[2], rot[3]);
        }
    }
    char text[768];
    std::snprintf(text, sizeof(text), "actor=%s entity=%s health.current=%s health.maximum=%s zone=%s position=%s rotation=%s",
                  actor_text, entity_text, current_text, maximum_text, zone_text, position_text, rotation_text);
    if (HAS_FN(g_api, sco_api, log)) g_api->log(g_self, SCO_LOG_INFO, text);
    if (reply_size) std::snprintf(reply, reply_size, "%s", text);
    return SCO_OK;
}

}  // namespace

const sco_plugin_info* Query() { return &kInfo; }

sco_result Load(const sco_api* api, sco_plugin* self) {
    if (!api || !self || g_api) return SCO_BAD_ARG;
    if (!HAS_FN(api, sco_api, has) || !HAS_FN(api, sco_api, log) ||
        !HAS_FN(api, sco_api, register_command) || !HAS_FN(api, sco_api, query_service) ||
        api->major != SCO_API_MAJOR || api->minor < SCO_API_MINOR)
        return SCO_UNAVAILABLE;
    const sco_command cmd = { sizeof(sco_command), 0, "inspector.read", "Read player",
                              "Read the current player without changing the game", nullptr,
                              nullptr, 0, sizeof(sco_arg_def), Read, nullptr };
    g_api = api;
    g_self = self;
    const sco_result r = api->register_command(self, &cmd);
    if (r != SCO_OK) Unload();
    return r;
}

void Unload() {
    // The host removes commands at unload. No borrowed service, id or callback context survives.
    g_api = nullptr;
    g_self = nullptr;
}

#undef HAS_FN
}  // namespace sco::features::inspector
