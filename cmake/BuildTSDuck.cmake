# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Threads REQUIRED)

set(ARIBCAP_TSDUCK_ROOT "" CACHE PATH
  "Use an existing TSDuck install prefix instead of building vendor/tsduck")

if(ARIBCAP_TSDUCK_ROOT)
  set(TSDUCK_INSTALL_PREFIX "${ARIBCAP_TSDUCK_ROOT}")
else()
  include(ExternalProject)
  include(ProcessorCount)

  find_program(TSDUCK_MAKE_EXECUTABLE NAMES gmake make REQUIRED)
  ProcessorCount(TSDUCK_JOBS)
  if(TSDUCK_JOBS EQUAL 0)
    set(TSDUCK_JOBS 1)
  endif()

  set(TSDUCK_BUILD_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallTSDuck.cmake")
  set(TSDUCK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/tsduck")
  set(TSDUCK_VENDOR_ROOT "${CMAKE_CURRENT_BINARY_DIR}/_vendor/tsduck")
  set(TSDUCK_BUILD_ROOT "${TSDUCK_VENDOR_ROOT}/build")
  set(TSDUCK_INSTALL_ROOT "${TSDUCK_VENDOR_ROOT}/install")
  set(TSDUCK_INSTALL_PREFIX "${TSDUCK_INSTALL_ROOT}/usr")
endif()

set(TSDUCK_INCLUDE_DIR "${TSDUCK_INSTALL_PREFIX}/include")
set(TSDUCK_LIB_DIR "${TSDUCK_INSTALL_PREFIX}/lib")
set(TSDUCK_TSCORE_LIBRARY "${TSDUCK_LIB_DIR}/libtscore.a")
set(TSDUCK_TSDUCK_LIBRARY "${TSDUCK_LIB_DIR}/libtsduck.a")

if(ARIBCAP_TSDUCK_ROOT)
  foreach(TSDUCK_REQUIRED_PATH IN ITEMS
      "${TSDUCK_INCLUDE_DIR}/tscore"
      "${TSDUCK_INCLUDE_DIR}/tsduck"
      "${TSDUCK_TSCORE_LIBRARY}"
      "${TSDUCK_TSDUCK_LIBRARY}")
    if(NOT EXISTS "${TSDUCK_REQUIRED_PATH}")
      message(FATAL_ERROR "ARIBCAP_TSDUCK_ROOT is missing ${TSDUCK_REQUIRED_PATH}")
    endif()
  endforeach()
else()
  file(GLOB_RECURSE TSDUCK_LIBRARY_INPUTS CONFIGURE_DEPENDS
    "${TSDUCK_SOURCE_DIR}/src/libtscore/*"
    "${TSDUCK_SOURCE_DIR}/src/libtsduck/*")
  set(TSDUCK_SOURCE_INPUTS
    "${TSDUCK_BUILD_SCRIPT}"
    "${TSDUCK_SOURCE_DIR}/CONFIG.txt"
    "${TSDUCK_SOURCE_DIR}/Makefile.inc"
    "${TSDUCK_SOURCE_DIR}/scripts/build-tsduck-header.py"
    "${TSDUCK_SOURCE_DIR}/scripts/make-config.sh"
    "${TSDUCK_SOURCE_DIR}/scripts/tsbuild.py"
    "${TSDUCK_SOURCE_DIR}/src/Makefile"
    "${TSDUCK_SOURCE_DIR}/src/libtscore/Makefile"
    "${TSDUCK_SOURCE_DIR}/src/libtsduck/Makefile"
    ${TSDUCK_LIBRARY_INPUTS})

  set(TSDUCK_CPPFLAGS_EXTRA "${CMAKE_CXX_FLAGS}")
  set(TSDUCK_CXXFLAGS_EXTRA "${CMAKE_CXX_FLAGS}")
  set(TSDUCK_LDFLAGS_EXTRA "${CMAKE_EXE_LINKER_FLAGS}")

  set(TSDUCK_DEBUG false)
  if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" TSDUCK_BUILD_TYPE_UPPER)
    string(APPEND TSDUCK_CPPFLAGS_EXTRA " ${CMAKE_CXX_FLAGS_${TSDUCK_BUILD_TYPE_UPPER}}")
    string(APPEND TSDUCK_CXXFLAGS_EXTRA " ${CMAKE_CXX_FLAGS_${TSDUCK_BUILD_TYPE_UPPER}}")
    string(APPEND TSDUCK_LDFLAGS_EXTRA " ${CMAKE_EXE_LINKER_FLAGS_${TSDUCK_BUILD_TYPE_UPPER}}")

    if(TSDUCK_BUILD_TYPE_UPPER STREQUAL "DEBUG")
      set(TSDUCK_DEBUG true)
    endif()
  endif()

  file(MAKE_DIRECTORY
    "${TSDUCK_INCLUDE_DIR}"
    "${TSDUCK_INCLUDE_DIR}/tscore"
    "${TSDUCK_INCLUDE_DIR}/tsduck"
    "${TSDUCK_LIB_DIR}")

  # NONAMES (a Makefile build option we added to vendor/tsduck; see CONFIG.txt) disables
  # loading .names configuration files at run time. This project never needs .names
  # configuration files: it never formats a table ID, descriptor tag, or similar
  # identifier as a display name.
  #
  # TS_REGISTER_TABLE/_DESCRIPTOR is a TSDuck macro that each table/descriptor .cpp file
  # invokes once, expanding to a file-scope static object. Static initialization runs
  # each such object's constructor before main, once for every translation unit linked
  # into the binary. Each constructor registers its class into libtsduck's PSIRepository
  # singleton, regardless of whether the caller uses name-lookup/display.
  #
  # Without NONAMES, that singleton tries to load tsduck.dtv.names and logs "configuration
  # file 'dtv' not found" to stderr when the file is missing.
  #
  # "install-devel" does not build or install tsduck.dtv.names; only "install-tools" does,
  # and it also pulls in tsplugins/tstools that this project does not need. Disabling
  # the lookup is simpler than shipping tsduck.dtv.names.
  ExternalProject_Add(tsduck_external
    SOURCE_DIR "${TSDUCK_SOURCE_DIR}"
    PREFIX "${TSDUCK_VENDOR_ROOT}"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND
      "${CMAKE_COMMAND}"
        "-DTSDUCK_MAKE_EXECUTABLE=${TSDUCK_MAKE_EXECUTABLE}"
        "-DTSDUCK_JOBS=${TSDUCK_JOBS}"
        "-DTSDUCK_SOURCE_DIR=<SOURCE_DIR>"
        "-DTSDUCK_INSTALL_ROOT=${TSDUCK_INSTALL_ROOT}"
        "-DTSDUCK_BUILD_ROOT=${TSDUCK_BUILD_ROOT}"
        "-DTSDUCK_CC=${CMAKE_C_COMPILER}"
        "-DTSDUCK_CXX=${CMAKE_CXX_COMPILER}"
        "-DTSDUCK_AR=${CMAKE_AR}"
        "-DTSDUCK_CPPFLAGS_EXTRA=${TSDUCK_CPPFLAGS_EXTRA}"
        "-DTSDUCK_CXXFLAGS_EXTRA=${TSDUCK_CXXFLAGS_EXTRA}"
        "-DTSDUCK_LDFLAGS_EXTRA=${TSDUCK_LDFLAGS_EXTRA}"
        "-DTSDUCK_DEBUG=${TSDUCK_DEBUG}"
        -P "${TSDUCK_BUILD_SCRIPT}"
    BUILD_BYPRODUCTS
      "${TSDUCK_TSCORE_LIBRARY}"
      "${TSDUCK_TSDUCK_LIBRARY}"
    INSTALL_COMMAND "")

  ExternalProject_Add_Step(tsduck_external check_source_inputs
    COMMAND "${CMAKE_COMMAND}" -E echo_append
    DEPENDEES configure
    DEPENDERS build
    DEPENDS ${TSDUCK_SOURCE_INPUTS})
endif()

add_library(TSDuck::tscore STATIC IMPORTED GLOBAL)
set_target_properties(TSDuck::tscore PROPERTIES
  IMPORTED_LOCATION "${TSDUCK_TSCORE_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES
    "${TSDUCK_INCLUDE_DIR};${TSDUCK_INCLUDE_DIR}/tscore")
if(NOT ARIBCAP_TSDUCK_ROOT)
  add_dependencies(TSDuck::tscore tsduck_external)
endif()

add_library(TSDuck::tsduck STATIC IMPORTED GLOBAL)
set_target_properties(TSDuck::tsduck PROPERTIES
  IMPORTED_LOCATION "${TSDUCK_TSDUCK_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES
    "${TSDUCK_INCLUDE_DIR};${TSDUCK_INCLUDE_DIR}/tsduck")
target_link_libraries(TSDuck::tsduck INTERFACE
  TSDuck::tscore
  Threads::Threads
  ${CMAKE_DL_LIBS}
  m
  $<$<PLATFORM_ID:Linux>:rt>)
target_compile_features(TSDuck::tsduck INTERFACE cxx_std_20)
if(NOT ARIBCAP_TSDUCK_ROOT)
  add_dependencies(TSDuck::tsduck tsduck_external)
endif()
