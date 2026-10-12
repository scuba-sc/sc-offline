#include "cvars.h"
#include "menu.h"
#include "world_caps.h"

// Each cvar's storage is a sco-core row (cvars.*, sco/game/world.h), in capability cvars.qdrive_kept_on.
static const struct { const char* cvar; int32_t value; const char* also; const char* row; } kKeptOn[] = {
    { "v_qdrive2.quantumTravelAllowed", 1, "p_enable_physical_quantum_travel 1", "cvars.quantum_travel_allowed" },
    { "v_qdrive2.quantumBoostAllowed", 1, nullptr, "cvars.quantum_boost_allowed" },
    { "v_qdrive2.setting_ignoreBlockedBoost", 1, nullptr, "cvars.ignore_blocked_boost" },
    { "v_qdrive2.setting_ignoreBlockedTravel", 1, nullptr, "cvars.ignore_blocked_travel" },
    { "v_qdrive.logging", 1, nullptr, "cvars.qdrive_logging" },
};
constexpr int kKeptOnCount = sizeof(kKeptOn) / sizeof(kKeptOn[0]);
static int32_t* g_keptOn[kKeptOnCount];

static const struct { const char* cvar; float value; const char* row; } kKeptOnFloat[] = {
    { "v_qdrive2.setting_targetLockAngularSpeedThresholdPlayer", 45.0f, "cvars.target_lock_angular" },
    { "v_qdrive2.setting_targetLockLinearSpeedThresholdPlayer", 1000.0f, "cvars.target_lock_linear" },
};
constexpr int kKeptOnFloatCount = sizeof(kKeptOnFloat) / sizeof(kKeptOnFloat[0]);
static float* g_keptOnFloat[kKeptOnFloatCount];

static uintptr_t* g_console = nullptr;

static void FindConsole() {
    if (WorldCapability("cvars.console")) { g_console = reinterpret_cast<uintptr_t*>(sco::Sig("cvars.console")); return; }
    Log("[cvars] console not found; console commands are disabled (see the [core] lines in mod.log)");
}

bool Menu_ConsoleReady() { return g_console && *g_console; }

void Menu_RunConsole(const char* cmd) {
    Log("[cvars] console command refused: arbitrary console execution is not approved (%s)", cmd ? cmd : "");
}

static bool Execute(const char* cmd) {
    if (!g_console || !*g_console) return false;
    __try {
        VCall<void>(*g_console, 0x130, cmd, false, false, static_cast<void*>(nullptr));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[cvars] fault while running '%s'", cmd);
        return false;
    }
}

bool SetCVarNow(const char* name, float value) {
    if (!g_console || !*g_console) return false;
    __try {
        const uintptr_t cvar = VCall<uintptr_t>(*g_console, 0xC0, name);
        if (!cvar) return false;
        VCall<void>(cvar, 0x38, value);
        return VCall<float>(cvar, 0x20) == value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool GetCVarNow(const char* name, float& value) {
    if (!g_console || !*g_console || !name || !*name) return false;
    __try {
        const uintptr_t cvar = VCall<uintptr_t>(*g_console, 0xC0, name);
        if (!cvar) return false;
        value = VCall<float>(cvar, 0x20);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Squadron 42 tab settings. The shipped mod carries these four cvars XORed with a
// per-blob key (Murmur-style fmix32 over the byte offset); these are the names
// decrypted out of dinput8.dll's label table.
static const struct { const char* label; const char* tip; const char* cvar; } kS42Settings[] = {
    { "SQ42 auto targeting",
      "\"Enables the auto targeting feature for SQ42\" (the game mode can override it).",
      "i_target_selector.targeting2_enabled" },
    { "Visor mini-map",
      "The mini-map on your visor HUD.",
      "pl_lensdisplay.minimap_enabled" },
    { "Visor greebles",
      "The decorative frame pieces on your visor HUD.",
      "pl_lensdisplay.greebles_enabled" },
    { "SQ42 menus (experimental)",
      "\"Enable Squadron 42 Frontend\". Switches the pause menu and loading screens to SQ42's, whose data is "
      "missing; turn it off before traveling or quitting if anything breaks.",
      "g_squadron_frontend" },
};
constexpr int kS42SettingCount = sizeof(kS42Settings) / sizeof(kS42Settings[0]);

int         Menu_S42SettingCount() { return kS42SettingCount; }
const char* Menu_S42SettingLabel(int i) { return i >= 0 && i < kS42SettingCount ? kS42Settings[i].label : ""; }
const char* Menu_S42SettingTip(int i)   { return i >= 0 && i < kS42SettingCount ? kS42Settings[i].tip   : ""; }
bool        Menu_S42SettingOn(int)    { return false; }
bool        Menu_S42SettingKnown(int) { return false; }

// Legacy UI is inactive. The external loadout plugin owns approved public reads;
// SQ42 cvar writes are not approved, and never enter the private cvar keeper.
void Menu_RequestS42Setting(int i, bool on) {
    Log("[sq42] setting write refused: read-only (index %d, value %d); cvar writes are not approved", i, on ? 1 : 0);
}

void ResolveCVarsApi(const Section&, const Section&) {
    if (WorldCapability("cvars.qdrive_kept_on")) {
        for (int i = 0; i < kKeptOnCount; ++i) g_keptOn[i] = reinterpret_cast<int32_t*>(sco::Sig(kKeptOn[i].row));
        for (int i = 0; i < kKeptOnFloatCount; ++i) g_keptOnFloat[i] = reinterpret_cast<float*>(sco::Sig(kKeptOnFloat[i].row));
    } else {
        Log("[+] quantum cvars not found; they stay as the game mode sets them (see the [core] lines in mod.log)");
    }
    FindConsole();
}

static void KeepSettingsOn() {
    for (int i = 0; i < kKeptOnCount; ++i) {
        int32_t* const s = g_keptOn[i];
        if (!s) continue;
        __try {
            if (*s == kKeptOn[i].value) continue;
            char cmd[96];
            snprintf(cmd, sizeof(cmd), "%s %d", kKeptOn[i].cvar, kKeptOn[i].value);
            Execute(cmd);
            if (*s != kKeptOn[i].value) *s = kKeptOn[i].value;
            if (kKeptOn[i].also) Execute(kKeptOn[i].also);
            Log("[+] %s = %d%s%s", kKeptOn[i].cvar, *s, kKeptOn[i].also ? ", " : "", kKeptOn[i].also ? kKeptOn[i].also : "");
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    for (int i = 0; i < kKeptOnFloatCount; ++i) {
        float* const s = g_keptOnFloat[i];
        if (!s) continue;
        __try {
            if (*s == kKeptOnFloat[i].value) continue;
            char cmd[96];
            snprintf(cmd, sizeof(cmd), "%s %g", kKeptOnFloat[i].cvar, kKeptOnFloat[i].value);
            Execute(cmd);
            if (*s != kKeptOnFloat[i].value) *s = kKeptOnFloat[i].value;
            Log("[+] %s = %g", kKeptOnFloat[i].cvar, *s);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

void ProcessCVars() {
    KeepSettingsOn();
}
