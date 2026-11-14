// SPDX-License-Identifier: GPL-3.0-only
// Host that imports sco_lifetime_fixture.dll through its import table, as an executable
// imports a proxy DLL. Unbalanced releases must not unmap the module.
#include <windows.h>
#include <stdio.h>

__declspec(dllimport) int fixture_load_mode(void);
__declspec(dllimport) int fixture_callback(void);

int main(void) {
    int fail = 0;
    HMODULE h = GetModuleHandleW(L"sco_lifetime_fixture.dll");
    printf("static: module=%p load_mode=%d (expect 1)\n", (void*)h, fixture_load_mode());
    if (!h || fixture_load_mode() != 1) fail = 1;
    int ok = 0;
    for (int i = 0; i < 1000; ++i) ok += FreeLibrary(h) ? 1 : 0;
    HMODULE after = GetModuleHandleW(L"sco_lifetime_fixture.dll");
    printf("static: 1000 unbalanced FreeLibrary calls returned TRUE %d times; module after=%p (expect same)\n",
           ok, (void*)after);
    if (after != h) fail = 1;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)&fixture_callback, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) {
        printf("static: import thunk not committed\n");
        fail = 1;
    }
    if (!fail) {
        int v = fixture_callback();
        printf("static: callback after releases=%d (expect 42)\n", v);
        if (v != 42) fail = 1;
    }
    printf("static: %s\n", fail ? "FAIL" : "PASS");
    return fail;
}
