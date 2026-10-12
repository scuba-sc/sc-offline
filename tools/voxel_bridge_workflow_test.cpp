// SPDX-License-Identifier: GPL-3.0-only
// Actual module; synthetic public providers, not Windows/game/cross-process proof.
#include "workflow_module.h"
#include <sc_game_spatial.h>
#include <sc_spatial.h>
#include <sc_entities.h>
#include <sc_world.h>
#include <sco_ipc.h>
#include <sc_voxel_bridge.h>
#include <sco_widgets.h>
#include <sco_ui.h>
#include <vector>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

namespace {
std::map<std::string, sco_command> commands;
sco_event_fn tick = nullptr;
int handle;
sco_plugin* self = reinterpret_cast<sco_plugin*>(&handle);
sco_result Register(sco_plugin* owner, const sco_command* command) {
    assert(owner == self); commands[command->name] = *command; return SCO_OK;
}
sco_result Subscribe(sco_plugin* owner, const char* event, sco_event_fn fn, void*) {
    assert(owner == self && !std::strcmp(event, "tick")); tick = fn; return SCO_OK;
}
sco_result Unsubscribe(sco_plugin*, const char*, sco_event_fn) { tick = nullptr; return SCO_OK; }
std::string lastLog;
void Log(sco_plugin*, sco_log_level, const char* text) { lastLog = text; }
bool providers = false;
alignas(64) unsigned char mapping[VX_CHANNEL_BYTES] = {};
sc_ipc_chan channel = {}, peer = {};
sc_ipc_ring_view outgoing = {}, incoming = {};
uint64_t now = 1000;
bool open = false;
int creates = 0, closes = 0, spawns = 0, removals = 0, rays = 0, conversions = 0;
uint64_t playerZone = 7;
bool removalFail = false, rayMissing = false;
std::map<uint64_t, bool> owned;
double lastPos[3] = {}, lastRot[4] = {};
sco_result Wire(int result) {
    if (result == SC_IPC_OK) return SCO_OK;
    if (result == SC_IPC_NOT_READY || result == SC_IPC_EMPTY) return SCO_NOT_FOUND;
    if (result == SC_IPC_TOO_SMALL || result == SC_IPC_FULL) return SCO_TOO_MANY;
    return SCO_FAILED;
}
sco_result Create(sco_plugin* owner, const char* name, uint64_t bytes, uint32_t layout, uint32_t version, uint64_t* id) {
    assert(owner == self && !open && !std::strcmp(name, VX_CHANNEL_NAME));
    assert(bytes == VX_CHANNEL_BYTES && layout == VX_LAYOUT_ID && version == VX_LAYOUT_VERSION);
    std::memset(mapping, 0, sizeof(mapping));
    assert(sc_ipc_init(mapping, bytes, 1, layout, version, now, &channel) == SC_IPC_OK);
    open = true; ++creates; *id = 1; return SCO_OK;
}
sco_result Age(sco_plugin* owner, uint64_t id, uint32_t* age) {
    assert(owner == self && id == 1 && open);
    if (sc_ipc_check(&channel) != SC_IPC_OK) return SCO_FAILED;
    return Wire(sc_ipc_peer_age_ms(&channel, now, age));
}
sco_result Write(sco_plugin*, uint64_t, uint64_t offset, const void* bytes, uint32_t size) {
    return Wire(sc_ipc_block_write(&channel, offset, bytes, size));
}
sco_result Read(sco_plugin*, uint64_t, uint64_t offset, void* bytes, uint32_t size) {
    return Wire(sc_ipc_block_read(&channel, offset, bytes, size));
}
sco_result RingInit(sco_plugin*, uint64_t, uint64_t offset, uint64_t capacity, uint32_t direction) {
    return Wire(sc_ipc_ring_init(&channel, offset, capacity, direction, direction == SC_IPC_TO_PEER ? &outgoing : &incoming));
}
sco_result Push(sco_plugin*, uint64_t, uint64_t offset, uint32_t type, const void* bytes, uint32_t size) {
    assert(offset == VX_OFF_TO_PEER); return Wire(sc_ipc_ring_push(&outgoing, type, bytes, size));
}
sco_result Pop(sco_plugin*, uint64_t, uint64_t offset, uint32_t* type, void* bytes, uint32_t* size) {
    assert(offset == VX_OFF_FROM_PEER); return Wire(sc_ipc_ring_pop(&incoming, type, bytes, size));
}
sco_result Close(sco_plugin*, uint64_t) { assert(open); sc_ipc_close(&channel); open = false; ++closes; return SCO_OK; }
sco_ipc_v1 ipc = {sizeof(ipc), 0, Create, Age, Write, Read, RingInit, Push, Pop, nullptr, Close};
const void* ipcTable = &ipc;
int Player(double p[3], double q[4], uint64_t* zone) {
    const double pos[] = {10, 20, 30}, rot[] = {0, 0, 0, 1};
    std::memcpy(p, pos, sizeof(pos)); std::memcpy(q, rot, sizeof(rot)); *zone = playerZone; return 1;
}
int Name(uint64_t, char* name, uint32_t size) { std::snprintf(name, size, "synthetic_area"); return 1; }
sc_spatial_v1 spatial = {sizeof(spatial), Player, nullptr, nullptr, nullptr, nullptr, Name};
int Pose(uint64_t from, uint64_t to, const double*, const double*, double p[3], double q[4]) {
    assert(from == 8 && to == 7); ++conversions;
    const double pos[] = {11, 20, 30}, rot[] = {0, 0, 0.7071067811865475, 0.7071067811865475};
    std::memcpy(p, pos, sizeof(pos)); std::memcpy(q, rot, sizeof(rot)); return 1;
}
sc_game_spatial_v1 fullPose = {sizeof(fullPose), Pose, nullptr, nullptr, nullptr};
sco_result Spawn(sco_plugin* owner, const char*, uint64_t zone, const double p[3], const double q[4], uint64_t* id) {
    assert(owner == self && zone == 7); ++spawns;
    std::memcpy(lastPos, p, sizeof(lastPos)); std::memcpy(lastRot, q, sizeof(lastRot));
    *id = 100 + static_cast<uint64_t>(spawns); owned[*id] = true; return SCO_OK;
}
sco_result Despawn(sco_plugin* owner, uint64_t id) {
    assert(owner == self && owned.count(id));
    if (removalFail) return SCO_UNAVAILABLE;
    owned.erase(id); ++removals; return SCO_OK; // initiation, not observed disappearance
}
sc_entities_v1 entities = {};
sco_result Ray(uint64_t zone, const double p[3], const double dir[3], double distance, sc_world_hit* hit) {
    assert(zone == 7 && dir[2] < 0 && distance > 0 && hit->size == sizeof(*hit)); ++rays;
    if (rayMissing) return SCO_UNAVAILABLE;
    std::memcpy(hit->pos, p, sizeof(hit->pos)); hit->pos[2] = 30; return SCO_OK;
}
sc_world_v1 world = {sizeof(world), 0, Ray, nullptr, nullptr};
sco_widget_event_fn panelEvent = nullptr;
std::vector<std::string> panelLabels;
int panelsRemoved = 0, keysBound = 0, keysRemoved = 0;
sco_result SetPanel(sco_plugin* owner, const char* id, const char* title, const sco_widget_node* nodes,
                    uint32_t count, uint32_t stride, sco_widget_event_fn fn, void*) {
    assert(owner == self && !std::strcmp(id, "voxel_bridge.voxel") && !std::strcmp(title, "Voxel"));
    assert(stride == sizeof(*nodes)); panelLabels.clear();
    for (uint32_t i = 0; i < count; ++i) { assert(nodes[i].size == sizeof(nodes[i])); panelLabels.emplace_back(nodes[i].label); }
    panelEvent = fn; return SCO_OK;
}
sco_result RemovePanel(sco_plugin* owner, const char* id) {
    assert(owner == self && !std::strcmp(id, "voxel_bridge.voxel")); panelEvent = nullptr; panelLabels.clear(); ++panelsRemoved; return SCO_OK;
}
sco_widgets_v1 widgets = {sizeof(widgets), 0, SetPanel, RemovePanel};
sco_result Bind(sco_plugin* owner, const char* chord, const char* command, const sco_arg*, uint32_t count) {
    assert(owner == self && !std::strcmp(chord,"ctrl+f9") && !std::strcmp(command,"voxel_bridge.toggle") && count == 0); ++keysBound; return SCO_OK;
}
sco_result Unbind(sco_plugin*, const char*) { ++keysRemoved; return SCO_OK; }
sco_ui_v1 ui = {};
sco_result Query(const char* name, uint32_t, const void** out) {
    *out = nullptr; if (!providers) return SCO_NOT_FOUND;
    if (!std::strcmp(name, SCO_IPC_NAME)) *out = ipcTable;
    else if (!std::strcmp(name, SC_SPATIAL_SERVICE_NAME)) *out = &spatial;
    else if (!std::strcmp(name, SC_GAME_SPATIAL_NAME)) *out = &fullPose;
    else if (!std::strcmp(name, SC_ENTITIES_NAME)) *out = &entities;
    else if (!std::strcmp(name, SC_WORLD_NAME)) *out = &world;
    else if (!std::strcmp(name, SCO_WIDGETS_NAME)) *out = &widgets;
    else if (!std::strcmp(name, SCO_UI_NAME)) *out = &ui;
    return *out ? SCO_OK : SCO_NOT_FOUND;
}
void Tick() { assert(tick); tick("tick", nullptr, nullptr); }
void Attach() {
    assert(sc_ipc_attach(mapping, sizeof(mapping), VX_LAYOUT_ID, VX_LAYOUT_VERSION + 1, &peer) == SC_IPC_MISMATCH);
    assert(sc_ipc_attach(mapping, sizeof(mapping), VX_LAYOUT_ID, VX_LAYOUT_VERSION, &peer) == SC_IPC_OK);
    sc_ipc_peer_beat(&peer, 2, now);
    vx_peer_state state = {}; state.flags = VX_PEER_IN_WORLD;
    assert(sc_ipc_block_write(&peer, VX_OFF_PEER_STATE, &state, sizeof(state)) == SC_IPC_OK);
}
vx_sc_state State() { vx_sc_state state = {}; assert(sc_ipc_block_read(&peer, VX_OFF_SC_STATE, &state, sizeof(state)) == SC_IPC_OK); return state; }
void Solids(uint32_t epoch, bool solid = true) {
    vx_msg_solids msg = {}; msg.sy = 4; msg.epoch = epoch; msg.bits[0] = solid ? 1 : 0;
    assert(sc_ipc_ring_push(&incoming, VX_MSG_SOLIDS, &msg, sizeof(msg)) == SC_IPC_OK);
}
sco_result Invoke(const char* name, std::string& reply) {
    char buffer[512] = {}; auto it = commands.find(name); assert(it != commands.end());
    sco_result result = it->second.fn(nullptr, 0, nullptr, buffer, sizeof(buffer));
    reply = buffer; return result;
}
}
int main(int argc, char** argv) {
    test_module::Open(argc, argv);
    assert(!std::strcmp(test_module::Query()->name, "voxel_bridge"));
    sco_api api = {}; api.size = sizeof(api); api.major = SCO_API_MAJOR; api.minor = SCO_API_MINOR;
    api.register_command = Register; api.subscribe = Subscribe; api.unsubscribe = Unsubscribe;
    api.log = Log; api.query_service = Query;
    assert(test_module::Load(&api, self) == SCO_OK);
    assert(commands.size() == 3);
    std::string reply;
    assert(Invoke("voxel_bridge.toggle", reply) == SCO_UNAVAILABLE);
    assert(reply.find("unavailable") != std::string::npos);
    assert(Invoke("voxel_bridge.status", reply) == SCO_OK);
    assert(reply.find("G2") != std::string::npos);
    test_module::Unload(); test_module::Unload();
    assert(!tick);
    std::puts("voxel_bridge module: absent providers, preserved IDs, G2 refusal, unload twice PASS");
    commands.clear(); providers = true;
    entities.size = sizeof(entities); entities.spawn = Spawn; entities.despawn = Despawn;
    ui.size = sizeof(ui); ui.bind_hotkey = Bind; ui.unbind_hotkey = Unbind;
    assert(test_module::Load(&api, self) == SCO_OK);
    assert(panelEvent && !panelLabels.empty() && keysBound == 1);
    sco_widget_event button = {}; button.size = sizeof(button); button.kind = SCO_WIDGET_BUTTON; button.node_id = 1;
    panelEvent("voxel_bridge.voxel", &button, nullptr);
    assert(open);
    Attach(); Tick();
    vx_sc_state state = State(); assert(state.epoch && (state.flags & VX_SC_ANCHORED));
    Solids(state.epoch); Tick();
    assert(spawns == 1 && owned.size() == 1);
    assert(lastPos[0] == 10.25 && lastPos[1] == 19.75 && lastPos[2] == 30 && lastRot[3] == 1);
    assert(rays == 128);
    // Decode actual outbound payload with the public wire, not only provider counters.
    uint8_t ground[sizeof(vx_msg_ground) + 64 * sizeof(vx_ground)] = {};
    uint32_t groundType = 0, groundSize = sizeof(ground); bool foundGround = false;
    while (sc_ipc_ring_pop(&outgoing, &groundType, ground, &groundSize) == SC_IPC_OK) {
        if (groundType == VX_MSG_GROUND) {
            vx_msg_ground header; std::memcpy(&header, ground, sizeof(header));
            assert(header.epoch == state.epoch && header.count == 64);
            assert(groundSize == sizeof(header) + 64 * sizeof(vx_ground));
            vx_ground column; std::memcpy(&column, ground + sizeof(header), sizeof(column));
            assert(column.top == VX_BASE_Y); foundGround = true;
        }
        groundSize = sizeof(ground);
    }
    assert(foundGround);
    const char peerLine[] = "synthetic\npeer";
    assert(sc_ipc_ring_push(&incoming, VX_MSG_LOG, peerLine, sizeof(peerLine)) == SC_IPC_OK);
    Tick(); assert(lastLog == "synthetic?peer");
    assert(Invoke("voxel_bridge.toggle", reply) == SCO_OK);
    assert(owned.empty() && closes == 1 && sc_ipc_check(&peer) == SC_IPC_GONE);
    test_module::Unload();
    std::puts("voxel_bridge module: real wire handshake, area, owned crate placement, ground scan, close cleanup PASS");
    // Re-open/reload isolates state; actual quaternion conversion crosses the nested frame.
    playerZone = 7;
    assert(test_module::Load(&api, self) == SCO_OK);
    assert(Invoke("voxel_bridge.toggle", reply) == SCO_OK);
    Attach(); Tick(); state = State();
    playerZone = 8; Tick(); state = State();
    assert(conversions == 1 && state.feet[0] == 2 && state.feet[1] == VX_BASE_Y);
    assert(std::fabs(state.yaw - 90) < 0.001 && !(state.flags & VX_SC_PAUSED));
    auto savedPose = fullPose.pose_to_zone; fullPose.pose_to_zone = nullptr;
    Tick(); state = State(); assert(state.flags & VX_SC_PAUSED);
    for (int i = 0; i < 6; ++i) { Solids(state.epoch); Solids(state.epoch, false); }
    Tick();
    assert(Invoke("voxel_bridge.status", reply) == SCO_OK && reply.find("0 to spawn") != std::string::npos);
    fullPose.pose_to_zone = savedPose; playerZone = 7;
    Solids(state.epoch); Tick(); assert(owned.size() == 1);
    // Wrong-sized frame, unknown type, old epoch and absurd section are ignored.
    uint32_t nonsense = 0;
    assert(sc_ipc_ring_push(&incoming, VX_MSG_SOLIDS, &nonsense, sizeof(nonsense)) == SC_IPC_OK);
    assert(sc_ipc_ring_push(&incoming, 99, &nonsense, sizeof(nonsense)) == SC_IPC_OK);
    Solids(state.epoch + 1);
    vx_msg_solids bad = {}; bad.epoch = state.epoch; bad.sx = INT32_MAX; bad.bits[0] = 1;
    assert(sc_ipc_ring_push(&incoming, VX_MSG_SOLIDS, &bad, sizeof(bad)) == SC_IPC_OK);
    Tick(); assert(owned.size() == 1);
    // Heartbeat exact boundary: 7999 still live, 8000 closes and initiates cleanup.
    now += 7999; Tick(); assert(open && owned.size() == 1);
    ++now; Tick(); assert(!open && owned.empty());
    assert(Invoke("voxel_bridge.status", reply) == SCO_OK && reply.find("stale") != std::string::npos);
    assert(Invoke("voxel_bridge.toggle", reply) == SCO_OK); Attach(); Tick();
    state = State(); Solids(state.epoch); Tick(); assert(owned.size() == 1);
    // Corrupt raw ring counters: do not dereference/consume hostile payload; cleanup.
    incoming.ring->head = incoming.ring->capacity + incoming.ring->tail + 1;
    Tick(); assert(!open && owned.empty());
    assert(Invoke("voxel_bridge.status", reply) == SCO_OK && reply.find("malformed") != std::string::npos);
    assert(Invoke("voxel_bridge.toggle", reply) == SCO_OK); Attach(); Tick();
    state = State(); Solids(state.epoch); Tick(); assert(owned.size() == 1);
    sc_ipc_close(&peer); Tick(); assert(!open && owned.empty());
    // Removal rejection keeps bookkeeping until retry succeeds; no alive-based deletion.
    assert(Invoke("voxel_bridge.toggle", reply) == SCO_OK); Attach(); Tick();
    state = State(); Solids(state.epoch); Tick(); assert(owned.size() == 1);
    removalFail = true;
    Solids(state.epoch, false); Tick(); assert(owned.size() == 1);
    assert(Invoke("voxel_bridge.toggle", reply) == SCO_OK);
    assert(Invoke("voxel_bridge.toggle", reply) == SCO_UNAVAILABLE && reply.find("cleanup") != std::string::npos);
    removalFail = false; Tick(); assert(owned.empty());
    assert(Invoke("voxel_bridge.toggle", reply) == SCO_OK); Attach(); Tick();
    state = State(); Solids(state.epoch); Tick(); assert(owned.size() == 1);
    auto stalePanelEvent = panelEvent;
    test_module::Unload(); test_module::Unload(); assert(!open && owned.empty() && !tick);
    int oldCreates = creates; stalePanelEvent("voxel_bridge.voxel", &button, nullptr); assert(creates == oldCreates);
    assert(creates == closes);
    assert(!panelEvent && keysBound == keysRemoved && panelsRemoved > 0);
    std::puts("voxel_bridge module: full-pose quaternion, malformed frames/ring, heartbeat 7999/8000, closed peer, retry cleanup, reload PASS");
    // Physically partial close callback is never read, full-sized NULL also refused.
    const auto savedIpc = ipc;
    const size_t partialSize = offsetof(sco_ipc_v1, close) + sizeof(ipc.close) - 1;
    void* partial = std::malloc(partialSize); assert(partial);
    std::memcpy(partial, &ipc, partialSize);
    *static_cast<uint32_t*>(partial) = static_cast<uint32_t>(partialSize);
    ipcTable = partial;
    assert(test_module::Load(&api, self) == SCO_OK);
    assert(Invoke("voxel_bridge.toggle", reply) == SCO_UNAVAILABLE); test_module::Unload();
    std::free(partial); ipcTable = &ipc;
    ipc = savedIpc; ipc.close = nullptr;
    assert(test_module::Load(&api, self) == SCO_OK);
    assert(Invoke("voxel_bridge.toggle", reply) == SCO_UNAVAILABLE); test_module::Unload();
    ipc = savedIpc;
    assert(keysBound == keysRemoved && !panelEvent);
    std::puts("voxel_bridge module: partial callback extent, full-sized null callback, panel/button/hotkey cleanup PASS");
    test_module::Close();
}
