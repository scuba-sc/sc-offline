#pragma once
#include "common.h"

extern bool g_hooksInstalled;

void InstallHooks();
void LogHooks();
bool HookFunction(uint8_t* target, size_t stolen, void* detour, void** original);
uint8_t* NearData(size_t n);
bool MakeCryString(void* out, const char* s);
void FreeCryString(void* s);
const char* RequestShipFromAtc(uint64_t atcEntity, uint64_t player, const char* shipClass);

// The selected offline ASOP ships (asop_ships.txt, listed as entitlements). Each class gets a stable
// session ship ID the first time it is listed; the ID is never reused or changed, so it is safe to keep
// in long-lived records however the checked set changes. Any thread.
// The ID of the ship behind a CSCURN's 16-byte id (URN +0x10; either qword, either byte order), or -1
// when it isn't a ship in the entitlement list issued now.
int FleetShipIndexOfUrnId(const uint64_t id[2]);
// A stable ship ID's class name, even once the ship is unchecked (the pointer stays valid); nullptr
// for an ID that was never issued.
const char* FleetShipClass(int shipId);

// Which ship classes an ASOP terminal lists. The Vehicles tab's checkboxes are the source;
// data\asop_ships.txt keeps them, and ASOP reloads that file for each entitlement-list request.
// Nothing is checked until you check one. Any thread.
bool Fleet_AsopListed(const char* name);
int  Fleet_AsopListedCount();
void Fleet_SetAsopListed(const char* name, bool on);
void Fleet_ReplaceAsopListed(const char* const* names, int count);   // the whole checked set
void Fleet_ClearAsopListed();
