# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.25)

project(
  aribcap-dump
  VERSION 0.1.0
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(ARIBCAP_ARIBCAPTION_ROOT "" CACHE PATH
  "Use an existing libaribcaption install prefix instead of building vendor/libaribcaption")

if(ARIBCAP_ARIBCAPTION_ROOT)
  find_package(aribcaption REQUIRED PATHS "${ARIBCAP_ARIBCAPTION_ROOT}" NO_DEFAULT_PATH)
else()
  set(ARIBCC_NO_RENDERER ON CACHE BOOL "Disable libaribcaption renderer" FORCE)
  set(ARIBCC_BUILD_TESTS OFF CACHE BOOL "Disable libaribcaption tests" FORCE)
  set(ARIBCC_SHARED_LIBRARY OFF CACHE BOOL "Build libaribcaption statically" FORCE)
  add_subdirectory(vendor/libaribcaption EXCLUDE_FROM_ALL)
endif()

set(ARIBCAP_CATCH2_ROOT "" CACHE PATH
  "Use an existing Catch2 install prefix instead of building vendor/Catch2")

if(ARIBCAP_CATCH2_ROOT)
  find_package(Catch2 3 REQUIRED PATHS "${ARIBCAP_CATCH2_ROOT}" NO_DEFAULT_PATH)
else()
  set(CATCH_INSTALL_DOCS OFF CACHE BOOL "Disable vendored Catch2 docs install" FORCE)
  set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "Disable vendored Catch2 extras install" FORCE)
  add_subdirectory(vendor/Catch2 EXCLUDE_FROM_ALL)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vendor/Catch2/extras")
endif()

set(CLI11_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/CLI11/include")
set(JSONCONS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/jsoncons/include")

include(cmake/BuildTSDuck.cmake)

add_library(aribcap_core STATIC
  src/core/caption_classifier.cc
  src/core/caption_record_emitter.cc
  src/core/caption_dumper.cc
  src/core/eit_record_emitter.cc
  src/core/eit_parser.cc
  src/core/output_record.cc
  src/core/output_record_sink.cc
  src/core/packet_framer.cc
  src/core/program_clock.cc
  src/core/ts_utils.cc)
target_include_directories(aribcap_core PUBLIC src PRIVATE ${JSONCONS_INCLUDE_DIR})
target_link_libraries(aribcap_core PUBLIC aribcaption::aribcaption TSDuck::tsduck)

add_executable(aribcap-dump src/main.cc)
target_include_directories(aribcap-dump PRIVATE ${CLI11_INCLUDE_DIR})
target_link_libraries(aribcap-dump PRIVATE aribcap_core)
install(TARGETS aribcap-dump RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

enable_testing()
add_executable(aribcap_tests
  tests/caption_classifier_test.cc
  tests/caption_record_emitter_test.cc
  tests/caption_dumper_test.cc
  tests/eit_record_emitter_test.cc
  tests/eit_parser_test.cc
  tests/output_record_json_test.cc
  tests/packet_framer_test.cc
  tests/program_clock_test.cc
  tests/ts_utils_test.cc)
target_include_directories(aribcap_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(aribcap_tests PRIVATE aribcap_core Catch2::Catch2WithMain)
include(CTest)
include(Catch)
catch_discover_tests(aribcap_tests)
