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

// Offline fleet ships (ships.txt). Each class receives a never-reused session ID, not a row
// position. Decode either CSCURN qword/byte order; return -1 unless currently issued. Any thread.
int FleetShipIndexOfUrnId(const uint64_t id[2]);
// Historical names remain immutable and valid for the process lifetime, even when unlisted.
const char* FleetShipClass(int shipId);   // nullptr for an ID never assigned
