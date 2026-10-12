// SPDX-License-Identifier: GPL-3.0-only
// Actual dynamic module, synthetic public providers. No host guard/game is linked.
#include "workflow_module.h"
#include "sc_creative.h"
#include "sco_settings.h"
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <map>
#include <string>

namespace {
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
std::map<std::string, sco_command> commands;
sco_plugin* const owner = reinterpret_cast<sco_plugin*>(&commands);
const sc_creative_v1* creative = nullptr;
const sco_settings_v1* settings = nullptr;
sco_result query_result = SCO_OK, provider_result = SCO_OK, register_result = SCO_OK;
sco_result setting_result = SCO_OK, settings_query_result = SCO_OK;
const char* denied = nullptr;
int calls = 0, errors = 0;
uint32_t flags = 0;
float speed = 23; // Synthetic pre-plugin game default.
std::map<uint32_t, sco_plugin*> owners;
sco_plugin* speed_owner = nullptr;
std::string last_reply;
int Has(const char* cap) { return !denied || std::strcmp(cap, denied); }
void Log(sco_plugin* self, sco_log_level, const char*) { CHECK(self == owner); }
sco_result Register(sco_plugin* self, const sco_command* cmd) {
    CHECK(self == owner && cmd->size == sizeof(*cmd) && cmd->fn);
    commands.emplace(cmd->name, *cmd); return register_result;
}
sco_result Query(const char* name, uint32_t version, const void** out) {
    CHECK(version == 0x00010000u);
    *out = std::strcmp(name, SC_CREATIVE_SERVICE_NAME) == 0 ? static_cast<const void*>(creative)
                                                         : static_cast<const void*>(settings);
    // Deliberately leave a pointer on failure: the consumer must trust the result, not stale data.
    if (std::strcmp(name, SCO_SETTINGS_NAME) == 0 && settings_query_result != SCO_OK) return settings_query_result;
    return query_result == SCO_OK ? (*out ? SCO_OK : SCO_NOT_FOUND) : query_result;
}
sco_result Set(sco_plugin* self, int on, uint32_t bit) {
    CHECK(self == owner); ++calls;
    if (provider_result != SCO_OK) return provider_result;
    if (on) { flags |= bit; owners[bit] = self; }
    else { flags &= ~bit; owners.erase(bit); }
    return SCO_OK;
}
sco_result God(sco_plugin* s, int on) { return Set(s, on, SC_CREATIVE_GOD_MODE); }
sco_result Fly(sco_plugin* s, int on) { return Set(s, on, SC_CREATIVE_FLY); }
sco_result Ammo(sco_plugin* s, int on) { return Set(s, on, SC_CREATIVE_INFINITE_AMMO); }
sco_result Ship(sco_plugin* s, int on) { return Set(s, on, SC_CREATIVE_INFINITE_SHIP_AMMO); }
sco_result Speed(sco_plugin* self, float value) {
    CHECK(self == owner); ++calls;
    if (provider_result == SCO_OK) { speed = value; speed_owner = self; }
    return provider_result;
}
sco_result State(sco_plugin* self, uint32_t* out) {
    CHECK(self == owner); ++calls; if (provider_result == SCO_OK) *out = flags; return provider_result;
}
sco_result Error(sco_plugin* self, char* out, uint32_t* size) {
    CHECK(self == owner); ++errors;
    const char text[] = "synthetic refusal";
    if (*size < sizeof(text)) { *size = sizeof(text); return SCO_TOO_MANY; }
    std::memcpy(out, text, sizeof(text)); *size = sizeof(text); return SCO_OK;
}
sc_creative_v1 Full() {
    sc_creative_v1 t{}; t.size = sizeof(t); t.set_god_mode = God; t.set_fly_mode = Fly;
    t.set_fly_speed = Speed; t.set_infinite_ammo = Ammo; t.set_infinite_ship_ammo = Ship;
    t.state = State; t.last_error = Error; return t;
}
sco_api Api() {
    sco_api a{}; a.size = sizeof(a); a.major = SCO_API_MAJOR; a.minor = SCO_API_MINOR;
    a.has = Has; a.log = Log; a.register_command = Register; a.query_service = Query; return a;
}
sco_result Invoke(const char* name, const sco_arg* arg = nullptr, uint32_t n = 0) {
    auto c = commands.at(name); char reply[512]{};
    auto r = c.fn(arg, n, c.ctx, reply, sizeof(reply)); last_reply = reply; return r;
}
sco_arg Bool(bool b) { sco_arg a{}; a.type = SCO_ARG_BOOL; a.v.i = b; return a; }
sco_arg Float(double f) { sco_arg a{}; a.type = SCO_ARG_FLOAT; a.v.f = f; return a; }
void Unload() { test_module::Unload(); commands.clear(); }
double configured_speed = 12;
int reads = 0;
sco_result GetFloat(sco_plugin* self, const char* name, double* out) {
    CHECK(self == owner && std::strcmp(name, "noclip_speed") == 0); ++reads;
    if (setting_result == SCO_OK) *out = configured_speed;
    return setting_result;
}
void SettingsAndLifecycle() {
    auto a = Api(); auto full = Full(); creative = &full;
    sco_settings_v1 st{}; st.size = sizeof(st); st.get_float = GetFloat; settings = &st;
    flags = 0; owners.clear(); speed = 23; speed_owner = nullptr;
    const int before_load = calls;
    CHECK(test_module::Load(&a, owner) == SCO_OK); CHECK(calls == before_load && reads == 0);
    auto on = Bool(true);
    CHECK(Invoke("creative.noclip", &on, 1) == SCO_OK); CHECK(speed == 12 && reads == 1);
    configured_speed = 14;
    CHECK(Invoke("creative.noclip", &on, 1) == SCO_OK); CHECK(speed == 14 && reads == 2);
    for (size_t size = sizeof(uint32_t); size < offsetof(sco_settings_v1, get_float) + sizeof(st.get_float); ++size) {
        auto* bytes = static_cast<unsigned char*>(std::malloc(size)); CHECK(bytes);
        std::memcpy(bytes, &st, size); uint32_t extent = static_cast<uint32_t>(size);
        std::memcpy(bytes, &extent, sizeof(extent)); settings = reinterpret_cast<sco_settings_v1*>(bytes);
        const int c = calls, r = reads;
        CHECK(Invoke("creative.noclip", &on, 1) == SCO_UNAVAILABLE); CHECK(calls == c && reads == r);
        std::free(bytes);
    }
    settings = &st; st.get_float = nullptr;
    const int c = calls;
    CHECK(Invoke("creative.noclip", &on, 1) == SCO_UNAVAILABLE); CHECK(calls == c);
    st.get_float = GetFloat;
    for (auto r : {SCO_FAILED, SCO_UNAVAILABLE, SCO_NOT_FOUND}) {
        setting_result = r; CHECK(Invoke("creative.noclip", &on, 1) == r); CHECK(calls == c);
    }
    setting_result = SCO_OK;
    configured_speed = std::numeric_limits<double>::quiet_NaN();
    CHECK(Invoke("creative.noclip", &on, 1) == SCO_BAD_ARG); CHECK(calls == c);
    settings_query_result = SCO_UNAVAILABLE;
    CHECK(Invoke("creative.noclip", &on, 1) == SCO_UNAVAILABLE); CHECK(calls == c);
    settings_query_result = SCO_OK; configured_speed = 18;
    CHECK(Invoke("creative.noclip", &on, 1) == SCO_OK); CHECK(speed == 18);
    settings = nullptr; // Optional settings absence preserves the existing game-speed behavior.
    CHECK(Invoke("creative.noclip", &on, 1) == SCO_OK); CHECK(speed == 18);
    for (auto name : {"creative.god", "creative.ammo", "creative.ship_ammo"}) CHECK(Invoke(name, &on, 1) == SCO_OK);
    auto faster = Float(24); CHECK(Invoke("creative.noclip_speed", &faster, 1) == SCO_OK); CHECK(speed == 24);
    CHECK(Invoke("creative.status") == SCO_OK); CHECK(last_reply.find("god on, noclip on, ammo on, ship ammo on") != std::string::npos);
    auto off = Bool(false);
    for (auto name : {"creative.god", "creative.noclip", "creative.ammo", "creative.ship_ammo"})
        CHECK(Invoke(name, &off, 1) == SCO_OK);
    CHECK(flags == 0);
    for (auto name : {"creative.god", "creative.noclip", "creative.ammo", "creative.ship_ammo"})
        CHECK(Invoke(name, &on, 1) == SCO_OK);
    // Another plugin takes ownership. Creative unload must not turn its god mode off.
    auto* foreign = reinterpret_cast<sco_plugin*>(&reads); owners[SC_CREATIVE_GOD_MODE] = foreign;
    auto saved = commands.at("creative.god"); const int before_unload = calls;
    Unload(); test_module::Unload(); CHECK(calls == before_unload);
    char reply[128]{};
    CHECK(saved.fn(&on, 1, saved.ctx, reply, sizeof(reply)) == SCO_NOT_FOUND); CHECK(calls == before_unload);
    // Stand-in for the documented host/provider release hook, NOT an actual host-guard proof.
    for (auto it = owners.begin(); it != owners.end();) {
        if (it->second == owner) { flags &= ~it->first; it = owners.erase(it); } else ++it;
    }
    if (speed_owner == owner) { speed = 23; speed_owner = nullptr; }
    CHECK(flags == SC_CREATIVE_GOD_MODE && speed == 23);
    CHECK(test_module::Load(&a, owner) == SCO_OK); CHECK(commands.size() == 6);
    CHECK(saved.fn(&on, 1, saved.ctx, reply, sizeof(reply)) == SCO_NOT_FOUND);
    Unload(); creative = nullptr; owners.clear(); flags = 0;
    std::puts("creative settings/lifecycle: PASS (provider release-hook stand-in)");
}
void RejectsInvalidInputs() {
    auto a = Api(); auto full = Full(); creative = &full;
    CHECK(test_module::Load(&a, owner) == SCO_OK);
    for (double value : {0.0, -1.0, 10001.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto arg = Float(value); const int before = calls;
        CHECK(Invoke("creative.noclip_speed", &arg, 1) == SCO_BAD_ARG); CHECK(calls == before);
    }
    auto arg = Float(10); const int before = calls;
    CHECK(Invoke("creative.god", &arg, 1) == SCO_BAD_ARG);
    arg = Bool(true); arg.v.i = 2;
    CHECK(Invoke("creative.god", &arg, 1) == SCO_BAD_ARG);
    CHECK(Invoke("creative.god") == SCO_BAD_ARG);
    CHECK(Invoke("creative.status", &arg, 1) == SCO_BAD_ARG);
    CHECK(Invoke("creative.noclip_speed", &arg, 1) == SCO_BAD_ARG);
    CHECK(calls == before);
    Unload(); creative = nullptr;
    std::puts("creative invalid inputs: PASS");
}
void RejectsHostTablesAndFailedLoad() {
    auto a = Api();
    for (size_t size = sizeof(uint32_t); size < offsetof(sco_api, query_service) + sizeof(a.query_service); ++size) {
        auto* bytes = static_cast<unsigned char*>(std::malloc(size)); CHECK(bytes);
        std::memcpy(bytes, &a, size); uint32_t extent = static_cast<uint32_t>(size);
        std::memcpy(bytes, &extent, sizeof(extent));
        CHECK(test_module::Load(reinterpret_cast<sco_api*>(bytes), owner) == SCO_UNAVAILABLE);
        CHECK(commands.empty()); std::free(bytes);
    }
    CHECK(test_module::Load(nullptr, owner) == SCO_BAD_ARG);
    CHECK(test_module::Load(&a, nullptr) == SCO_BAD_ARG);
    for (int i = 0; i < 6; ++i) {
        auto bad = a;
        switch (i) { case 0: bad.has = nullptr; break; case 1: bad.log = nullptr; break;
            case 2: bad.register_command = nullptr; break; case 3: bad.query_service = nullptr; break;
            case 4: bad.major = 2; break; case 5: bad.minor = 0; break; }
        CHECK(test_module::Load(&bad, owner) == SCO_UNAVAILABLE); CHECK(commands.empty());
    }
    register_result = SCO_TOO_MANY;
    CHECK(test_module::Load(&a, owner) == SCO_TOO_MANY);
    auto saved = commands.begin()->second; char reply[32]{};
    CHECK(saved.fn(nullptr, 0, saved.ctx, reply, sizeof(reply)) == SCO_NOT_FOUND);
    commands.clear(); register_result = SCO_OK;
    CHECK(test_module::Load(&a, owner) == SCO_OK); CHECK(commands.size() == 6);
    CHECK(test_module::Load(&a, owner) == SCO_BAD_ARG);
    Unload(); test_module::Unload();
    std::puts("creative host extent/null/version/failed-load: PASS");
}
void ServiceBoundaries() {
    auto a = Api(); auto full = Full(); creative = &full;
    CHECK(test_module::Load(&a, owner) == SCO_OK);
    CHECK(commands.size() == 6);
    const char* names[] = {"creative.god", "creative.noclip", "creative.noclip_speed", "creative.ammo", "creative.ship_ammo", "creative.status"};
    const size_t offsets[] = {offsetof(sc_creative_v1, set_god_mode), offsetof(sc_creative_v1, set_fly_mode),
        offsetof(sc_creative_v1, set_fly_speed), offsetof(sc_creative_v1, set_infinite_ammo),
        offsetof(sc_creative_v1, set_infinite_ship_ammo), offsetof(sc_creative_v1, state)};
    for (size_t i = 0; i < 6; ++i) {
        auto arg = i == 2 ? Float(10) : Bool(false);
        for (size_t size = sizeof(uint32_t); size < offsets[i] + sizeof(void*); ++size) {
            // Physical allocation stops inside the pointer, not just an advertised short size.
            auto* bytes = static_cast<unsigned char*>(std::malloc(size)); CHECK(bytes);
            std::memcpy(bytes, &full, size); uint32_t extent = static_cast<uint32_t>(size);
            std::memcpy(bytes, &extent, sizeof(extent)); creative = reinterpret_cast<sc_creative_v1*>(bytes);
            const int before = calls;
            CHECK(Invoke(names[i], i == 5 ? nullptr : &arg, i == 5 ? 0 : 1) == SCO_UNAVAILABLE);
            CHECK(calls == before); std::free(bytes);
        }
        auto missing = full;
        switch (i) {
            case 0: missing.set_god_mode = nullptr; break; case 1: missing.set_fly_mode = nullptr; break;
            case 2: missing.set_fly_speed = nullptr; break; case 3: missing.set_infinite_ammo = nullptr; break;
            case 4: missing.set_infinite_ship_ammo = nullptr; break; case 5: missing.state = nullptr; break;
        }
        creative = &missing; const int before = calls;
        CHECK(Invoke(names[i], i == 5 ? nullptr : &arg, i == 5 ? 0 : 1) == SCO_UNAVAILABLE);
        CHECK(calls == before);
    }
    creative = &full;
    for (auto result : {SCO_NOT_FOUND, SCO_UNAVAILABLE, SCO_FAILED}) {
        query_result = result; const int before = calls;
        CHECK(Invoke("creative.status") == SCO_UNAVAILABLE); CHECK(calls == before);
    }
    query_result = SCO_OK; creative = nullptr;
    CHECK(Invoke("creative.status") == SCO_UNAVAILABLE);
    creative = &full; CHECK(Invoke("creative.status") == SCO_OK);
    const char* caps[] = {"game.creative.god_mode", "game.creative.fly", "game.creative.fly",
        "game.creative.ammo", "game.creative.ship_ammo"};
    for (size_t i = 0; i < 5; ++i) {
        auto input = i == 2 ? Float(10) : Bool(false); denied = caps[i]; const int before = calls;
        CHECK(Invoke(names[i], &input, 1) == SCO_UNAVAILABLE); CHECK(calls == before);
    }
    denied = nullptr; auto arg = Bool(true);
    for (auto r : {SCO_WRONG_THREAD, SCO_FAILED, SCO_CRASHED}) {
        provider_result = r; CHECK(Invoke("creative.god", &arg, 1) == r);
        CHECK(last_reply.find("synthetic refusal") != std::string::npos);
    }
    provider_result = SCO_FAILED;
    for (size_t size = sizeof(uint32_t); size < offsetof(sc_creative_v1, last_error) + sizeof(full.last_error); ++size) {
        auto* bytes = static_cast<unsigned char*>(std::malloc(size)); CHECK(bytes);
        std::memcpy(bytes, &full, size); uint32_t extent = static_cast<uint32_t>(size);
        std::memcpy(bytes, &extent, sizeof(extent)); creative = reinterpret_cast<sc_creative_v1*>(bytes);
        const int before = errors;
        CHECK(Invoke("creative.god", &arg, 1) == (size < offsetof(sc_creative_v1, set_god_mode) + sizeof(full.set_god_mode) ? SCO_UNAVAILABLE : SCO_FAILED));
        CHECK(errors == before); std::free(bytes);
    }
    full.last_error = nullptr; creative = &full; CHECK(Invoke("creative.god", &arg, 1) == SCO_FAILED);
    provider_result = SCO_OK; Unload(); creative = nullptr;
    std::puts("creative service extents/null/reopen/capability/refusal: PASS");
}
}
int main(int argc, char** argv) {
    test_module::Open(argc, argv);
    const auto* info = test_module::Query();
    CHECK(info && info->size == sizeof(*info) && info->api_major == SCO_API_MAJOR && info->api_minor == SCO_API_MINOR);
    CHECK(std::strcmp(info->name, "creative") == 0 && std::strcmp(info->version, "1.1.0") == 0);
    ServiceBoundaries();
    RejectsInvalidInputs();
    SettingsAndLifecycle();
    RejectsHostTablesAndFailedLoad();
    test_module::Close();
    test_module::Open(argc, argv); // Physical module close/reopen, not a linked-in forwarding fixture.
    auto api = Api();
    CHECK(test_module::Load(&api, owner) == SCO_OK);
    CHECK(Invoke("creative.status") == SCO_UNAVAILABLE);
    Unload(); test_module::Close();
    std::puts("creative module workflow: PASS (synthetic, not run in game)");
}
