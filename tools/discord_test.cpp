// SPDX-License-Identifier: GPL-3.0-only
// Dynamically loads the actual consumer. Only synthetic public settings are supplied.
#include "workflow_module.h"
#include "sco_settings.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); std::exit(1); } } while (0)
sco_command command{};
sco_plugin* const owner = reinterpret_cast<sco_plugin*>(&command);
const sco_settings_v1* settings = nullptr;
sco_result lookup = SCO_OK, getter = SCO_OK, registration = SCO_OK;
int queries = 0, reads = 0, registrations = 0, logs = 0;
int32_t values[3]{};
bool write_value = true;
std::string logged;
sco_result GetBool(sco_plugin* self, const char* name, int32_t* out) {
    CHECK(self == owner && out);
    ++reads;
    const char* names[] = { "show_location", "show_ship", "show_buttons" };
    for (unsigned i = 0; i < 3; ++i) {
        if (std::strcmp(name, names[i]) == 0) {
            if (write_value) *out = values[i];
            return getter;
        }
    }
    CHECK(false); return SCO_BAD_ARG;
}
sco_result Query(const char* name, uint32_t version, const void** out) {
    // Any IPC/game/network request is an immediate failure, not a permissive fake.
    CHECK(std::strcmp(name, SCO_SETTINGS_NAME) == 0 && version == SCO_SETTINGS_VERSION_1_0);
    ++queries;
    *out = settings; // A failed reopen with a non-null out must not use the table.
    return lookup;
}
sco_result Register(sco_plugin* self, const sco_command* cmd) {
    CHECK(self == owner && cmd && cmd->fn);
    ++registrations; command = *cmd; return registration;
}
void Log(sco_plugin* self, sco_log_level level, const char* text) {
    CHECK(self == owner && level == SCO_LOG_INFO);
    ++logs; logged = text;
}
int Has(const char*) { CHECK(false); return 0; }
const char* HostVersion() { CHECK(false); return "synthetic"; }
sco_result Subscribe(sco_plugin*, const char*, sco_event_fn, void*) { CHECK(false); return SCO_FAILED; }
sco_api Api() {
    sco_api api{};
    api.size = sizeof(api); api.major = SCO_API_MAJOR; api.minor = SCO_API_MINOR;
    api.register_command = Register; api.log = Log; api.query_service = Query;
    api.has = Has; api.host_version = HostVersion; api.subscribe = Subscribe;
    return api;
}
std::string Status() {
    char reply[512]{};
    CHECK(command.fn(nullptr, 0, command.ctx, reply, sizeof(reply)) == SCO_UNAVAILABLE);
    CHECK(std::strstr(reply, "nothing transmitted"));
    return reply;
}
void Inactive(const char* preferences) { CHECK(Status().find(preferences) != std::string::npos); }
void NoLateCalls() {
    const int q = queries, r = reads, l = logs;
    test_module::Unload(); test_module::Unload();
    CHECK(Status().find("privacy/security approval") != std::string::npos);
    CHECK(queries == q && reads == r && logs == l);
}
}

