// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

// Product control surface only; this is not a plugin or core ABI.
enum class MenuWorkerState { NotStarted, Joined, Unknown };
// Scoped to this worker owner's acquired DLL reference. NotAcquired says
// nothing about other product callbacks; Busy cannot establish ownership.
enum class MenuBacking { NotAcquired, ProcessRetained, UnknownRetainRequired };
enum class MenuDrainStatus { Complete, TimedOut, WrongThread, Busy, WaitFailed };
struct MenuDrainReport {
    MenuWorkerState worker;
    MenuBacking backing;
    MenuDrainStatus status;
};

// Any product thread. Closes new frame admission permanently; does not wait,
// call core, destroy resources or join. An admitted frame may still be running.
void Menu_RequestStop() noexcept;
// Only the same control thread that called Menu_Start, outside DllMain/loader
// lock and runtime callouts. Requests stop and waits at most min(timeoutMs,60000).
// A joined worker still retains code, ImGui/D3D and optional client backing.
// Timeout/wait failure leaves all handles and backing intact. No restart or unload
// is authorized by any result. Wrong-thread/busy calls do not wait.
MenuDrainReport Menu_Drain(uint32_t timeoutMs) noexcept;
