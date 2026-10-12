#include "teleport.h"
#include "menu.h"
#include "builtins/builtin_store.h"
#include "sco/game/teleport.h"
#include "sco/signatures.h"
#include <cmath>
#include <share.h>

TeleportApi g_tp;

// The addresses are sco-core's teleport.* signature rows (src/game/teleport_sigs.cpp: the scan
// and checks this file used to do, moved byte for byte), resolved by ResolveAll in StartOffline.
bool ResolveTeleportApi() {
    sco::game::TeleportAddrs a;
    if (!sco::game::TeleportAddresses(a)) {
        // to_camera holds the checks; the other rows only fail when it does.
        const sco::SigResult* r = sco::SigLookup("teleport.to_camera");
        Log("[tp] TeleportToCamera not found (teleport.to_camera %s: %s); teleport disabled",
            r ? sco::SigStateName(r->state) : "unknown", r && r->why ? r->why : "see [core] below");
        return false;
    }
    g_tp.clientMgr    = a.clientMgr;
    g_tp.handleFromId = a.handleFromId;
    g_tp.entitySystem = a.entitySystem;
    g_tp.ok = true;
    return true;
}

constexpr double kMaxLocalCoord = 1.0e12;

// Returned when the only zone a spot shares with you is the universe root: the spot is in another
// star system, which isn't loaded, so teleporting there leaves you floating in nothing.
static const char* const kOtherSystem = "that spot is in another star system - travel to that system first";

static bool PositionLooksValid(const double p[3]) {
    for (int i = 0; i < 3; ++i)
        if (!(p[i] > -kMaxLocalCoord && p[i] < kMaxLocalCoord)) return false;
    return true;
}

bool GetLocalPlayer(uintptr_t& actor, uintptr_t& entity) {
    const uintptr_t mgr = *g_tp.clientMgr;
    if (!mgr) return false;
    const uintptr_t sub = Rd<uintptr_t>(mgr + 0xE0);
    if (!sub) return false;
    const uintptr_t info = VCall<uintptr_t>(sub, 0x2E0);
    if (!info) return false;
    uint64_t handle = 0;
    reinterpret_cast<void(__fastcall*)(uint64_t*, uint64_t)>(g_tp.handleFromId)(&handle, Rd<uint64_t>(info + 8));
    actor = handle & kPtrMask;
    if (!actor) return false;
    const uintptr_t life = VCall<uintptr_t>(actor, 0xA08);
    if (!life || !VCall<uintptr_t>(life, 0x28)) return false;
    entity = Rd<uintptr_t>(actor + 8) & kPtrMask;
    return entity != 0;
}

uintptr_t ZoneParent(uintptr_t zone) { return VCall<uintptr_t>(zone, 0x08); }
const char* ZoneName(uintptr_t zone) { return VCall<const char*>(zone, 0x218); }

uint64_t ZoneId(uintptr_t zone) {
    uint8_t tmp[16] = {};
    const uint64_t* id = VCall<const uint64_t*>(zone, 0x58, tmp);
    return id ? *id : 0;
}

uintptr_t ZoneFromId(uint64_t zoneId) {
    const uintptr_t es = *g_tp.entitySystem;
    if (!es || !zoneId) return 0;
    const uintptr_t ent = VCall<uintptr_t>(es, 0x120, zoneId);
    return ent ? VCall<uintptr_t>(ent, 0x6E0) : 0;
}

void Vec3Out(uintptr_t obj, size_t off, double out[3]) {
    double buf[4] = {};
    const double* r = VCall<const double*>(obj, off, buf, uintptr_t(0));
    out[0] = r[0]; out[1] = r[1]; out[2] = r[2];
}

void LocalToWorld(uintptr_t zone, const double local[3], double world[3]) {
    double buf[4] = {};
    const double* r = VCall<const double*>(zone, 0x198, buf, local);
    world[0] = r[0]; world[1] = r[1]; world[2] = r[2];
}

static double Dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// A zone's frame in the world: its origin and the world directions of its x, y and z axes, probed
// through LocalToWorld. False when the axes aren't orthonormal (a frame this code can't invert).
static bool ZoneAxes(uintptr_t zone, double origin[3], double axis[3][3]) {
    constexpr double kArm = 1.0e6;
    const double zero[3] = {};
    LocalToWorld(zone, zero, origin);
    for (int i = 0; i < 3; ++i) {
        double probe[3] = {}, w[3];
        probe[i] = kArm;
        LocalToWorld(zone, probe, w);
        for (int k = 0; k < 3; ++k) axis[i][k] = (w[k] - origin[k]) / kArm;
    }
    for (int i = 0; i < 3; ++i) {
        if (fabs(Dot(axis[i], axis[i]) - 1.0) > 1e-6) return false;
        for (int j = i + 1; j < 3; ++j)
            if (fabs(Dot(axis[i], axis[j])) > 1e-6) return false;
    }
    return true;
}

