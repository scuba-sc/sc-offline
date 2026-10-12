#pragma once
#include "common.h"

void ResolveQuantumApi(const Section& text, const Section& rdata);
void LogQuantum();
bool QuantumDriveReady();   // false until an attributable migrated pack outcome is available
bool QuantumBoostReady();   // quantum boost works (LogQuantum's second line)
void ProcessQuantum();
