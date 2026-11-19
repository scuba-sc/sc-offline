# Module lifetime fixture

A synthetic Windows fixture for the startup gate's load-mode rule ([host composition](../../../docs/host-composition.md#startup-gate)). It builds a small DLL (`sco_lifetime_fixture.dll`) and two hosts, and checks the running OS:

- `module_lifetime_static`: a DLL imported through the executable's import table receives a non-null `lpvReserved` in `DLL_PROCESS_ATTACH`. 1000 unbalanced `FreeLibrary` calls leave it mapped and callable.
- `module_lifetime_dynamic`: the same DLL loaded with `LoadLibraryW` receives a null `lpvReserved`, and one `FreeLibrary` before any pin unmaps it. After a pin taken outside `DllMain`, 1000 `FreeLibrary` calls leave it mapped.

It uses no game, launcher or product code. Passing it does not enable native activation or show reader or worker exclusion or drain.