int main(int argc, char** argv) {
    test_module::Open(argc, argv);
    const auto* info = test_module::Query();
    CHECK(info && info->size == sizeof(*info));
    CHECK(std::strcmp(info->name, "discord") == 0 && std::strcmp(info->version, "1.1.0") == 0);
    auto api = Api();
    CHECK(test_module::Load(nullptr, owner) == SCO_BAD_ARG);
    CHECK(test_module::Load(&api, nullptr) == SCO_BAD_ARG);
    // Complete host member extents, incompatible versions and full-sized null callbacks.
    const int before = registrations;
    for (uint32_t size = 0; size < offsetof(sco_api, register_command) + sizeof(api.register_command); ++size) {
        api = Api(); api.size = size;
        CHECK(test_module::Load(&api, owner) == SCO_UNAVAILABLE);
    }
    for (int fault = 0; fault < 4; ++fault) {
        api = Api();
        if (fault == 0) api.log = nullptr;
        if (fault == 1) api.register_command = nullptr;
        if (fault == 2) api.major = 2;
        if (fault == 3) api.minor = 0;
        CHECK(test_module::Load(&api, owner) == SCO_UNAVAILABLE);
    }
    CHECK(registrations == before);
    api = Api();
    registration = SCO_FAILED;
    CHECK(test_module::Load(&api, owner) == SCO_FAILED);
    CHECK(logs == 0);
    NoLateCalls(); registration = SCO_OK;
    CHECK(test_module::Load(&api, owner) == SCO_OK);
    CHECK(test_module::Load(&api, owner) == SCO_BAD_ARG);
    CHECK(std::strcmp(command.name, "discord.status") == 0 && command.nargs == 0 && !command.capability);
    CHECK(logged.find("nothing transmitted") != std::string::npos);
    // Missing settings, all legacy true values and subsequent changes never enable presence.
    Inactive("location=off ship=off buttons=off");
    sco_settings_v1 st{}; st.size = sizeof(st); st.get_bool = GetBool; settings = &st;
    values[0] = values[1] = values[2] = 1;
    Inactive("location=on ship=on buttons=on");
    values[0] = 0; values[1] = 1; values[2] = 0;
    Inactive("location=off ship=on buttons=off");
    for (uint32_t size = 0; size < offsetof(sco_settings_v1, get_bool) + sizeof(st.get_bool); ++size) {
        st.size = size; const int r = reads;
        Inactive("location=off ship=off buttons=off"); CHECK(reads == r);
    }
    st.size = sizeof(st); st.get_bool = nullptr;
    Inactive("location=off ship=off buttons=off"); st.get_bool = GetBool;
    getter = SCO_FAILED;
    Inactive("location=off ship=off buttons=off"); getter = SCO_OK;
    write_value = false;
    Inactive("location=off ship=off buttons=off"); write_value = true;
    values[0] = values[1] = values[2] = 2; // Reject a malformed provider bool.
    Inactive("location=off ship=off buttons=off");
    values[0] = values[1] = values[2] = 1;
    lookup = SCO_UNAVAILABLE; const int r = reads;
    Inactive("location=off ship=off buttons=off"); CHECK(reads == r);
    lookup = SCO_OK; Inactive("location=on ship=on buttons=on");
    CHECK(command.fn(nullptr, 1, nullptr, nullptr, 0) == SCO_BAD_ARG);
    CHECK(command.fn(nullptr, 0, nullptr, nullptr, 1) == SCO_BAD_ARG);
    CHECK(command.fn(nullptr, 0, nullptr, nullptr, 0) == SCO_UNAVAILABLE);
    char tiny[2] = { 'x', 'x' };
    CHECK(command.fn(nullptr, 0, nullptr, tiny, sizeof(tiny)) == SCO_UNAVAILABLE && tiny[1] == '\0');
    NoLateCalls();
    // Physically truncated borrowed settings getter; never read a partial function pointer.
    constexpr size_t short_size = offsetof(sco_settings_v1, get_bool) + sizeof(st.get_bool) - 1;
    void* short_table = std::malloc(short_size); CHECK(short_table);
    st.size = static_cast<uint32_t>(short_size); std::memcpy(short_table, &st, short_size);
    settings = static_cast<const sco_settings_v1*>(short_table);
    CHECK(test_module::Load(&api, owner) == SCO_OK);
    Inactive("location=off ship=off buttons=off"); NoLateCalls();
    std::free(short_table); settings = nullptr;
    // Partial optional host tail and absent callbacks work with no settings lookup.
    api = Api(); api.size = offsetof(sco_api, query_service) + sizeof(api.query_service) - 1;
    CHECK(test_module::Load(&api, owner) == SCO_OK);
    const int q = queries;
    Inactive("location=off ship=off buttons=off"); CHECK(queries == q); NoLateCalls();
    api = Api(); api.query_service = nullptr;
    CHECK(test_module::Load(&api, owner) == SCO_OK);
    Inactive("location=off ship=off buttons=off"); NoLateCalls();
    // Re-open the binary, not merely call forwarding exports in the same executable.
    test_module::Close(); test_module::Open(argc, argv);
    api = Api(); CHECK(test_module::Load(&api, owner) == SCO_OK);
    Inactive("location=off ship=off buttons=off"); NoLateCalls(); test_module::Close();
    std::puts("discord module: settings/refusal/lifecycle PASS (synthetic; no transport or game run)");
}
