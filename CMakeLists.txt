# encedo-hem-c-api — the Encedo HEM C SDK.
# implements: REQ-BUILD-001
#
# Cross-platform build (Linux GCC/Clang, Windows MinGW/MSYS2) producing static
# and shared `encedo-hem` libraries plus, later, the hem-tool executable.
# See ARCHITECTURE.md §1 (fixed decisions) and §10 (directory layout).

cmake_minimum_required(VERSION 3.20)

project(encedo-hem
  VERSION 0.1.0
  DESCRIPTION "Encedo HEM C SDK"
  LANGUAGES C)

# --- global build settings ------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)                 # strict C99, no GNU extensions

# Hidden by default everywhere; only EHEM_API symbols escape (REQ-API-006).
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# Warnings-as-errors baseline (REQ-BUILD-001).
if(MSVC)
  set(EHEM_WARNING_FLAGS /W4 /WX)
else()
  set(EHEM_WARNING_FLAGS -Wall -Wextra -Werror)
endif()

# Optional sanitizers for the unit suite on Linux/Clang/GCC (STEP-M1-040 DoD).
option(EHEM_SANITIZE "Build with ASan/UBSan (Debug, non-MSVC)" OFF)
if(EHEM_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# --- library --------------------------------------------------------------
# Compiled twice so the export macro differs (shared → dllexport/visibility,
# static → nothing). The source set is tiny; correctness beats sharing objects.
set(EHEM_SOURCES
  src/ehem.c)

add_library(encedo-hem        SHARED ${EHEM_SOURCES})   # libencedo-hem.so/.dll
add_library(encedo-hem-static STATIC ${EHEM_SOURCES})   # libencedo-hem.a
add_library(encedo-hem::encedo-hem        ALIAS encedo-hem)
add_library(encedo-hem::encedo-hem-static ALIAS encedo-hem-static)

foreach(_t encedo-hem encedo-hem-static)
  target_include_directories(${_t} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
  target_compile_definitions(${_t} PRIVATE
    EHEM_VERSION_STRING="${PROJECT_VERSION}")
  target_compile_options(${_t} PRIVATE ${EHEM_WARNING_FLAGS})
  set_target_properties(${_t} PROPERTIES OUTPUT_NAME encedo-hem)
endforeach()

# Only the shared build gets the export-enabling macro.
target_compile_definitions(encedo-hem PRIVATE EHEM_BUILDING_SHARED)
set_target_properties(encedo-hem PROPERTIES
  VERSION   ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR})

# MinGW: with dllexport present, restrict the DLL to those symbols only, and
# emit a .def file listing the exports. The .def is a stable, machine-readable
# export list (unlike `objdump -p`, whose format drifts across binutils
# versions) that the symbol-export check parses on Windows.
if(WIN32 AND NOT MSVC)
  set_target_properties(encedo-hem PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS OFF)
  target_link_options(encedo-hem PRIVATE
    "-Wl,--output-def,${CMAKE_CURRENT_BINARY_DIR}/libencedo-hem.def")
endif()

# --- install / export -----------------------------------------------------
install(TARGETS encedo-hem encedo-hem-static
  EXPORT encedo-hem-targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT encedo-hem-targets
  NAMESPACE encedo-hem::
  FILE encedo-hem-targets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/encedo-hem)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/encedo-hem-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/encedo-hem-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/encedo-hem)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/encedo-hem-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/encedo-hem-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/encedo-hem-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/encedo-hem)

# --- tests ----------------------------------------------------------------
include(CTest)               # defines BUILD_TESTING (default ON), enables CTest
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