bool WorldToLocal(uintptr_t zone, const double world[3], double local[3]) {
    double origin[3], axis[3][3];
    if (!ZoneAxes(zone, origin, axis)) return false;
    const double d[3] = { world[0] - origin[0], world[1] - origin[1], world[2] - origin[2] };
    for (int i = 0; i < 3; ++i) local[i] = Dot(axis[i], d);
    return PositionLooksValid(local);
}

int ReadZoneChain(uintptr_t zone, ZoneFrame* out, int max) {
    int n = 0;
    // steps bounds the walk on a parent cycle, since skipped zones don't count towards max.
    int steps = 0;
    for (uintptr_t z = zone; z && n < max && steps < 64; z = ZoneParent(z), ++steps) {
        const uint64_t id = ZoneId(z);
        if (!id || ZoneFromId(id) != z) continue;   // can't be named by id, so can't be queried
        ZoneFrame& f = out[n];
        if (!ZoneAxes(z, f.origin, f.axis)) continue;
        f.id = id;
        const char* name = ZoneName(z);
        strncpy_s(f.name, name ? name : "", _TRUNCATE);
        ++n;
    }
    return n;
}

static const char* CaptureSpot(Spot& s, double world[3]) {
    __try {
        uintptr_t actor, entity;
        if (!GetLocalPlayer(actor, entity)) return "player not spawned";
        const uintptr_t zone = VCall<uintptr_t>(entity, 0x6B8);
        if (!zone) return "not in a zone";
        double local[3];
        Vec3Out(entity, 0x2B8, local);
        if (!PositionLooksValid(local)) return "position out of range";
        LocalToWorld(zone, local, world);
        s.n = 0;
        for (uintptr_t z = zone; z && s.n < kMaxZoneDepth; z = ZoneParent(z)) {
            const char* name = ZoneName(z);
            if (!name || !*name) continue;
            ZoneSpot& e = s.z[s.n];
            if (z == zone) memcpy(e.local, local, sizeof(local));
            else if (!WorldToLocal(z, world, e.local)) continue;
            strncpy_s(e.name, name, _TRUNCATE);
            ++s.n;
        }
        return s.n ? nullptr : "zone has no name";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "fault while reading player";
    }
}

static int FindSavedZone(const Spot& s, uintptr_t entity, uintptr_t& zoneOut) {
    uintptr_t chain[kMaxZoneDepth];
    const char* names[kMaxZoneDepth];
    int n = 0;
    for (uintptr_t z = VCall<uintptr_t>(entity, 0x6B8); z && n < kMaxZoneDepth; z = ZoneParent(z)) {
        chain[n] = z;
        names[n++] = ZoneName(z);
    }
    for (int i = 0; i < s.n; ++i)
        for (int k = 0; k < n; ++k)
            if (names[k] && strcmp(names[k], s.z[i].name) == 0) { zoneOut = chain[k]; return i; }
    return -1;
}

