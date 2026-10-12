// SPDX-License-Identifier: GPL-3.0-only
// Retained ONLY for legacy NPC/contracts/optional bridge callers not yet migrated.
// The build plugin never links or calls this file; no aim/freecam/spawn/removal fallback.
#include "build.h"
#include "spawner.h"
#include "teleport.h"
#include "world_caps.h"
#include <sco/game/world.h>
#include <sco/signatures.h>
#include <cmath>

using ReleaseGridFn = void(__fastcall*)(uintptr_t grid);
static uintptr_t*    g_physWorld = nullptr;
static ReleaseGridFn g_releaseGrid = nullptr;
static const char*   g_rayTag = nullptr;

// What the entity vtable's slots hold (sco-core's build.entity_* rows); a live entity's slots are
// compared with them before build mode calls through them.
static uintptr_t g_entitySetPosition = 0, g_entitySetRotation = 0, g_entityGetRotation = 0;
static uintptr_t g_entityRayProxy = 0, g_entitySkipAdd = 0;

static uintptr_t SigAddr(const char* id) { return reinterpret_cast<uintptr_t>(sco::Sig(id)); }

void ResolveLegacyPlacementApi(const Section&, const Section&) {
    if (WorldCapability("build.ground_ray")) {
        g_rayTag = reinterpret_cast<const char*>(sco::Sig("build.ray_tag"));
        g_physWorld = reinterpret_cast<uintptr_t*>(sco::Sig("build.phys_world"));
        g_releaseGrid = reinterpret_cast<ReleaseGridFn>(sco::Sig("build.release_grid"));
        g_entityRayProxy = SigAddr("build.entity_ray_proxy");
        g_entitySkipAdd = SigAddr("build.entity_skip_add");
    }
    if (WorldCapability("build.entity_move")) {
        g_entitySetPosition = SigAddr("build.entity_set_position");
        g_entitySetRotation = SigAddr("build.entity_set_rotation");
        g_entityGetRotation = SigAddr("build.entity_get_rotation");
    }
}

static int g_slotsOk = -1;

static bool EntitySlotsOk(uintptr_t entity) {
    if (g_slotsOk < 0) {
        const uintptr_t vt = Rd<uintptr_t>(entity);
        g_slotsOk = g_entitySetPosition
            && Rd<uintptr_t>(vt + sco::game::world::kEntitySetPositionSlot) == g_entitySetPosition
            && Rd<uintptr_t>(vt + sco::game::world::kEntitySetRotationSlot) == g_entitySetRotation
            && Rd<uintptr_t>(vt + sco::game::world::kEntityGetRotationSlot) == g_entityGetRotation;
        if (!g_slotsOk) Log("[build] entity move/rotate functions changed; the preview won't follow the camera");
    }
    return g_slotsOk > 0;
}

bool EntityRotation(uintptr_t entity, double rot[4]) {
    if (!EntitySlotsOk(entity)) return false;
    double buf[4] = {};
    const double* r = VCall<const double*>(entity, 0x2C8, buf, static_cast<uint8_t>(0));
    if (!r) return false;
    memcpy(rot, r, 4 * sizeof(double));
    return true;
}

static uintptr_t EntityById(uint64_t id) { return id ? VCall<uintptr_t>(*g_tp.entitySystem, 0x120, id) : 0; }

static void QuatMul(const double a[4], const double b[4], double r[4]) {
    r[0] = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
    r[1] = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
    r[2] = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
    r[3] = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
}

static int g_rayOk = -1;

static bool RaySlotsOk(uintptr_t entity) {
    if (g_rayOk < 0) {
        const uintptr_t vt = Rd<uintptr_t>(entity);
        g_rayOk = g_physWorld
            && Rd<uintptr_t>(vt + sco::game::world::kEntityRayProxySlot) == g_entityRayProxy
            && Rd<uintptr_t>(vt + sco::game::world::kEntitySkipAddSlot) == g_entitySkipAdd;
        if (!g_rayOk && g_physWorld) Log("[build] ground ray slots changed; objects go where the camera points instead of on the ground");
    }
    return g_rayOk > 0;
}

struct PhysSkipList {
    uint64_t slots[8];
    uint64_t heap;
    int32_t  count, capacity;
    int64_t  lock;
    int32_t  owner;
    uint8_t  depth, pad[3];
};
static_assert(sizeof(PhysSkipList) == 0x60, "PhysSkipList layout");
static PhysSkipList g_skip = { {}, 0, 0, 8, 0, -1, 0, {} };

static double CastInZone(uintptr_t zone, const double from[3], const double to[3]) {
    double o[3], e[3];
    if (!WorldToLocal(zone, from, o) || !WorldToLocal(zone, to, e)) return -1;
    const double d[3] = { e[0] - o[0], e[1] - o[1], e[2] - o[2] };
    const double len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (len < 0.01) return -1;
    uintptr_t ref = 0;
    const uintptr_t* grid = VCall<const uintptr_t*>(zone, 0x30, &ref);
    alignas(16) uint8_t rp[0xA8] = {};
    alignas(16) uint8_t hits[0x60 * 2] = {};
    memcpy(rp + 0x18, o, sizeof(o));
    for (int i = 0; i < 3; ++i) reinterpret_cast<float*>(rp + 0x30)[i] = static_cast<float>(d[i]);
    *reinterpret_cast<int32_t*>(rp + 0x3C)     = 0x101;
    *reinterpret_cast<uint64_t*>(rp + 0x40)    = 0x20F;
    *reinterpret_cast<void**>(rp + 0x48)       = hits;
    *reinterpret_cast<int32_t*>(rp + 0x50)     = 1;
    *reinterpret_cast<void**>(rp + 0x70)       = &g_skip;
    *reinterpret_cast<uintptr_t*>(rp + 0x80)   = grid ? *grid : 0;
    *reinterpret_cast<const char**>(rp + 0x88) = g_rayTag;
    const int n = grid && *grid ? VCall<int>(*g_physWorld, 0x1C0, static_cast<void*>(rp), 30) : 0;
    if (ref) g_releaseGrid(ref);
    const float dist = *reinterpret_cast<const float*>(hits + 0x10);
    if (n <= 0 || !*reinterpret_cast<const uint64_t*>(hits) || !(dist >= 0.0f) || dist > len) return -1;
    return dist / len;
}

