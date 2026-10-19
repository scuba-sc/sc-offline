# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.20)
project(sc_offline_inventory_contents LANGUAGES CXX)
set(SCO_SDK_INCLUDE_DIR "" CACHE PATH "Extracted checksum-verified core and game SDK include directory")
if(NOT EXISTS "${SCO_SDK_INCLUDE_DIR}/../SHA256SUMS")
  message(FATAL_ERROR "Use an extracted SDK archive with its checksum inventory")
endif()
foreach(_header sco_api.h sc_container_contents.h)
  if(NOT EXISTS "${SCO_SDK_INCLUDE_DIR}/${_header}")
    message(FATAL_ERROR "Missing required SDK header: ${_header}")
  endif()
endforeach()
add_library(inventory_contents MODULE inventory_contents.cpp)
target_include_directories(inventory_contents PRIVATE "${SCO_SDK_INCLUDE_DIR}")
target_compile_features(inventory_contents PRIVATE cxx_std_20)
set_target_properties(inventory_contents PROPERTIES PREFIX "" SUFFIX ".dll"
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/inventory_contents/$<0:>"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/inventory_contents/$<0:>")
if(MSVC)
  target_compile_options(inventory_contents PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(inventory_contents PRIVATE -Wall -Wextra -Werror)
endif()
configure_file(plugin.ini "${CMAKE_BINARY_DIR}/plugins/inventory_contents/plugin.ini" COPYONLY)
install(TARGETS inventory_contents LIBRARY DESTINATION data/plugins/inventory_contents
  RUNTIME DESTINATION data/plugins/inventory_contents)
install(FILES plugin.ini README.md DESTINATION data/plugins/inventory_contents)
