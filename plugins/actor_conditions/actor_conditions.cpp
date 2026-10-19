// SPDX-License-Identifier: GPL-3.0-only
#include <sc_actor_stats.h>
#include <cstddef>
#include <cstdio>
namespace {
const sco_api* api = nullptr;
sco_plugin* owner = nullptr;
sco_result Read(const sco_arg* args, uint32_t count, void*, char* out, uint32_t capacity) noexcept {
    if (!out && capacity) return SCO_BAD_ARG;
    if (out && capacity) out[0] = 0;
    if (!args || count != 2 || args[0].type != SCO_ARG_INT || args[1].type != SCO_ARG_INT ||
        args[0].v.i <= 0 || args[1].v.i <= 0 || args[1].v.i > SC_ACTOR_STAT_LAST_1_1) return SCO_BAD_ARG;
    const void* raw = nullptr;
    const auto query = api->query_service(SC_ACTOR_STATS_NAME, SC_ACTOR_STATS_VERSION_1_1, &raw);
    if (query != SCO_OK) return query;
    float value = 0;
    const auto result = sc_actor_stats_read(static_cast<const sc_actor_stats_v1*>(raw),
        static_cast<uint64_t>(args[0].v.i), static_cast<uint32_t>(args[1].v.i), &value);
    if (out && capacity) {
        if (result == SCO_OK) std::snprintf(out, capacity, "stat=%u raw=%.9g",
            static_cast<unsigned>(args[1].v.i), static_cast<double>(value));
        else std::snprintf(out, capacity, "Actor condition unavailable (result=%u); not a zero value",
            static_cast<unsigned>(result));
    }
    return result;
}
}
extern "C" SCO_EXPORT const sco_plugin_info* sco_plugin_query() {
    static const sco_plugin_info info{sizeof(info), SCO_API_MAJOR, SCO_API_MINOR,
        "actor_conditions", "1.0.0", "scubamount"};
    return &info;
}
extern "C" SCO_EXPORT sco_result sco_plugin_load(const sco_api* host, sco_plugin* self) {
    if (!host || !self || host->size < offsetof(sco_api, query_service) + sizeof(host->query_service)
        || !host->query_service || !host->register_command) return SCO_UNAVAILABLE;
    api = host; owner = self;
    const sco_arg_def args[] = {{"actor", SCO_ARG_INT, 0, "Session entity id"},
        {"stat", SCO_ARG_INT, 0, "Named public sc_actor_stat id 1..29"}};
    const sco_command command{sizeof(command), 0, "actor_conditions.read", "Read actor condition",
        "Raw named stat through SDK 1.1; no tier inference or writes", nullptr,
        args, 2, sizeof(sco_arg_def), Read, nullptr};
    const auto result = api->register_command(owner, &command);
    if (result != SCO_OK) { api = nullptr; owner = nullptr; }
    return result;
}
extern "C" SCO_EXPORT void sco_plugin_unload() { api = nullptr; owner = nullptr; }