static bool RayHit(uintptr_t zone, const double from[3], const double to[3], double hit[3]) {
    for (int depth = 0; zone && depth < 3; ++depth, zone = ZoneParent(zone)) {
        if (!ZoneParent(zone)) break;
        const double t = CastInZone(zone, from, to);
        if (t < 0) continue;
        for (int i = 0; i < 3; ++i) hit[i] = from[i] + (to[i] - from[i]) * t;
        return true;
    }
    return false;
}

// Legacy callers only. Not a fallback accessible from the external build consumer.
static bool PlainRayHit(uintptr_t zone, const double from[3], const double to[3], double hit[3]) {
    uintptr_t actor, entity;
    if (!GetLocalPlayer(actor, entity) || !RaySlotsOk(entity)) return false;
    g_skip.count = 0;
    return RayHit(zone, from, to, hit);
}
bool GroundRay(uintptr_t zone, const double from[3], const double to[3], double hit[3]) {
    return zone && PlainRayHit(zone, from, to, hit);
}

bool PlaceNearPlayer(double ahead, double side, double lift, double pos[3], double rot[4]) {
    uintptr_t actor, entity;
    if (!GetLocalPlayer(actor, entity)) return false;
    const uintptr_t zone = VCall<uintptr_t>(entity, 0x6B8);
    if (!zone || !EntitySlotsOk(entity)) return false;
    double you[3], stand[4] = { 0, 0, 0, 1 }, buf[4] = {};
    Vec3Out(entity, 0x2B8, you);
    if (const double* r = VCall<const double*>(entity, 0x2C8, buf, static_cast<uint8_t>(0))) memcpy(stand, r, sizeof(stand));
    const double* q = stand;
    const double right[3] = { 1 - 2 * (q[1] * q[1] + q[2] * q[2]), 2 * (q[0] * q[1] + q[3] * q[2]), 2 * (q[0] * q[2] - q[3] * q[1]) };
    const double fwd[3]   = { 2 * (q[0] * q[1] - q[3] * q[2]), 1 - 2 * (q[0] * q[0] + q[2] * q[2]), 2 * (q[1] * q[2] + q[3] * q[0]) };
    const double up[3]    = { 2 * (q[0] * q[2] + q[3] * q[1]), 2 * (q[1] * q[2] - q[3] * q[0]), 1 - 2 * (q[0] * q[0] + q[1] * q[1]) };
    double chest[3], spot[3], chestW[3], spotW[3], upW[3], probe[3], probeW[3];
    for (int i = 0; i < 3; ++i) {
        chest[i] = you[i] + up[i] * 1.2;
        spot[i]  = chest[i] + fwd[i] * ahead + right[i] * side;
        probe[i] = you[i] + up[i];
    }
    LocalToWorld(zone, chest, chestW);
    LocalToWorld(zone, spot, spotW);
    LocalToWorld(zone, you, probeW);
    LocalToWorld(zone, probe, upW);
    for (int i = 0; i < 3; ++i) upW[i] -= probeW[i];

    double world[3];
    for (int i = 0; i < 3; ++i) world[i] = spotW[i] - upW[i] * 1.2;
    {
        double hit[3], down[3], floor[3];
        const double d[3] = { spotW[0] - chestW[0], spotW[1] - chestW[1], spotW[2] - chestW[2] };
        const double len = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len > 0.6 && PlainRayHit(zone, chestW, spotW, hit))
            for (int i = 0; i < 3; ++i) spotW[i] = hit[i] - d[i] / len * 0.5;
        for (int i = 0; i < 3; ++i) down[i] = spotW[i] - upW[i] * 50.0;
        if (PlainRayHit(zone, spotW, down, floor)) memcpy(world, floor, sizeof(world));
    }
    for (int i = 0; i < 3; ++i) world[i] += upW[i] * lift;
    if (!WorldToLocal(zone, world, pos)) return false;
    const double turn[4] = { 0, 0, 1, 0 };
    QuatMul(stand, turn, rot);
    return true;
}

// Moves (and with rot, turns) any entity within its zone, in local coordinates. Build mode's preview,
// NPCs that can't be removed (sent far away) and spawn.entities' set_entity_transform.
bool MoveEntityLocal(uint64_t id, const double pos[3], const double rot[4]) {
    __try {
        const uintptr_t e = EntityById(id);
        if (!e || !EntitySlotsOk(e)) return false;
        VCall<void>(e, 0x2B0, pos, 0, false);
        if (rot) VCall<void>(e, 0x2C0, rot, 0, false);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

