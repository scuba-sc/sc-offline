// SPDX-License-Identifier: GPL-3.0-only
// Presence is refused until an approved host transport and reviewed field policy exist.
// No Discord pipe, network, worker, game read, or implicit host fallback lives here.
#include "discord.h"
#include "sco_settings.h"
#include <cstddef>
#include <cstdio>

namespace sco::features::discord {
namespace {
#define HAS_FN(table, type, member) \
    ((table) && (table)->size >= offsetof(type, member) + sizeof((table)->member) && (table)->member)

const sco_api* g_api = nullptr;
sco_plugin* g_self = nullptr;
const sco_plugin_info kInfo = { sizeof(sco_plugin_info), SCO_API_MAJOR, SCO_API_MINOR,
                               "discord", "1.1.0", "sc-offline" };
constexpr const char* kRefusal = "Discord presence unavailable: transport and fields await owner privacy/security approval; nothing transmitted";

// Preferences are local diagnostics only, never consent to transmit. Reopen every time;
// failures leave the safe default false and no borrowed settings table survives a call.
bool Preference(const sco_settings_v1* settings, const char* name) {
    int32_t value = 0;
    return HAS_FN(settings, sco_settings_v1, get_bool) &&
           settings->get_bool(g_self, name, &value) == SCO_OK && value == 1;
}

sco_result Status(const sco_arg*, uint32_t nargs, void*, char* reply, uint32_t reply_size) {
    if (nargs || (!reply && reply_size)) return SCO_BAD_ARG;
    if (!g_api || !g_self) {
        if (reply_size) std::snprintf(reply, reply_size, "%s", kRefusal);
        return SCO_UNAVAILABLE;
    }
    const void* table = nullptr;
    const sco_settings_v1* settings = nullptr;
    if (HAS_FN(g_api, sco_api, query_service) &&
        g_api->query_service(SCO_SETTINGS_NAME, SCO_SETTINGS_VERSION_1_0, &table) == SCO_OK)
        settings = static_cast<const sco_settings_v1*>(table);
    const bool location = Preference(settings, "show_location");
    const bool ship = Preference(settings, "show_ship");
    const bool buttons = Preference(settings, "show_buttons");
    if (reply_size)
        std::snprintf(reply, reply_size, "%s; inactive preferences location=%s ship=%s buttons=%s",
                      kRefusal, location ? "on" : "off", ship ? "on" : "off", buttons ? "on" : "off");
    return SCO_UNAVAILABLE;
}
}  // namespace

const sco_plugin_info* Query() { return &kInfo; }
sco_result Load(const sco_api* api, sco_plugin* self) {
    if (!api || !self || g_api) return SCO_BAD_ARG;
    if (!HAS_FN(api, sco_api, log) || !HAS_FN(api, sco_api, register_command) ||
        api->major != SCO_API_MAJOR || api->minor < SCO_API_MINOR)
        return SCO_UNAVAILABLE;
    const sco_command cmd = { sizeof(sco_command), 0, "discord.status", "Discord presence status",
                              "Explain the privacy/transport refusal; preferences cannot enable transmission",
                              nullptr, nullptr, 0, sizeof(sco_arg_def), Status, nullptr };
    g_api = api;
    g_self = self;
    const sco_result result = api->register_command(self, &cmd);
    if (result != SCO_OK) { Unload(); return result; }
    api->log(self, SCO_LOG_INFO, kRefusal);
    return SCO_OK; // The diagnostic module loads; presence itself remains unavailable.
}
void Unload() {
    // The host withdraws this owner's command; no workers, subscriptions or service calls.
    g_api = nullptr;
    g_self = nullptr;
}
#undef HAS_FN
}  // namespace sco::features::discord

// Preserve the existing one-file host target until it is replaced by the isolated build.
// The standalone static implementation uses external.cpp instead.
#ifndef DISCORD_EXTERNAL_ADAPTER
extern "C" {
SCO_EXPORT const sco_plugin_info* sco_plugin_query(void) { return sco::features::discord::Query(); }
SCO_EXPORT sco_result sco_plugin_load(const sco_api* api, sco_plugin* self) { return sco::features::discord::Load(api, self); }
SCO_EXPORT void sco_plugin_unload(void) { sco::features::discord::Unload(); }
}
#endif
