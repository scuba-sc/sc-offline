// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "common.h"
// Legacy shared primitives retained for NPC/contracts/bridges until their own public closure.
// NEVER used by the SDK-only build module.
void ResolveLegacyPlacementApi(const Section& text, const Section& rdata);
bool PlaceNearPlayer(double ahead, double side, double lift, double pos[3], double rot[4]);
bool GroundRay(uintptr_t zone, const double from[3], const double to[3], double hit[3]);
bool MoveEntityLocal(uint64_t id, const double pos[3], const double rot[4] = nullptr);
bool EntityRotation(uintptr_t entity, double rot[4]);

