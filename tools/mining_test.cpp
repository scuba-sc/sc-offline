// SPDX-License-Identifier: GPL-3.0-only
// Real module, synthetic public tables only; not a native game/admission test.
#include "workflow_module.h"
#include "sco_settings.h"
#include "sc_mining.h"
#include <cstring>
#include <string>
#define CHECK(e) do { if (!(e)) { std::fprintf(stderr, "FAIL %d: %s\n", __LINE__, #e); std::exit(1); } } while(0)
namespace {
sco_command command{};
sco_event_fn tick = nullptr;
sco_plugin* const owner = reinterpret_cast<sco_plugin*>(&command);
int32_t enabled = 0, debug = 0;
bool available = true, settingsAvailable = true;
sco_result settingsResult = SCO_OK, requestResult = SCO_OK, stateResult = SCO_OK;
int requests = 0, logs = 0;
uint32_t requested = 0, active = 0, block = SC_MINING_BLOCK_OFF;
uint64_t cells = 0;
sco_settings_v1 settings{};
sc_mining_v1 service{};
sco_result Bool(sco_plugin* self, const char* name, int32_t* out) {
    CHECK(self == owner);
    if (!std::strcmp(name, "enabled")) *out = enabled;
    else { CHECK(!std::strcmp(name, "debug")); *out = debug; }
    return settingsResult;
}
sco_result Request(sco_plugin* self, uint32_t on) { CHECK(self == owner && on <= 1); ++requests; requested = on; return requestResult; }
sco_result State(sco_plugin* self, sc_mining_state* out) {
    CHECK(self == owner && out && out->size == sizeof(*out));
    *out = {sizeof(*out), requested, active, requested ? block : SC_MINING_BLOCK_OFF, cells, 2, 0}; return stateResult;
}
sco_result Query(const char* name, uint32_t version, const void** out) {
    if (!std::strcmp(name, SCO_SETTINGS_NAME)) { CHECK(version == SCO_SETTINGS_VERSION_1_0); *out = &settings; return settingsAvailable ? SCO_OK : SCO_UNAVAILABLE; }
    CHECK(!std::strcmp(name, SC_MINING_SERVICE_NAME) && version == SC_MINING_SERVICE_VERSION);
    *out = &service; return available ? SCO_OK : SCO_UNAVAILABLE; // failed lookup deliberately leaves output set
}
sco_result Register(sco_plugin* self, const sco_command* c) { CHECK(self == owner); command = *c; return SCO_OK; }
sco_result Subscribe(sco_plugin* self, const char* name, sco_event_fn fn, void*) { CHECK(self == owner && !std::strcmp(name, "tick")); tick = fn; return SCO_OK; }
void Log(sco_plugin* self, sco_log_level level, const char* text) { CHECK(self == owner && level == SCO_LOG_INFO && std::strstr(text, "[mining/trace]")); ++logs; }
std::string Status(sco_result expected) { char reply[512]{}; CHECK(command.fn(nullptr, 0, nullptr, reply, sizeof(reply)) == expected); return reply; }
}
int main(int argc, char** argv) {
    test_module::Open(argc, argv);
    CHECK(!std::strcmp(test_module::Query()->name, "mining"));
    sco_api api{}; api.size = sizeof(api); api.major = SCO_API_MAJOR; api.minor = SCO_API_MINOR;
    api.register_command = Register; api.subscribe = Subscribe; api.log = Log; api.query_service = Query;
    settings.size = sizeof(settings); settings.get_bool = Bool;
    service = {sizeof(service), 0, Request, State};
    CHECK(test_module::Load(nullptr, owner) == SCO_BAD_ARG);
    CHECK(test_module::Load(&api, owner) == SCO_OK);
    CHECK(!command.capability && !std::strcmp(command.name, "mining.status"));
    CHECK(requested == 0 && Status(SCO_OK).find("off") != std::string::npos);
    enabled = 1; block = SC_MINING_BLOCK_UNAVAILABLE;
    CHECK(Status(SCO_UNAVAILABLE).find("native engine bridge not verified") != std::string::npos);
    block = SC_MINING_BLOCK_ADMISSION;
    CHECK(Status(SCO_UNAVAILABLE).find("waiting for verified offline admission") != std::string::npos);
    active = 1; block = SC_MINING_BLOCK_OFF;
    CHECK(Status(SCO_OK).find("active") != std::string::npos);
    active = 0; block = SC_MINING_BLOCK_RESTART;
    CHECK(Status(SCO_UNAVAILABLE).find("restart required") != std::string::npos);
    settingsAvailable = false; Status(SCO_UNAVAILABLE); CHECK(requested == 0); settingsAvailable = true;
    enabled = 2; Status(SCO_UNAVAILABLE); CHECK(requested == 0); // invalid bool refuses settings, not turns on
    // Invalid settings report unavailable, even though the withdrawal succeeds.
    enabled = 1; settingsResult = SCO_FAILED; Status(SCO_UNAVAILABLE); CHECK(requested == 0); settingsResult = SCO_OK;
    for (uint32_t size = 0; size < offsetof(sc_mining_v1, request) + sizeof(service.request); ++size) {
        service.size = size; const int count = requests; Status(SCO_UNAVAILABLE); CHECK(requests == count);
    }
    service.size = sizeof(service); service.request = nullptr; Status(SCO_UNAVAILABLE); service.request = Request;
    service.size = offsetof(sc_mining_v1, state) + sizeof(service.state) - 1; Status(SCO_UNAVAILABLE); service.size = sizeof(service);
    service.state = nullptr; Status(SCO_UNAVAILABLE); service.state = State;
    available = false; const int count = requests; Status(SCO_UNAVAILABLE); CHECK(requests == count); available = true;
    requestResult = SCO_FAILED; Status(SCO_UNAVAILABLE); requestResult = SCO_OK;
    stateResult = SCO_FAILED; Status(SCO_UNAVAILABLE); stateResult = SCO_OK;
    enabled = debug = 1; block = SC_MINING_BLOCK_OFF; active = 1;
    uint32_t now = 100000; tick("tick", &now, nullptr); cells = 10; now += 999; tick("tick", &now, nullptr); CHECK(logs == 0);
    ++now; tick("tick", &now, nullptr); CHECK(logs == 1); now += 1000; tick("tick", &now, nullptr); CHECK(logs == 1);
    enabled = 0; active = 0; tick("tick", &now, nullptr); CHECK(requested == 0);
    CHECK(command.fn(nullptr, 1, nullptr, nullptr, 0) == SCO_BAD_ARG);
    CHECK(command.fn(nullptr, 0, nullptr, nullptr, 1) == SCO_BAD_ARG);
    enabled = 1; active = 1; Status(SCO_OK); CHECK(requested == 1);
    test_module::Unload(); CHECK(requested == 0); const int unloaded = requests;
    test_module::Unload(); CHECK(requests == unloaded); Status(SCO_UNAVAILABLE);
    test_module::Close(); test_module::Open(argc, argv); enabled = 0; active = 0;
    CHECK(test_module::Load(&api, owner) == SCO_OK); Status(SCO_OK); test_module::Unload(); test_module::Close();
    std::puts("mining module settings/refusal/lifecycle/throttled trace PASS (synthetic)");
}
