#include "quantum.h"
#include "hooks.h"
#include "teleport.h"
#include "sco/caps.h"
#include "sco/game/features.h"
#include "world_caps.h"
#include <cstring>

// DataCore/CryPak composition now belongs to sco_game_host: early discovery,
// OPEN/LOADED timing and producer drain before teardown. The retained drive data
// is moved into a normal external data folder by the pack migration lane.
// No private pseudo-plugin list or second loader may be reactivated here.

using OnActionFn = uintptr_t(__fastcall*)(uintptr_t handler, int action, int mode, float value, void* functor);
using StartUseFn = void(__fastcall*)(uintptr_t usable, uintptr_t user);
struct QuantumInput { int32_t action, mode; float value; };
using DriveInputFn = void(__fastcall*)(uintptr_t drive, const QuantumInput* input);
static OnActionFn   g_onActionOrig = nullptr;
static StartUseFn   g_startUseOrig = nullptr;
static DriveInputFn g_driveInput = nullptr;
static bool         g_inputHooked = false, g_startUseHooked = false, g_capsHeld = false;

constexpr int kEngage = 579;
constexpr int kEnableHold = 582;
constexpr int kPress = 1, kRelease = 2;

static uintptr_t g_handler = 0, g_handlerVt = 0;
static bool      g_held = false, g_byMouse = false;
static uintptr_t g_drive = 0, g_driveVt = 0;

static const char* StateName(int s) {
    static const char* const names[] = { "Off", "WaitForInputDelay", "TargetLocking", "Charging", "BoostingSlow",
                                         "Travelling", "CancelingTravel", "CancelingBoost", "Cooldown", "Done" };
    return s >= 0 && s < 10 ? names[s] : "?";
}

static const char* ReasonName(int r) {
    static const char* const names[] = { "None", "TargetTooClose", "TargetTooFar", "SolarObstruction", "QuantumObstruction",
                                         "MovingBackwards", "OutOfFuel", "GroupNotReady", "GroupHasObstruction", "NavpointBlocked",
                                         "RotationNotAligned", "RotationUnstable", "BlockedByEvent", "LandingGear", "Unknown" };
    return r >= 0 && r < 15 ? names[r] : "none";
}

static void NoteAction(int action) {
    static int seen[32], count = 0;
    for (int i = 0; i < count; ++i) if (seen[i] == action) return;
    if (count == 32) return;
    seen[count++] = action;
    Log("[qt] new drive input handler got action %d", action);
}

static uintptr_t __fastcall OnActionHook(uintptr_t handler, int action, int mode, float value, void* functor) {
    NoteAction(action);
    if (action != kEngage) return g_onActionOrig(handler, action, mode, value, functor);
    if (g_held && !g_byMouse) {
        g_held = false;
        Log("[qt] engage again -> quantum hold released");
        return g_onActionOrig(handler, kEnableHold, kRelease, 0.0f, functor);
    }
    g_handler = handler;
    g_handlerVt = Rd<uintptr_t>(handler);
    g_byMouse = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    g_held = true;
    Log("[qt] engage -> quantum hold pressed (%s)", g_byMouse ? "let go of the mouse button to stop" : "engage again to stop");
    return g_onActionOrig(handler, kEnableHold, kPress, 1.0f, functor);
}

