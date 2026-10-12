// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "sco_api.h"
// Thin host rendering facade for external build commands, independent of private game helpers.
void BuildMenuUseApi(const sco_api* api, sco_plugin* self);
int Menu_BuildCount();
const char* Menu_BuildName(int index);
const char* Menu_BuildCategory(int index);
int Menu_BuildCategoryOf(int index);
int Menu_BuildCategoryCount();
const char* Menu_BuildCategoryName(int category);
void Menu_SetBuild(int index, float reach);
float Menu_BuildReach();
void Menu_ToggleBuildMode();
bool Menu_BuildModeActive();
void Menu_BuildUndo();
void Menu_BuildClear();
int Menu_BuildPlacedCount();
void Menu_RequestPlace(int index, bool inFront, float aheadMetres);
