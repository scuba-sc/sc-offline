// SPDX-License-Identifier: GPL-3.0-only
// Host that loads sco_lifetime_fixture.dll with LoadLibrary (an injected or late-loaded copy).
#include <windows.h>
#include <stdio.h>

typedef int (*fn_t)(void);

static int mapped(void* p) {
    MEMORY_BASIC_INFORMATION m;
    return VirtualQuery(p, &m, sizeof m) && m.State == MEM_COMMIT && m.Type == MEM_IMAGE;
}

int main(void) {
    int fail = 0;
    // Case A: a single release before any pin unloads the module (the lifetime gap).
    HMODULE h = LoadLibraryW(L"sco_lifetime_fixture.dll");
    if (!h) {
        printf("dynamic: load failed %lu\n", GetLastError());
        return 2;
    }
    fn_t mode = (fn_t)GetProcAddress(h, "fixture_load_mode");
    void* cb = (void*)GetProcAddress(h, "fixture_callback");
    printf("dynamic A: load_mode=%d (expect 0)\n", mode());
    if (mode() != 0) fail = 1;
    FreeLibrary(h);
    printf("dynamic A: after one FreeLibrary module=%p callback mapped=%d (expect null, 0)\n",
           (void*)GetModuleHandleW(L"sco_lifetime_fixture.dll"), mapped(cb));
    if (GetModuleHandleW(L"sco_lifetime_fixture.dll") || mapped(cb)) fail = 1;
    // Case B: pin outside DllMain, then unbalanced releases cannot unload.
    h = LoadLibraryW(L"sco_lifetime_fixture.dll");
    if (!h) {
        printf("dynamic: reload failed %lu\n", GetLastError());
        return 2;
    }
    fn_t pin = (fn_t)GetProcAddress(h, "fixture_pin");
    fn_t call = (fn_t)GetProcAddress(h, "fixture_callback");
    int pinned = pin();
    for (int i = 0; i < 1000; ++i) FreeLibrary(h);
    printf("dynamic B: pin=%d; after 1000 FreeLibrary module=%p callback=%d (expect 1, non-null, 42)\n",
           pinned, (void*)GetModuleHandleW(L"sco_lifetime_fixture.dll"), mapped((void*)call) ? call() : -1);
    if (!pinned || !GetModuleHandleW(L"sco_lifetime_fixture.dll") || !mapped((void*)call)) fail = 1;
    printf("dynamic: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}
