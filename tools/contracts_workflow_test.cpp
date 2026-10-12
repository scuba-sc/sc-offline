// SPDX-License-Identifier: GPL-3.0-only
// Independently compiled dynamic consumer fixture. Synthetic PUBLIC providers, not engine evidence.
#include "workflow_module.h"
#include <sc_missions.h>
#include <sc_economy.h>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <cstdio>
#include <cstdlib>

namespace {
std::map<std::string, sco_command> commands;
sco_plugin* owner = reinterpret_cast<sco_plugin*>(uintptr_t{1});
bool available = true;
const void* missions_table = nullptr;
const void* economy_table = nullptr;
sc_mission_offer offered{sizeof(sc_mission_offer), 0, 11, 22, 2, 1, 100, 1};
sc_mission_status state{sizeof(sc_mission_status), SC_MISSION_ACTIVE, 77, 11, 0, 0, 2, 1, 1};
unsigned reads = 0;
sco_result Register(sco_plugin* self, const sco_command* command) {
    assert(self == owner && command && command->size == sizeof(*command));
    return commands.emplace(command->name, *command).second ? SCO_OK : SCO_BAD_ARG;
}
sco_result Query(const char* name, uint32_t version, const void** out) {
    assert(version == 0x10000u);
    *out = nullptr;
    if (!available) return SCO_NOT_FOUND;
    if (!std::strcmp(name, SC_MISSIONS_SERVICE_NAME)) *out = missions_table;
    if (!std::strcmp(name, SC_ECONOMY_SERVICE_NAME)) *out = economy_table;
    return *out ? SCO_OK : SCO_NOT_FOUND;
}
sco_result Offer(sco_plugin* self, uint32_t index, sc_mission_offer* out) {
    assert(self == owner && out->size == sizeof(*out)); ++reads;
    if (index) return SCO_NOT_FOUND;
    *out = offered; return SCO_OK;
}
sco_result Status(sco_plugin* self, uint64_t id, sc_mission_status* out) {
    assert(self == owner && out->size == sizeof(*out)); ++reads;
    if (id != state.mission_id) return SCO_NOT_FOUND;
    *out = state; return SCO_OK;
}
unsigned accept_effects = 0;
bool mutation_ready = true;
struct Acceptance { sc_mission_offer offer; uint64_t id; };
std::map<std::tuple<uintptr_t, uint64_t, uint64_t>, Acceptance> acceptances;
sco_result Accept(sco_plugin* self, const sc_mission_offer* offer, uint64_t key, uint64_t* id) {
    assert(self == owner && offer->size == sizeof(*offer) && key);
    if (!mutation_ready) return SCO_WRONG_THREAD;
    auto scope = std::make_tuple(reinterpret_cast<uintptr_t>(self), state.session, key);
    auto found = acceptances.find(scope);
    if (found != acceptances.end()) {
        if (std::memcmp(&found->second.offer, offer, sizeof(*offer))) return SCO_BAD_ARG;
        *id = found->second.id; return SCO_OK;
    }
    if (offer->generation != offered.generation || offer->broker_id != offered.broker_id) return SCO_BAD_ARG;
    if (acceptances.size() >= SC_MISSIONS_MAX_ACTIVE) return SCO_TOO_MANY;
    *id = state.mission_id; acceptances.emplace(scope, Acceptance{*offer, *id});
    ++accept_effects; return SCO_OK;
}
sc_missions_v1 missions{sizeof(sc_missions_v1), 0, Offer, Status, Accept, nullptr, nullptr, nullptr, nullptr};
sc_economy_v1 economy{sizeof(sc_economy_v1), 0, nullptr, nullptr, nullptr};
sco_api api{};
std::string reply;
sco_arg Int(int64_t value) { sco_arg a{}; a.type = SCO_ARG_INT; a.v.i = value; return a; }
sco_result Call(const char* name, std::initializer_list<sco_arg> args = {}) {
    auto it = commands.find(name);
    if (it == commands.end()) return SCO_NOT_FOUND;
    char buffer[512]{};
    auto r = it->second.fn(args.begin(), static_cast<uint32_t>(args.size()), it->second.ctx, buffer, sizeof(buffer));
    reply = buffer; return r;
}
void AcceptCase() {
    assert(Call("contracts.accept", {Int(0), Int(9)}) == SCO_OK && reply.find("77") != std::string::npos);
    assert(Call("contracts.accept", {Int(0), Int(9)}) == SCO_OK && accept_effects == 1);
    ++offered.generation;
    assert(Call("contracts.accept", {Int(0), Int(9)}) == SCO_BAD_ARG && accept_effects == 1);
    assert(Call("contracts.accept", {Int(0), Int(0)}) == SCO_BAD_ARG);
    mutation_ready = false;
    assert(Call("contracts.accept", {Int(0), Int(10)}) == SCO_WRONG_THREAD && accept_effects == 1);
    mutation_ready = true;
    available = false;
    assert(Call("contracts.accept", {Int(0), Int(10)}) == SCO_UNAVAILABLE && accept_effects == 1);
    available = true;
    auto callback = missions.accept; missions.accept = nullptr;
    assert(Call("contracts.accept", {Int(0), Int(10)}) == SCO_UNAVAILABLE);
    missions.accept = callback;
    uint32_t size = missions.size; missions.size = offsetof(sc_missions_v1, accept) + 1;
    assert(Call("contracts.accept", {Int(0), Int(10)}) == SCO_UNAVAILABLE); missions.size = size;
    test_module::Unload(); commands.clear(); owner = reinterpret_cast<sco_plugin*>(uintptr_t{2});
    assert(test_module::Load(&api, owner) == SCO_OK);
    --offered.generation;
    assert(Call("contracts.accept", {Int(0), Int(9)}) == SCO_OK && accept_effects == 2);
    ++state.session;
    assert(Call("contracts.accept", {Int(0), Int(9)}) == SCO_OK && accept_effects == 3);
}
void ReadCase() {
    assert(Call("contracts.status") == SCO_OK);
    assert(reply.find("synthetic") == std::string::npos); // production reply must not pretend provider evidence
    assert(Call("contracts.offer", {Int(0)}) == SCO_OK && reply.find("11") != std::string::npos);
    assert(Call("contracts.mission", {Int(77)}) == SCO_OK && reply.find("session 1") != std::string::npos);
    assert(Call("contracts.offer", {Int(-1)}) == SCO_BAD_ARG);
    unsigned before = reads;
    available = false;
    assert(Call("contracts.status") == SCO_UNAVAILABLE && reply.find("unavailable") != std::string::npos);
    assert(reads == before); available = true;
    auto saved = missions.status; missions.status = nullptr;
    assert(Call("contracts.mission", {Int(77)}) == SCO_UNAVAILABLE); missions.status = saved;
    // Physical allocation ends INSIDE the pointer; guard must never read the pointer tail.
    size_t length = offsetof(sc_missions_v1, status) + 1;
    void* partial = std::malloc(length); std::memset(partial, 0, length);
    *static_cast<uint32_t*>(partial) = static_cast<uint32_t>(length);
    missions_table = partial;
    assert(Call("contracts.mission", {Int(77)}) == SCO_UNAVAILABLE);
    std::free(partial); missions_table = &missions;
    test_module::Unload(); test_module::Unload();
    assert(Call("contracts.status") == SCO_UNAVAILABLE);
    commands.clear(); assert(test_module::Load(&api, owner) == SCO_OK);
    assert(Call("contracts.status") == SCO_OK);
}
}
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    test_module::Open(2, argv);
    assert(!std::strcmp(test_module::Query()->name, "contracts"));
    missions_table = &missions; economy_table = &economy;
    api.size = sizeof(api); api.major = SCO_API_MAJOR; api.minor = SCO_API_MINOR;
    api.register_command = Register; api.query_service = Query;
    assert(test_module::Load(&api, owner) == SCO_OK);
    if (!std::strcmp(argv[2], "read")) ReadCase();
    else if (!std::strcmp(argv[2], "accept")) AcceptCase();
    else return 2;
    test_module::Unload(); test_module::Close();
    std::printf("contracts %s: PASS (dynamic module, synthetic public provider; not run in game)\n", argv[2]);
}
