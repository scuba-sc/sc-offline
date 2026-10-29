// SPDX-License-Identifier: GPL-3.0-only
// Independently compiled dynamic consumer fixture. Synthetic PUBLIC providers, not engine evidence.
#include "workflow_module.h"
#include <sc_missions.h>
#include <sc_economy.h>
#include <sco_content.h>
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
const void* content_table = nullptr;
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
    if (!std::strcmp(name, SCO_CONTENT_NAME)) *out = content_table;
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
unsigned objective_effects = 0, phase_effects = 0;
sco_result Objective(sco_plugin* self, uint64_t id, uint32_t phase, uint32_t objective) {
    assert(self == owner);
    if (!mutation_ready) return SCO_UNAVAILABLE;
    if (id != state.mission_id) return SCO_NOT_FOUND;
    if (phase < state.phase || (phase == state.phase && objective < state.objective)) return SCO_OK;
    if (state.state != SC_MISSION_ACTIVE || phase != state.phase || objective != state.objective || objective >= state.objectives_in_phase) return SCO_BAD_ARG;
    ++state.objective; ++objective_effects; return SCO_OK;
}
sco_result Advance(sco_plugin* self, uint64_t id, uint32_t phase) {
    assert(self == owner);
    if (!mutation_ready) return SCO_UNAVAILABLE;
    if (id != state.mission_id) return SCO_NOT_FOUND;
    if (phase < state.phase || (state.state == SC_MISSION_COMPLETED && phase == state.phase)) return SCO_OK;
    if (state.state != SC_MISSION_ACTIVE || phase != state.phase || state.objective != state.objectives_in_phase) return SCO_BAD_ARG;
    if (state.phase + 1 == state.phase_count) state.state = SC_MISSION_COMPLETED;
    else { ++state.phase; state.objective = 0; }
    ++phase_effects; return SCO_OK;
}
unsigned abandon_effects = 0, ingest_effects = 0;
std::string script_bytes = "sco-mission 1\nphases 2\nobjectives 1\n";
std::string script_name = "missions/demo.scom";
std::map<std::string, std::string> scripts;
sco_result Abandon(sco_plugin* self, uint64_t id) {
    assert(self == owner);
    if (!mutation_ready) return SCO_UNAVAILABLE;
    if (id != state.mission_id) return SCO_NOT_FOUND;
    if (state.state == SC_MISSION_ABANDONED) return SCO_OK;
    if (state.state != SC_MISSION_ACTIVE) return SCO_BAD_ARG;
    state.state = SC_MISSION_ABANDONED; ++abandon_effects; return SCO_OK;
}
sco_result Ingest(sco_plugin* self, const sc_mission_script* script) {
    assert(self == owner && script->size == sizeof(*script) && !script->flags && !script->_pad);
    if (!mutation_ready) return SCO_UNAVAILABLE;
    std::string bytes(script->bytes, script->byte_count);
    if (bytes != "sco-mission 1\nphases 2\nobjectives 1\n") return SCO_BAD_ARG;
    auto found = scripts.find(script->relative_path);
    if (found != scripts.end()) return found->second == bytes ? SCO_OK : SCO_BAD_ARG;
    scripts.emplace(script->relative_path, bytes); ++ingest_effects; return SCO_OK;
}
sco_result Item(sco_plugin* self, sco_content_kind kind, uint32_t ordinal, sco_content_item* out) {
    assert(self == owner && kind == SCO_CONTENT_SCRIPT && out->size == sizeof(*out));
    if (ordinal) return SCO_NOT_FOUND;
    out->kind = kind; out->id = 91;
    std::snprintf(out->name, sizeof(out->name), "%s", script_name.c_str()); return SCO_OK;
}
sco_result Read(sco_plugin* self, uint64_t id, void* out, uint32_t* size) {
    assert(self == owner && id == 91);
    if (*size < script_bytes.size()) { *size = static_cast<uint32_t>(script_bytes.size()); return SCO_TOO_MANY; }
    std::memcpy(out, script_bytes.data(), script_bytes.size()); *size = static_cast<uint32_t>(script_bytes.size()); return SCO_OK;
}
sco_content_v1 content{sizeof(sco_content_v1), 0, Item, Read};
sc_missions_v1 missions{sizeof(sc_missions_v1), 0, Offer, Status, Accept, Objective, Advance, Abandon, Ingest};
int64_t wallet = 100;
unsigned payment_effects = 0, payment_signals = 0;
bool uncertain_payment = false;
struct Payment { sc_economy_request request; sc_economy_receipt receipt; sco_result result; };
std::map<std::tuple<uintptr_t, uint64_t, uint64_t>, Payment> payments;
sco_result Balance(sco_plugin* self, sc_economy_balance* out) {
    assert(self == owner && out->size == sizeof(*out));
    if (!mutation_ready) return SCO_UNAVAILABLE;
    out->local_balance = wallet; return SCO_OK;
}
sco_result Apply(sco_plugin* self, const sc_economy_request* request, sc_economy_receipt* out) {
    assert(self == owner && request->size == sizeof(*request) && out->size == sizeof(*out));
    if (!mutation_ready) return SCO_WRONG_THREAD;
    auto scope = std::make_tuple(reinterpret_cast<uintptr_t>(self), state.session, request->key);
    auto found = payments.find(scope);
    if (found != payments.end()) {
        if (std::memcmp(&found->second.request, request, sizeof(*request))) return SCO_BAD_ARG;
        if (found->second.result == SCO_OK) *out = found->second.receipt;
        return found->second.result;
    }
    if (payments.size() >= SC_ECONOMY_MAX_RECEIPTS) return SCO_TOO_MANY;
    if (wallet != request->expected_balance) return SCO_FAILED;
    sc_economy_receipt receipt{sizeof(sc_economy_receipt), 0, request->key, state.session, wallet, wallet + request->delta, request->delta, payment_effects + 1};
    ++payment_effects; wallet += request->delta;
    auto result = uncertain_payment ? SCO_FAILED : SCO_OK;
    payments.emplace(scope, Payment{*request, receipt, result});
    if (result == SCO_OK) { *out = receipt; ++payment_signals; }
    return result;
}
sco_result Receipt(sco_plugin* self, uint64_t key, sc_economy_receipt* out) {
    assert(self == owner && out->size == sizeof(*out));
    auto found = payments.find(std::make_tuple(reinterpret_cast<uintptr_t>(self), state.session, key));
    if (found == payments.end()) return SCO_NOT_FOUND;
    if (found->second.result == SCO_OK) *out = found->second.receipt;
    return found->second.result;
}
sc_economy_v1 economy{sizeof(sc_economy_v1), 0, Balance, Apply, Receipt};
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
void EconomyCase() {
    assert(Call("contracts.balance") == SCO_OK && reply.find("100") != std::string::npos);
    assert(Call("contracts.apply", {Int(9), Int(100), Int(25)}) == SCO_OK && wallet == 125);
    assert(Call("contracts.apply", {Int(9), Int(100), Int(25)}) == SCO_OK && payment_effects == 1 && payment_signals == 1);
    assert(Call("contracts.apply", {Int(9), Int(125), Int(25)}) == SCO_BAD_ARG && payment_effects == 1);
    assert(Call("contracts.receipt", {Int(9)}) == SCO_OK && reply.find("session 1") != std::string::npos);
    assert(Call("contracts.apply", {Int(10), Int(125), Int(INT64_MIN)}) == SCO_BAD_ARG);
    assert(Call("contracts.apply", {Int(10), Int(125), Int(-126)}) == SCO_BAD_ARG);
    assert(Call("contracts.apply", {Int(10), Int(125), Int(0)}) == SCO_BAD_ARG);
    mutation_ready = false;
    assert(Call("contracts.apply", {Int(10), Int(125), Int(1)}) == SCO_WRONG_THREAD && payment_effects == 1);
    mutation_ready = true;
    assert(Call("contracts.cancel") == SCO_OK);
    assert(Call("contracts.apply", {Int(10), Int(125), Int(1)}) == SCO_UNAVAILABLE && payment_effects == 1);
    assert(Call("contracts.receipt", {Int(9)}) == SCO_OK);
    assert(Call("contracts.resume") == SCO_OK);
    uncertain_payment = true;
    assert(Call("contracts.apply", {Int(10), Int(125), Int(1)}) == SCO_FAILED && payment_effects == 2);
    uncertain_payment = false;
    assert(Call("contracts.apply", {Int(10), Int(125), Int(1)}) == SCO_FAILED && payment_effects == 2 && payment_signals == 1);
    assert(Call("contracts.receipt", {Int(10)}) == SCO_FAILED);
    available = false;
    assert(Call("contracts.balance") == SCO_UNAVAILABLE);
    available = true;
    size_t length = offsetof(sc_economy_v1, apply) + 1;
    void* partial = std::malloc(length); std::memset(partial, 0, length);
    *static_cast<uint32_t*>(partial) = static_cast<uint32_t>(length); economy_table = partial;
    assert(Call("contracts.apply", {Int(11), Int(126), Int(1)}) == SCO_UNAVAILABLE && payment_effects == 2);
    std::free(partial); economy_table = &economy;
    auto callback = economy.apply; economy.apply = nullptr;
    assert(Call("contracts.apply", {Int(11), Int(126), Int(1)}) == SCO_UNAVAILABLE); economy.apply = callback;
    ++state.session;
    assert(Call("contracts.receipt", {Int(9)}) == SCO_NOT_FOUND);
    assert(Call("contracts.apply", {Int(9), Int(126), Int(-1)}) == SCO_OK && wallet == 125);
    test_module::Unload(); commands.clear(); owner = reinterpret_cast<sco_plugin*>(uintptr_t{2});
    assert(test_module::Load(&api, owner) == SCO_OK);
    assert(Call("contracts.receipt", {Int(9)}) == SCO_NOT_FOUND);
    assert(Call("contracts.apply", {Int(9), Int(125), Int(-1)}) == SCO_OK && wallet == 124);
    // Non-evicting provider receipt quota; consumer must propagate refusal, not invent new keys/retry.
    for (uint32_t key = 20; payments.size() < SC_ECONOMY_MAX_RECEIPTS; ++key) {
        assert(Call("contracts.apply", {Int(key), Int(wallet), Int(1)}) == SCO_OK);
    }
    auto before = payment_effects;
    assert(Call("contracts.apply", {Int(9999), Int(wallet), Int(1)}) == SCO_TOO_MANY && payment_effects == before);
}
void ScriptCase() {
    assert(Call("contracts.accept", {Int(0), Int(1)}) == SCO_OK);
    assert(Call("contracts.abandon", {Int(77)}) == SCO_OK);
    assert(Call("contracts.abandon", {Int(77)}) == SCO_OK && abandon_effects == 1);
    assert(Call("contracts.ingest", {Int(0)}) == SCO_OK && scripts.contains("missions/demo.scom"));
    assert(Call("contracts.ingest", {Int(0)}) == SCO_OK && ingest_effects == 1);
    script_bytes = "<!DOCTYPE mission>";
    assert(Call("contracts.ingest", {Int(0)}) == SCO_BAD_ARG && ingest_effects == 1);
    script_bytes = "sco-mission 1\nphases 2\nobjectives 1\n";
    script_name = "../missions/demo.scom";
    assert(Call("contracts.ingest", {Int(0)}) == SCO_BAD_ARG && ingest_effects == 1);
    // The id cap is the rule that decides a long name: 100 characters is accepted and 101 is not.
    // (The path-capacity check in the plugin cannot be reached from here, because an accepted id
    // always fits relative_path; a static_assert in contracts.cpp holds that relationship.)
    script_name = "missions/" + std::string(100, 'a') + ".scom";
    assert(Call("contracts.ingest", {Int(0)}) == SCO_OK && ingest_effects == 2);
    script_name = "missions/" + std::string(101, 'a') + ".scom";
    assert(Call("contracts.ingest", {Int(0)}) == SCO_BAD_ARG && ingest_effects == 2);
    script_name = "missions/demo.scom";
    script_bytes.assign(SC_MISSIONS_SCRIPT_BYTES + 1, 'a');
    assert(Call("contracts.ingest", {Int(0)}) == SCO_BAD_ARG && ingest_effects == 1);
    assert(Call("contracts.cancel") == SCO_OK);
    unsigned before = reads;
    assert(Call("contracts.accept", {Int(0), Int(2)}) == SCO_UNAVAILABLE && reads == before);
    assert(Call("contracts.ingest", {Int(0)}) == SCO_UNAVAILABLE);
    assert(Call("contracts.status") == SCO_OK); // cancellation never hides reads/receipts
    assert(Call("contracts.resume") == SCO_OK);
    assert(Call("contracts.accept", {Int(0), Int(1)}) == SCO_OK && accept_effects == 1);
    content_table = nullptr;
    assert(Call("contracts.ingest", {Int(0)}) == SCO_UNAVAILABLE);
    content_table = &content;
    auto callback = missions.ingest; missions.ingest = nullptr;
    assert(Call("contracts.ingest", {Int(0)}) == SCO_UNAVAILABLE); missions.ingest = callback;
    test_module::Unload(); test_module::Unload();
    assert(Call("contracts.abandon", {Int(77)}) == SCO_UNAVAILABLE && abandon_effects == 1);
}
void ProgressCase() {
    assert(Call("contracts.accept", {Int(0), Int(1)}) == SCO_OK);
    assert(Call("contracts.advance", {Int(77), Int(0)}) == SCO_BAD_ARG && phase_effects == 0);
    assert(Call("contracts.objective", {Int(77), Int(0), Int(1)}) == SCO_BAD_ARG && objective_effects == 0);
    assert(Call("contracts.objective", {Int(77), Int(0), Int(0)}) == SCO_OK);
    assert(Call("contracts.objective", {Int(77), Int(0), Int(0)}) == SCO_OK && objective_effects == 1);
    assert(Call("contracts.advance", {Int(77), Int(0)}) == SCO_OK);
    assert(Call("contracts.advance", {Int(77), Int(0)}) == SCO_OK && phase_effects == 1);
    mutation_ready = false;
    assert(Call("contracts.objective", {Int(77), Int(1), Int(0)}) == SCO_UNAVAILABLE && objective_effects == 1);
    mutation_ready = true;
    assert(Call("contracts.objective", {Int(77), Int(1), Int(0)}) == SCO_OK);
    assert(Call("contracts.advance", {Int(77), Int(1)}) == SCO_OK && state.state == SC_MISSION_COMPLETED);
    assert(Call("contracts.advance", {Int(77), Int(1)}) == SCO_OK && phase_effects == 2);
    assert(Call("contracts.objective", {Int(77), Int(INT64_MAX), Int(0)}) == SCO_BAD_ARG);
    auto callback = missions.advance; missions.advance = nullptr;
    assert(Call("contracts.advance", {Int(77), Int(1)}) == SCO_UNAVAILABLE); missions.advance = callback;
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
    missions_table = &missions; economy_table = &economy; content_table = &content;
    api.size = sizeof(api); api.major = SCO_API_MAJOR; api.minor = SCO_API_MINOR;
    api.register_command = Register; api.query_service = Query;
    // Check API admission before version/pointer tails, and do not retain failed opens.
    size_t api_length = offsetof(sco_api, query_service) + 1;
    void* partial_api = std::malloc(api_length); std::memset(partial_api, 0, api_length);
    *static_cast<uint32_t*>(partial_api) = static_cast<uint32_t>(api_length);
    assert(test_module::Load(static_cast<const sco_api*>(partial_api), owner) == SCO_UNAVAILABLE);
    std::free(partial_api);
    sco_api incompatible = api; incompatible.major = 99;
    assert(test_module::Load(&incompatible, owner) == SCO_UNAVAILABLE);
    assert(test_module::Load(&api, owner) == SCO_OK);
    if (!std::strcmp(argv[2], "read")) ReadCase();
    else if (!std::strcmp(argv[2], "accept")) AcceptCase();
    else if (!std::strcmp(argv[2], "progress")) ProgressCase();
    else if (!std::strcmp(argv[2], "script")) ScriptCase();
    else if (!std::strcmp(argv[2], "economy")) EconomyCase();
    else return 2;
    test_module::Unload(); test_module::Close();
    std::printf("contracts %s: PASS (dynamic module, synthetic public provider; not run in game)\n", argv[2]);
}