static void __fastcall StartUseHook(uintptr_t usable, uintptr_t user) {
    g_startUseOrig(usable, user);
    const uintptr_t drive = usable - 0x1C0;
    __try {
        if (!Rd<uint8_t>(drive + 0xB60)) return;
        g_driveVt = Rd<uintptr_t>(drive);
        g_drive = drive;
        if (float* disableCharge = reinterpret_cast<float*>(Rd<uintptr_t>(drive + 0x18) + 0xC0); *disableCharge < 1e9f) {
            Log("[qt] drive data: disableCharge %.1f -> 1e9 (it blocked the drive at any charge)", *disableCharge);
            *disableCharge = 1e9f;
        }
        Log("[qt] you control a new quantum drive: hold Caps Lock, or B for NAV mode then hold left mouse "
            "(no quantum target = boost, target = travel)");
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

using EffectUpdateFn = uintptr_t(__fastcall*)(uintptr_t handler, uint64_t state, double a3, uint64_t a4,
                                              uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8);
static EffectUpdateFn g_effectUpdateOrig = nullptr;
static bool           g_effectGuarded = false;

static uintptr_t __fastcall EffectUpdateHook(uintptr_t handler, uint64_t state, double a3, uint64_t a4,
                                             uint64_t a5, uint64_t a6, uint64_t a7, uint64_t a8) {
    if (handler && (Rd<uint64_t>(handler) & kPtrMask)) return g_effectUpdateOrig(handler, state, a3, a4, a5, a6, a7, a8);
    static bool logged = false;
    if (!logged) { logged = true; Log("[qt] quantum effects aren't linked to the ship; skipped them (no crash)"); }
    return 0;
}

using ChargeFn = uint8_t(__fastcall*)(uintptr_t drive, float alignment, float dt, uint8_t blocked);
using SplineGetYFn = float(__fastcall*)(uintptr_t spline, float x);
static ChargeFn     g_chargeOrig = nullptr;
static SplineGetYFn g_splineGetY = nullptr;
static bool         g_chargeHooked = false;

static uint8_t __fastcall ChargeHook(uintptr_t drive, float alignment, float dt, uint8_t blocked) {
    const uint8_t result = g_chargeOrig(drive, alignment, dt, blocked);
    if (drive != g_drive) return result;
    static DWORD chargingSince = 0, lastLog = 0;
    const DWORD now = GetTickCount();
    const int32_t state = Rd<int32_t>(drive + 0xC48);
    if (state != 3) chargingSince = 0;
    else if (!chargingSince) chargingSince = now;
    const bool full = state == 4 || state == 5 || (state == 3 && now - chargingSince > 3000);
    if (now - lastLog >= 1000) {
        lastLog = now;
        __try {
            const uintptr_t params = Rd<uintptr_t>(drive + 0x18);
            const float* rate = reinterpret_cast<const float*>(params + 0x560);
            const float curve = g_splineGetY ? g_splineGetY(params + 0x320 + 0x20, 1.0f) : -1.0f;
            Log("[qt] %s: charge %.2f (game: %d), alignment %.2f, blocked %d, rates %.2f/%.2f/%.2f, curve(1) %.2f%s",
                StateName(state), Rd<float>(drive + 0xCA8), result, alignment, blocked, rate[0], rate[1], rate[2], curve,
                full ? " -> kept full" : "");
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (!full) return result;
    *reinterpret_cast<float*>(drive + 0xCA8) = 1.0f;
    return 3;
}

using HandleValidFn = bool(__fastcall*)(const uint64_t* handle);
static HandleValidFn g_handleValid = nullptr;
static uintptr_t*    g_audioSystem = nullptr;
static uint32_t      g_lightballId = 0, g_portalId = 0;
static int           g_soundStage = 0;

static uint32_t SoundId(const char* name) {
    uint32_t id = 0;
    if (g_audioSystem && *g_audioSystem) VCall<void>(*g_audioSystem, 0x58, name, &id);
    return id;
}

static void ShipSound(uint32_t id, bool play) {
    const uintptr_t handler = Rd<uintptr_t>(g_drive + 0x800);
    if (!handler || !id || !g_handleValid || !g_handleValid(reinterpret_cast<const uint64_t*>(handler))) return;
    const uintptr_t audio = Rd<uintptr_t>(handler) & kPtrMask;
    if (play) VCall<uint8_t>(audio, 0x700, id, 1u, 0u);
    else      VCall<uint8_t>(audio, 0x718, id, false, 1u);
}

static void UpdateBoostSounds(int32_t state) {
    __try {
        if (state >= 1 && state <= 3 && g_soundStage == 0) {
            if (!g_lightballId) {
                g_lightballId = SoundId("Play_SSQT_Stage_2_Lightball");
                g_portalId = SoundId("Play_SSQT_Stage_3_Portal");
                Log("[qt] sound ids: Lightball %08X, Portal %08X", g_lightballId, g_portalId);
            }
            ShipSound(g_lightballId, true);
            g_soundStage = 1;
        }
        if (state == 3 && g_soundStage == 1) { ShipSound(g_portalId, true); g_soundStage = 2; }
        if ((state == 4 || state == 5) && g_soundStage) {
            if (g_soundStage == 1) ShipSound(g_lightballId, false);
            g_soundStage = 3;
        }
        if ((state == 0 || state >= 8) && g_soundStage) {
            if (g_soundStage < 3) { ShipSound(g_lightballId, false); ShipSound(g_portalId, false); }
            g_soundStage = 0;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

using SendEffectTagFn = uintptr_t(__fastcall*)(uintptr_t entitySystem, uintptr_t entity, const void* evt);
static SendEffectTagFn g_sendEffectTag = nullptr;
static uintptr_t       g_warmedDrive = 0;
static DWORD           g_warmUpSince = 0;

static void WarmUpEffects(DWORD now) {
    if (!g_sendEffectTag || !g_tp.entitySystem || !*g_tp.entitySystem || g_warmedDrive == g_drive) return;
    if (!g_warmUpSince) g_warmUpSince = now;
    __try {
        const uintptr_t handler = Rd<uintptr_t>(g_drive + 0x7F0);
        if (Rd<int32_t>(g_drive + 0xC48) != 0 || !handler || !(Rd<uint64_t>(handler) & kPtrMask)
            || !g_handleValid || !g_handleValid(reinterpret_cast<const uint64_t*>(handler + 8))) {
            if (now - g_warmUpSince > 15000) {
                g_warmedDrive = g_drive;
                Log("[qt] quantum effects never linked to the ship; not warmed up");
            }
            return;
        }
        const uintptr_t entity = Rd<uint64_t>(handler + 8) & kPtrMask;
        static const size_t kTags[] = { 0x90, 0x98, 0xA0, 0xA8, 0xB0, 0xC8, 0xE0, 0xE8, 0xF0, 0xF8, 0x100 };
        int sent = 0;
        for (int on = 1; on >= 0; --on)
            for (size_t off : kTags) {
                struct { uint64_t tag; uint8_t on; uint8_t pad[7]; } evt = { Rd<uint64_t>(handler + off), static_cast<uint8_t>(on), {} };
                if (static_cast<uint16_t>(evt.tag) == 0xFFFF) continue;
                g_sendEffectTag(*g_tp.entitySystem, entity, &evt);
                sent += on;
            }
        g_warmedDrive = g_drive;
        g_warmUpSince = 0;
        Log("[qt] quantum effects warmed up (%d tags on and off) - the first boost plays in full", sent);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_warmedDrive = g_drive;
        Log("[qt] fault warming up the quantum effects");
    }
}

void ProcessQuantum() {
    if (g_held && g_byMouse && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        g_held = false;
        __try {
            if (Rd<uintptr_t>(g_handler) == g_handlerVt) {
                uint64_t functor[4] = {};
                g_onActionOrig(g_handler, kEnableHold, kRelease, 0.0f, functor);
                Log("[qt] mouse button up -> quantum hold released");
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    if (!g_drive) return;
    const bool caps = GameHasFocus() && (GetAsyncKeyState(VK_CAPITAL) & 0x8000) != 0;
    if (caps != g_capsHeld && g_driveInput) {
        g_capsHeld = caps;
        const QuantumInput input = { kEnableHold, caps ? kPress : kRelease, caps ? 1.0f : 0.0f };
        __try {
            if (Rd<uintptr_t>(g_drive) == g_driveVt) g_driveInput(g_drive, &input);
            Log("[qt] Caps Lock %s -> quantum hold %s", caps ? "down" : "up", caps ? "pressed" : "released");
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    struct { int32_t state, reason; uint8_t powered, travelBlocked, boostBlocked, input; } now{};
    __try {
        if (Rd<uintptr_t>(g_drive) != g_driveVt) { g_drive = 0; return; }
        now.state = Rd<int32_t>(g_drive + 0xC48);
        now.reason = Rd<int32_t>(g_drive + 0xC3C);
        now.powered = Rd<uint8_t>(g_drive + 0xC68);
        now.travelBlocked = Rd<uint8_t>(g_drive + 0xC69);
        now.boostBlocked = Rd<uint8_t>(g_drive + 0xC6A);
        now.input = Rd<uint8_t>(g_drive + 0xBB0);
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_drive = 0; return; }
    UpdateBoostSounds(now.state);
    WarmUpEffects(GetTickCount());
    static decltype(now) last{ -1, -1 };
    if (!memcmp(&now, &last, sizeof(now))) return;
    last = now;
    Log("[qt] drive: %s, powered %d, input %d, boost blocked %d (reason %s), travel blocked %d",
        StateName(now.state), now.powered, now.input, now.boostBlocked, ReasonName(now.reason), now.travelBlocked);
}

// The functions come from sco-core's quantum.* rows (sco/game/world.h); hooking stays here.
static void HookBoostInput() {
    if (!WorldCapability("quantum.boost")) {
        Log("[!] quantum boost: functions not found (see the [core] lines in mod.log)");
        return;
    }
    g_inputHooked = HookFunction(sco::Sig("quantum.on_action"), 8, reinterpret_cast<void*>(&OnActionHook), reinterpret_cast<void**>(&g_onActionOrig));
    g_startUseHooked = HookFunction(sco::Sig("quantum.start_use"), 5, reinterpret_cast<void*>(&StartUseHook), reinterpret_cast<void**>(&g_startUseOrig));
    g_driveInput = reinterpret_cast<DriveInputFn>(sco::Sig("quantum.drive_input"));
    g_effectGuarded = HookFunction(sco::Sig("quantum.effect_update"), 8, reinterpret_cast<void*>(&EffectUpdateHook), reinterpret_cast<void**>(&g_effectUpdateOrig));
    g_chargeHooked = HookFunction(sco::Sig("quantum.charge"), 5, reinterpret_cast<void*>(&ChargeHook), reinterpret_cast<void**>(&g_chargeOrig));
    g_splineGetY = reinterpret_cast<SplineGetYFn>(sco::Sig("quantum.spline_get_y"));
    g_audioSystem = reinterpret_cast<uintptr_t*>(sco::Sig("quantum.audio_system"));
    g_handleValid = reinterpret_cast<HandleValidFn>(sco::Sig("quantum.handle_valid"));
}

// The effect tag sender is sco-core's quantum.send_effect_tag row (capability quantum.effect_tag,
// sco/game/features.h).
static void FindEffectTagSender() {
    size_t n = 0;
    const sco::game::features::Capability* caps = sco::game::features::Capabilities(n);
    for (size_t i = 0; i < n; ++i)
        if (strcmp(caps[i].name, "quantum.effect_tag") == 0) {
            sco::caps::SetFromSignatures(caps[i].name, caps[i].rows, caps[i].count);
            if (sco::caps::Has(caps[i].name)) g_sendEffectTag = reinterpret_cast<SendEffectTagFn>(sco::Sig("quantum.send_effect_tag"));
        }
}

void ResolveQuantumApi(const Section&, const Section&) {
    // Inactive legacy reference, excluded from the host target. G8 is approved,
    // but only the public game-pack service may admit owned drive writes.
    // Never reactivate this private offset/hook implementation as a fallback.
    Log("[app] private quantum control retired: use admitted game.quantum");
}

bool QuantumDriveReady() {
    // LOADED alone does not prove this pack applied. A per-pack consumer outcome
    // is still required; do not certify readiness from transport installation.
    return false;
}

bool QuantumBoostReady() {
    return g_inputHooked && g_startUseHooked && g_driveInput && g_effectGuarded && g_chargeHooked
        && g_audioSystem && g_handleValid && g_sendEffectTag;
}

void LogQuantum() {
    Log("[app] quantum drive pack: G1 DataCore owns loading; pack outcome unavailable (no private fallback)");
    const bool audio = g_audioSystem && g_handleValid;
    if (QuantumBoostReady())
        Log("[+] quantum boost: hold Caps Lock, or NAV mode + hold left mouse");
    else
        Log("[!] quantum boost: input hook %s, drive hook %s, drive input %s, effects guard %s, charge hook %s, audio %s, effect warm-up %s",
            g_inputHooked ? "ok" : "MISSING", g_startUseHooked ? "ok" : "MISSING", g_driveInput ? "ok" : "MISSING",
            g_effectGuarded ? "ok" : "MISSING", g_chargeHooked ? "ok" : "MISSING", audio ? "ok" : "MISSING",
            g_sendEffectTag ? "ok" : "MISSING");
}
