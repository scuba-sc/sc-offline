# SPDX-License-Identifier: GPL-3.0-only
# Standalone build of the Rust menu test host against a Core source tree that has
# include/sco/widgets_view.h (the 2.4.1 integration line). Not part of the product build.
#
#   cmake -S tools/rust-menu-host -B <build folder> -DSCO_CORE_SOURCE_DIR=<core checkout>
#   cmake --build <build folder> --config Release --target rust_menu_host
#   <build folder>/Release/rust_menu_host.exe <path>/sc_offline_menu.dll [screenshot folder]
#   (default screenshot folder: rust-menu-host-shots under the system temp folder)
cmake_minimum_required(VERSION 3.20)
project(rust_menu_host CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SCO_CORE_SOURCE_DIR "" CACHE PATH "Core source tree with include/sco/widgets_view.h")
if(NOT EXISTS "${SCO_CORE_SOURCE_DIR}/include/sco/widgets_view.h")
  message(FATAL_ERROR "SCO_CORE_SOURCE_DIR must hold include/sco/widgets_view.h (Core 2.4.1 integration line)")
endif()
set(SCO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SCO_WERROR OFF CACHE BOOL "" FORCE)
add_subdirectory("${SCO_CORE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/sco-core" EXCLUDE_FROM_ALL)

add_executable(rust_menu_host rust_menu_host.cpp)
# The host includes src/widgets/view_internal.h, the same private seam the Core tests use.
target_include_directories(rust_menu_host PRIVATE "${SCO_CORE_SOURCE_DIR}" "${SCO_CORE_SOURCE_DIR}/include")
target_link_libraries(rust_menu_host PRIVATE sco_widgets)

# Compile check of the host hook (src/menu_rust.cpp) against this Core: objects only, nothing linked.
add_library(menu_rust_hook_check OBJECT "${CMAKE_CURRENT_SOURCE_DIR}/../../src/menu_rust.cpp")
target_include_directories(menu_rust_hook_check PRIVATE "${SCO_CORE_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_compile_definitions(menu_rust_hook_check PRIVATE SCO_HOST_MENU_CLIENT)
