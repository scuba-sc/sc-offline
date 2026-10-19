# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.20)
project(sc_offline_actor_conditions LANGUAGES CXX)
set(SCO_SDK_INCLUDE_DIR "" CACHE PATH "Extracted checksum-verified SDK includes")
if(NOT EXISTS "${SCO_SDK_INCLUDE_DIR}/../SHA256SUMS" OR NOT EXISTS "${SCO_SDK_INCLUDE_DIR}/sc_actor_stats.h")
  message(FATAL_ERROR "Use an extracted candidate SDK with actor stats 1.1 and checksum inventory")
endif()
add_library(actor_conditions MODULE actor_conditions.cpp)
target_include_directories(actor_conditions PRIVATE "${SCO_SDK_INCLUDE_DIR}")
target_compile_features(actor_conditions PRIVATE cxx_std_20)
set_target_properties(actor_conditions PROPERTIES PREFIX "" SUFFIX ".dll"
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/actor_conditions/$<0:>"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/actor_conditions/$<0:>")
if(MSVC)
  target_compile_options(actor_conditions PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(actor_conditions PRIVATE -Wall -Wextra -Werror)
endif()
configure_file(plugin.ini "${CMAKE_BINARY_DIR}/plugins/actor_conditions/plugin.ini" COPYONLY)
install(TARGETS actor_conditions LIBRARY DESTINATION data/plugins/actor_conditions
  RUNTIME DESTINATION data/plugins/actor_conditions)
install(FILES plugin.ini README.md DESTINATION data/plugins/actor_conditions)
