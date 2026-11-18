# SPDX-License-Identifier: GPL-3.0-only
# Builds the launcher contract dump tool. It includes launcher/launcher.cpp, so it links what
# launcher_status_config_test links (ImGui sources, the vendored SQLite). Windows only.
cmake_minimum_required(VERSION 3.20)
project(launcher_contract_dump LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(SCO_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../external/sco-core" CACHE PATH "sco-core checkout holding third_party/sqlite")
if(NOT WIN32)
  message(FATAL_ERROR "launcher_contract_dump compiles the Windows launcher and builds on Windows only")
endif()
set(IMGUI "${CMAKE_CURRENT_LIST_DIR}/../../src/third_party/imgui")
add_library(contract_sqlite STATIC "${SCO_CORE_DIR}/third_party/sqlite/sqlite3.c")
target_compile_definitions(contract_sqlite PRIVATE SQLITE_THREADSAFE=1)
target_include_directories(contract_sqlite PUBLIC "${SCO_CORE_DIR}/third_party/sqlite")
add_executable(launcher_contract_dump dump.cpp
  ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
  ${IMGUI}/imgui_impl_win32.cpp ${IMGUI}/imgui_impl_dx11.cpp)
target_compile_definitions(launcher_contract_dump PRIVATE UNICODE _UNICODE _CONSOLE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
  target_compile_options(launcher_contract_dump PRIVATE /W3 /WX /utf-8 /EHsc /permissive- /sdl)
endif()
target_link_libraries(launcher_contract_dump PRIVATE contract_sqlite
  d3d11 d3dcompiler dwmapi imm32 bcrypt shell32 shlwapi pathcch advapi32 ole32 winhttp user32 gdi32 comctl32)
