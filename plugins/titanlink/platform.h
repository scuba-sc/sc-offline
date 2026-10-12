// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Internal platform boundary. No host-private headers or renderer hooks.
#include "sco_api.h"
#include "sco_ipc.h"
#include "sc_titanlink.h"
#include <cstdint>

void TlPlatformConfigure(const sco_api* api, sco_plugin* self);
bool TlPlatformLaunch(const char* game, const char* extra);
void TlPlatformTick(uint64_t now, uint32_t peer_pid);
void TlPlatformStop(); // Cancel a deferred launch; does not terminate the external game or overlay.
uint64_t TlPlatformNow();
bool TlPlatformFocused();
uint32_t TlPlatformButtons(bool titan, bool busy);
bool TlPlatformKey(int vk);
void TlPlatformViewSize(uint32_t& w, uint32_t& h);
bool TlPlatformAvailable();
void TlOverlayStart(const sco_ipc_v1* ipc, sco_plugin* self, uint64_t channel);
void TlOverlayStop(); // Must finish before closing the channel or releasing services.
void TlOverlayShow(bool on);

// Internal helpers shared only by the platform and owned overlay window.
void TlPlatformLog(sco_log_level level, const char* format, ...);
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
HWND TlPlatformWindow();
#endif
