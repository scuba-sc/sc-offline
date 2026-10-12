// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// A tiny loader for the targeted synthetic workflow tests, not a host implementation.
#include "sco_api.h"
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace test_module {
inline sco_plugin_query_fn query = nullptr;
inline sco_plugin_load_fn load = nullptr;
inline sco_plugin_unload_fn unload = nullptr;
#ifdef _WIN32
inline HMODULE module = nullptr;
inline void* Symbol(const char* name) { return reinterpret_cast<void*>(GetProcAddress(module, name)); }
#else
inline void* module = nullptr;
inline void* Symbol(const char* name) { return dlsym(module, name); }
#endif
inline void Open(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "Supply one workflow module path\n"); std::exit(1); }
#ifdef _WIN32
    module = LoadLibraryA(argv[1]);
#else
    module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
#endif
    if (!module) { std::fprintf(stderr, "Cannot load workflow module\n"); std::exit(1); }
    query = reinterpret_cast<sco_plugin_query_fn>(Symbol("sco_plugin_query"));
    load = reinterpret_cast<sco_plugin_load_fn>(Symbol("sco_plugin_load"));
    unload = reinterpret_cast<sco_plugin_unload_fn>(Symbol("sco_plugin_unload"));
    if (!query || !load || !unload) { std::fprintf(stderr, "Missing workflow export\n"); std::exit(1); }
}
inline const sco_plugin_info* Query() { return query(); }
inline sco_result Load(const sco_api* api, sco_plugin* self) { return load(api, self); }
inline void Unload() { unload(); }
inline void Close() {
#ifdef _WIN32
    FreeLibrary(module);
#else
    dlclose(module);
#endif
    module = nullptr; query = nullptr; load = nullptr; unload = nullptr;
}
}  // namespace test_module