static int SavedZoneLevel(const Spot& s) {
    __try {
        uintptr_t actor, entity, zone;
        return GetLocalPlayer(actor, entity) ? FindSavedZone(s, entity, zone) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

static const char* TeleportToSpot(const Spot& s, int& level) {
    (void)s; level = -1; return "Player relocation unavailable: G2 mutation is not approved; no private fallback";
}

const char* TeleportToEntity(uint64_t entityId, double up) {
    (void)entityId; (void)up; return "Player relocation unavailable: G2 mutation is not approved; no private fallback";
}

static bool SpotFilePath(char* path, DWORD n) {
    const DWORD len = GetEnvironmentVariableA("SC_OFFLINE_SPAWN_FILE", path, n);
    return len > 0 && len < n;
}

// One "spot x y z zone" line into s (when it parses, fits and, with checkPosition, is in range).
static void AddSpotLine(const char* line, Spot& s, bool checkPosition) {
    if (s.n >= kMaxZoneDepth) return;
    ZoneSpot& e = s.z[s.n];
    if (sscanf_s(line, "spot %lf %lf %lf %95[^\r\n]", &e.local[0], &e.local[1], &e.local[2],  // secret-scan: allow e.local is a struct member, not a hostname
                 e.name, static_cast<unsigned>(sizeof(e.name))) == 4 && (!checkPosition || PositionLooksValid(e.local)))
        ++s.n;
}

void SpotToText(const Spot& s, std::string& out) {
    out.clear();
    char line[256];
    for (int i = 0; i < s.n; ++i) {
        snprintf(line, sizeof(line), "spot %.6f %.6f %.6f %s\n", s.z[i].local[0], s.z[i].local[1], s.z[i].local[2], s.z[i].name);
        out += line;
    }
}

int SpotFromText(const char* text, Spot& s, bool checkPosition) {
    s = Spot{};
    char line[256];
    while (*text) {
        const size_t len = strcspn(text, "\n");
        const size_t n = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, text, n);
        line[n] = 0;
        AddSpotLine(line, s, checkPosition);
        text += len + (text[len] ? 1 : 0);
    }
    return s.n;
}

static bool LoadSpot(Spot& s, bool& oldFormat) {
    s.n = 0;
    oldFormat = false;
    char path[MAX_PATH];
    if (!SpotFilePath(path, sizeof(path))) return false;
    FILE* f = _fsopen(path, "r", _SH_DENYNO);
    if (!f) return false;
    char line[256];
    while (s.n < kMaxZoneDepth && fgets(line, sizeof(line), f)) {
        if (strncmp(line, "zone=", 5) == 0) { oldFormat = true; continue; }
        AddSpotLine(line, s, true);
    }
    fclose(f);
    return s.n > 0;
}

static bool SaveSpot(const Spot& s) {
    char path[MAX_PATH];
    if (!SpotFilePath(path, sizeof(path))) return false;
    FILE* f = _fsopen(path, "w", _SH_DENYNO);
    if (!f) return false;
    std::string text;
    SpotToText(s, text);
    fprintf(f, "# starcitzenofflinemods spawn spot (F7). Position in each zone, innermost first.\n%s", text.c_str());
    fclose(f);
    return true;
}

// The spot in the teleport built-in's storage (data/storage/teleport.db): key "spot", spawn.txt's
// lines as text. "spawn.txt.time" is spawn.txt's last-write time when it was imported
// (builtins/builtin_store.h has the rule). spawn.txt is no longer written while storage works.
constexpr const char* kSpotKey = "spot";
constexpr const char* kSpotTimeKey = "spawn.txt.time";

// Both keys in one transaction; fileTime 0 leaves the recorded time as it is.
static bool StoreSpot(const Spot& s, int64_t fileTime) {
    BuiltinStore& st = g_teleportStore;
    std::string text;
    SpotToText(s, text);
    sco::sdk::StorageTransaction tx(st.S());
    sco_result r = tx.Result();
    if (r == SCO_OK) r = st.S().Put(kSpotKey, text);
    if (r == SCO_OK && fileTime) r = st.PutInt(kSpotTimeKey, fileTime);
    if (r == SCO_OK) r = tx.Commit();
    if (r != SCO_OK) st.Failed("saving the spot", r, "using spawn.txt");
    return r == SCO_OK;
}

static void DescribeChain(const Spot& s, char* out, size_t n) {
    out[0] = 0;
    for (int i = 0; i < s.n; ++i) {
        if (i) strncat_s(out, n, " > ", _TRUNCATE);
        strncat_s(out, n, s.z[i].name, _TRUNCATE);
    }
}

static Spot  g_spot;
static bool  g_autoTeleportPending = false;
static DWORD g_playerReadySince = 0;

static Spot  g_refineSpot;     // the spot being refined (F8's or a bookmark's)
static int   g_refineLevel = 0;
static DWORD g_refineSince = 0;

static void StartRefine(const Spot& spot, int level, DWORD now, const char* why) {
    if (level == 0) { Log("[tp] %s -> teleported to saved spot in '%s'", why, spot.z[0].name); g_refineLevel = 0; return; }
    Log("[tp] %s -> teleported via '%s' (waiting for '%s' to stream in)", why, spot.z[level].name, spot.z[0].name);
    g_refineSpot = spot;
    g_refineLevel = level;
    g_refineSince = now;
}

const char* GoToSpot(const Spot& s, DWORD now, const char* why) {
    int level = -1;
    if (const char* err = TeleportToSpot(s, level)) return err;
    StartRefine(s, level, now, why);
    return nullptr;
}

const char* CaptureCurrentSpot(Spot& s) {
    double world[3] = {};
    return CaptureSpot(s, world);
}

bool SaveSpotHere(const char* why, char* reply, size_t n) {
    (void)why; snprintf(reply, n, "Quick save unavailable: G2 consumer is held in REFUSE mode"); return false;
}

bool GoToSavedSpot(const char* why, char* reply, size_t n) {
    (void)why; snprintf(reply, n, "Player relocation unavailable: G2 mutation is not approved; no private fallback"); return false;
}

static void SystemFromZoneName(const char* name, char* out, size_t n) {
    // OOC_Stanton_2b_Daymar -> Stanton
    if (!name || _strnicmp(name, "OOC_", 4) != 0) return;
    const char* sys = name + 4;
    const size_t len = strcspn(sys, "_");
    if (len && len < n) { memcpy(out, sys, len); out[len] = 0; }
}

void SpotSystemName(const Spot& s, char* out, size_t n) {
    out[0] = 0;
    for (int i = 0; i < s.n && !out[0]; ++i) SystemFromZoneName(s.z[i].name, out, n);
}

void CurrentSystemName(char* out, size_t n) {
    out[0] = 0;
    __try {
        uintptr_t actor, entity;
        if (!GetLocalPlayer(actor, entity)) return;
        for (uintptr_t z = VCall<uintptr_t>(entity, 0x6B8); z && !out[0]; z = ZoneParent(z))
            SystemFromZoneName(ZoneName(z), out, n);
    } __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

void CurrentSystemZoneName(char* out, size_t n) {
    out[0] = 0;
    __try {
        uintptr_t actor, entity;
        if (!GetLocalPlayer(actor, entity)) return;
        if (const uintptr_t sys = SystemZoneOf(VCall<uintptr_t>(entity, 0x6B8)))
            if (const char* name = ZoneName(sys)) strncpy_s(out, n, name, _TRUNCATE);
    } __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

// The "SolarSystem_*" zone a zone belongs to (or 0 for the root / unknown).
uintptr_t SystemZoneOf(uintptr_t zone) {
    for (uintptr_t z = zone; z; z = ZoneParent(z)) {
        const char* name = ZoneName(z);
        if (name && _strnicmp(name, "SolarSystem", 11) == 0) return z;
    }
    return 0;
}

const char* TeleportIntoZone(uintptr_t zone, const double local[3]) {
    (void)zone; (void)local; return "Player relocation unavailable: G2 mutation is not approved; no private fallback";
}

static bool g_spotLoadPending = false;   // LoadSavedSpot ran; the first TeleportTick loads the spot
static bool g_startingOverDaymar = false;

// Called before the host kit starts, so the built-in's storage isn't open yet: the spot is loaded
// on the first TeleportTick, after the teleport built-in has loaded (or failed to).
void LoadSavedSpot(bool startingOverDaymar) {
    g_startingOverDaymar = startingOverDaymar;
    g_spotLoadPending = true;
}

// spawn.txt, then storage: a spawn.txt storage hasn't seen is imported; otherwise storage's spot
// is the saved one. Without storage, spawn.txt as before.
static void LoadSpotNow() {
    bool oldFormat = false;
    Spot file;
    const bool haveFile = LoadSpot(file, oldFormat);
    g_spot = file;
    const char* from = "spawn.txt";
    BuiltinStore& st = g_teleportStore;
    if (st) {
        char path[MAX_PATH];
        const int64_t fileTime = SpotFilePath(path, sizeof(path)) ? FileWriteTime(path) : 0;
        int64_t seen = 0;
        const sco_result rs = st.GetInt(kSpotTimeKey, seen);
        if (haveFile && (rs != SCO_OK || seen != fileTime)) {
            if (StoreSpot(file, fileTime))
                Log("[storage] imported spawn.txt into teleport%s", rs == SCO_OK ? " (spawn.txt changed since the last import)" : "");
        } else {
            std::string text;
            const sco_result r = st.S().Get(kSpotKey, text);
            Spot stored;
            if (r == SCO_OK && SpotFromText(text.c_str(), stored, true)) { g_spot = stored; from = "storage"; }
            else if (r != SCO_OK && r != SCO_NOT_FOUND) st.Failed("reading the spot", r, "using spawn.txt");
        }
    }
    const bool startingOverDaymar = g_startingOverDaymar;
    if (g_spot.n) {
        g_autoTeleportPending = true;
        char chain[512];
        DescribeChain(g_spot, chain, sizeof(chain));
        Log("[tp] saved spot loaded from %s ('%s'; zones: %s); will teleport there after spawning. F7 = save new spot, F8 = go there now",
            from, g_spot.z[0].name, chain);
    } else if (oldFormat) {
        Log("[tp] saved spot is from an older version and can't be used after a relaunch: stand there and press F7 once more");
    } else {
        Log("[tp] no saved spot yet: stand where you want to spawn and press F7 (F8 = go there now)");
    }
    if (startingOverDaymar && g_autoTeleportPending) {
        g_autoTeleportPending = false;
        Log("[tp] starting over Daymar instead of the saved spot (F8 still goes there)");
    }
}

void TeleportTick(DWORD now) {
    (void)now; // External teleport owns read/state commands; no host fallback or auto-relocation.
}
