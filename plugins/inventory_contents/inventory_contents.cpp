// SPDX-License-Identifier: GPL-3.0-only
#include <sc_container_contents.h>
#include <cstddef>
#include <cstdio>
namespace {
const sco_api* api = nullptr;
sco_plugin* owner = nullptr;
sco_result Read(const sco_arg* args, uint32_t count, void*, char* out, uint32_t capacity) noexcept {
    if (!out && capacity) return SCO_BAD_ARG;
    if (out && capacity) out[0] = 0;
    if (!args || count != 1 || args[0].type != SCO_ARG_INT || args[0].v.i <= 0) return SCO_BAD_ARG;
    const void* raw = nullptr;
    const auto queried = api->query_service(SC_CONTAINER_CONTENTS_NAME, SC_CONTAINER_CONTENTS_VERSION_1_0, &raw);
    if (queried != SCO_OK) return queried;
    sc_container_snapshot snapshot{};
    snapshot.size = sizeof(snapshot); snapshot.count = SC_CONTAINER_CONTENTS_MAX_ITEMS;
    const auto result = sc_container_contents_read(static_cast<const sc_container_contents_v1*>(raw),
        owner, static_cast<uint64_t>(args[0].v.i), &snapshot);
    if (out && capacity) {
        if (result == SCO_OK) {
            std::snprintf(out, capacity, "items=%u total=%u generation=%llu volume_known=%u truncated=%u",
                snapshot.count, snapshot.total_seen, static_cast<unsigned long long>(snapshot.generation),
                (snapshot.flags & SC_CONTAINER_CONTENTS_VOLUME_KNOWN) != 0,
                (snapshot.flags & SC_CONTAINER_CONTENTS_TRUNCATED) != 0);
        } else std::snprintf(out, capacity, "Container contents unavailable (result=%u); no private fallback",
            static_cast<unsigned>(result));
    }
    return result;
}
}
extern "C" SCO_EXPORT const sco_plugin_info* sco_plugin_query() {
    static const sco_plugin_info info{sizeof(info), SCO_API_MAJOR, SCO_API_MINOR,
        "inventory_contents", "1.0.0", "scubamount"};
    return &info;
}
extern "C" SCO_EXPORT sco_result sco_plugin_load(const sco_api* host, sco_plugin* self) {
    if (!host || !self || host->size < offsetof(sco_api, query_service) + sizeof(host->query_service)
        || !host->query_service || !host->register_command) return SCO_UNAVAILABLE;
    api = host; owner = self;
    static const sco_arg_def arg{"container", SCO_ARG_INT, 0, "Session-local container id"};
    const sco_command command{sizeof(command), 0, "inventory_contents.read", "Read container contents",
        "Stored-item snapshot through public SDK; unavailable is not empty", nullptr,
        &arg, 1, sizeof(sco_arg_def), Read, nullptr};
    const auto result = api->register_command(self, &command);
    if (result != SCO_OK) { api = nullptr; owner = nullptr; }
    return result;
}
extern "C" SCO_EXPORT void sco_plugin_unload() { api = nullptr; owner = nullptr; }
