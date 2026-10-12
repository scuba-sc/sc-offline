#pragma once
#include "common.h"

void ResolveCVarsApi(const Section& text, const Section& rdata);
void ProcessCVars();
bool SetCVarNow(const char* name, float value);
bool GetCVarNow(const char* name, float& value);

// Inactive legacy Squadron 42 UI: public reads belong to the external loadout plugin.
int         Menu_S42SettingCount();
const char* Menu_S42SettingLabel(int index);
const char* Menu_S42SettingTip(int index);
bool        Menu_S42SettingOn(int index);      // always false: legacy reader is inactive
bool        Menu_S42SettingKnown(int index);   // always false: value is unknown
void        Menu_RequestS42Setting(int index, bool on);   // logs a read-only refusal; never queues a write
