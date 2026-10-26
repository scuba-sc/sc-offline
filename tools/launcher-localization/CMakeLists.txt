# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.20)
project(launcher_localization_checks LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
enable_testing()
set(LOCALE_FONT "" CACHE FILEPATH "Installed system font for Cyrillic coverage proof")
add_executable(launcher_localization_test ../launcher_localization_test.cpp)
add_test(NAME launcher_localization COMMAND launcher_localization_test
  "${CMAKE_CURRENT_LIST_DIR}/../../locales/launcher/en.ini"
  "${CMAKE_CURRENT_LIST_DIR}/../../locales/launcher/ru.ini")
add_executable(launcher_localization_ui_test ../launcher_localization_ui_test.cpp
  ../../src/third_party/imgui/imgui.cpp ../../src/third_party/imgui/imgui_draw.cpp
  ../../src/third_party/imgui/imgui_tables.cpp ../../src/third_party/imgui/imgui_widgets.cpp)
add_test(NAME launcher_localization_ui COMMAND launcher_localization_ui_test
  "${CMAKE_CURRENT_LIST_DIR}/../../locales/launcher/ru.ini" "${LOCALE_FONT}")
add_test(NAME launcher_localization_font_fallback COMMAND launcher_localization_ui_test
  "${CMAKE_CURRENT_LIST_DIR}/../../locales/launcher/ru.ini" "")
foreach(target IN ITEMS launcher_localization_test launcher_localization_ui_test)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  endif()
endforeach()
if(WIN32)
  add_executable(launcher_localization_win32_test ../launcher_localization_win32_test.cpp)
  if(MSVC)
    target_compile_options(launcher_localization_win32_test PRIVATE /W4 /WX /utf-8)
  endif()
  add_test(NAME launcher_localization_win32 COMMAND launcher_localization_win32_test
    "${CMAKE_CURRENT_BINARY_DIR}/native-fixtures")
  add_library(launcher_locale_sqlite STATIC ../../external/sco-core/third_party/sqlite/sqlite3.c)
  target_compile_definitions(launcher_locale_sqlite PRIVATE SQLITE_THREADSAFE=1)
  add_executable(launcher_localization_gui_test ../launcher_localization_gui_test.cpp
    ../../src/third_party/imgui/imgui.cpp ../../src/third_party/imgui/imgui_draw.cpp
    ../../src/third_party/imgui/imgui_tables.cpp ../../src/third_party/imgui/imgui_widgets.cpp
    ../../src/third_party/imgui/imgui_impl_win32.cpp ../../src/third_party/imgui/imgui_impl_dx11.cpp)
  target_compile_definitions(launcher_localization_gui_test PRIVATE UNICODE _UNICODE _CONSOLE _CRT_SECURE_NO_WARNINGS)
  if(MSVC)
    target_compile_options(launcher_localization_gui_test PRIVATE /W3 /WX /utf-8 /EHsc /permissive- /sdl)
  endif()
  target_link_libraries(launcher_localization_gui_test PRIVATE launcher_locale_sqlite
    d3d11 d3dcompiler dwmapi imm32 bcrypt shell32 shlwapi pathcch advapi32 ole32 winhttp user32 gdi32 comctl32)
  add_test(NAME launcher_localization_gui COMMAND launcher_localization_gui_test
    "${CMAKE_CURRENT_LIST_DIR}/../../locales/launcher/ru.ini"
    "${CMAKE_CURRENT_BINARY_DIR}/native-fixtures")
endif()
