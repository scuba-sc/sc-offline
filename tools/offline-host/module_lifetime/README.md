# Module lifetime fixture

Synthetic Windows fixture for the startup gate's load-mode rule. It builds a
tiny DLL (`sco_lifetime_fixture.dll`) and two hosts, and checks on the running OS:

- `module_lifetime_static`: a DLL imported through the executable's import table
  receives a non-null `lpvReserved` in `DLL_PROCESS_ATTACH`, and 1000 unbalanced
  `FreeLibrary` calls leave it mapped and callable.
- `module_lifetime_dynamic`: the same DLL loaded with `LoadLibraryW` receives a
  null `lpvReserved`, and one `FreeLibrary` before any pin unmaps it. After a pin
  taken outside `DllMain`, 1000 `FreeLibrary` calls leave it mapped.

It uses no game, launcher or product code. Passing it does not enable native
activation, and does not establish native reader/worker exclusion or drain.
