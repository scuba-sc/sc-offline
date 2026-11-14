// SPDX-License-Identifier: GPL-3.0-only
// Synthetic module-lifetime fixture. No game, launcher or product code.
#include <windows.h>

static volatile LONG g_mode = -1;   // 1 static load, 0 dynamic load (DllMain lpReserved)

__declspec(dllexport) int fixture_load_mode(void) { return (int)g_mode; }
__declspec(dllexport) int fixture_callback(void) { return 42; }
// Pins the module from ordinary code, not from DllMain.
__declspec(dllexport) int fixture_pin(void) {
    HMODULE m = NULL;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              (LPCWSTR)&fixture_callback, &m) ? 1 : 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    (void)h;
    if (reason == DLL_PROCESS_ATTACH) g_mode = reserved ? 1 : 0;
    return TRUE;
}
