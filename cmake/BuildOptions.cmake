# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# Centralized user-facing build options for komari-agent-c.
# Include this file from the top-level CMakeLists.txt BEFORE project()
# is called so that options can influence the toolchain setup.

# ---------------------------------------------------------------------------
# Build type selection
# ---------------------------------------------------------------------------
# Default to Release if the caller did not specify a build type. Allowed
# values: Debug, Release, RelWithDebInfo, MinSizeRel, Coverage, Sanitize.
# Note: do NOT name this variable CMAKE_DEFAULT_BUILD_TYPE — CMake 3.15+
# treats CMAKE_* as reserved and the generator will reject it.
set(KOMARI_DEFAULT_BUILD_TYPE "Release" CACHE STRING "Default build type")
set_property(CACHE KOMARI_DEFAULT_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel Coverage Sanitize)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "${KOMARI_DEFAULT_BUILD_TYPE}" CACHE STRING
        "Build type (Debug Release RelWithDebInfo MinSizeRel Coverage Sanitize)" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel Coverage Sanitize)
endif()

message(STATUS "komari-agent-c build type: ${CMAKE_BUILD_TYPE}")

# ---------------------------------------------------------------------------
# Build profile selection
# ---------------------------------------------------------------------------
# Distinguishes between standalone binary builds (Linux/Docker, used for
# direct deployment) and OpenWrt package builds (cross-compiled via the
# OpenWrt SDK to produce .ipk/.apk packages).
#
#   binary  - Default. Standalone ELF binary for direct installation.
#             Enables tests, CPack, install rules for /usr/bin etc.
#   openwrt - OpenWrt package build. Disables tests/CPack, adjusts default
#             install layout, and tweaks hardening flags for musl libc.
#
# This option is set automatically by the openwrt CMakePreset and by the
# OpenWrt Makefile's Build/Compile step; users rarely need to set it by hand.
set(KOMARI_BUILD_PROFILE "binary" CACHE STRING
    "Build profile: 'binary' (standalone) or 'openwrt' (OpenWrt package)")
set_property(CACHE KOMARI_BUILD_PROFILE PROPERTY STRINGS binary openwrt)

if(KOMARI_BUILD_PROFILE STREQUAL "openwrt")
    set(KOMARI_PROFILE_OPENWRT ON)
    set(KOMARI_PROFILE_BINARY  OFF)
elseif(KOMARI_BUILD_PROFILE STREQUAL "binary")
    set(KOMARI_PROFILE_OPENWRT OFF)
    set(KOMARI_PROFILE_BINARY  ON)
else()
    message(FATAL_ERROR
        "KOMARI_BUILD_PROFILE must be 'binary' or 'openwrt', got: '${KOMARI_BUILD_PROFILE}'")
endif()

# OpenWrt builds always disable tests (no Unity framework in the SDK
# staging tree) and CPack (the SDK produces .ipk/.apk via its own packager).
if(KOMARI_PROFILE_OPENWRT)
    set(KOMARI_BUILD_TESTS_DEFAULT OFF)
    set(KOMARI_ENABLE_CPACK_DEFAULT OFF)
else()
    set(KOMARI_BUILD_TESTS_DEFAULT ON)
    set(KOMARI_ENABLE_CPACK_DEFAULT ON)
endif()

# ---------------------------------------------------------------------------
# CMake build subdirectory name (used by OpenWrt Makefile)
# ---------------------------------------------------------------------------
# When the OpenWrt SDK invokes CMake inside $(PKG_BUILD_DIR), it uses
# -B $(KOMARI_BUILD_SUBDIR) to keep the OpenWrt build tree visually
# distinct from a regular local `build/` directory. This variable is
# also referenced by CMakePresets.json's openwrt preset.
if(KOMARI_PROFILE_OPENWRT)
    set(KOMARI_BUILD_SUBDIR "build-openwrt" CACHE STRING
        "CMake build subdirectory name (OpenWrt profile)")
else()
    set(KOMARI_BUILD_SUBDIR "build" CACHE STRING
        "CMake build subdirectory name (binary profile)")
endif()

# ---------------------------------------------------------------------------
# Feature toggles
# ---------------------------------------------------------------------------
option(KOMARI_BUILD_TESTS "Build unit tests (alias of BUILD_TESTING)" ${KOMARI_BUILD_TESTS_DEFAULT})
option(KOMARI_BUILD_SHARED "Build komari_core as a shared library instead of static" OFF)
option(KOMARI_ENABLE_LTO "Enable link-time optimization for Release builds" OFF)
option(KOMARI_ENABLE_SANITIZERS "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(KOMARI_ENABLE_COVERAGE "Enable code coverage instrumentation (gcc/clang only)" OFF)
option(KOMARI_ENABLE_PIE "Build position-independent executables" ON)
option(KOMARI_WERROR "Treat compiler warnings as errors" OFF)
option(KOMARI_VERBOSE_CMAKE "Print detailed CMake configuration diagnostics" OFF)
option(KOMARI_ENABLE_CPACK "Enable CPack packaging rules" ${KOMARI_ENABLE_CPACK_DEFAULT})

# Keep BUILD_TESTING in sync with KOMARI_BUILD_TESTS so both flags work.
if(DEFINED KOMARI_BUILD_TESTS)
    set(BUILD_TESTING ${KOMARI_BUILD_TESTS} CACHE BOOL "Build unit tests" FORCE)
elseif(DEFINED BUILD_TESTING)
    set(KOMARI_BUILD_TESTS ${BUILD_TESTING} CACHE BOOL "Build unit tests" FORCE)
endif()

# ---------------------------------------------------------------------------
# Hardening defaults
# ---------------------------------------------------------------------------
option(KOMARI_HARDEN_STACK "Enable -fstack-protector-strong" ON)
option(KOMARI_HARDEN_FORTIFY "Enable -D_FORTIFY_SOURCE=2" ON)
option(KOMARI_HARDEN_FORMAT "Enable -Wformat -Werror=format-security" ON)
option(KOMARI_HARDEN_VISIBILITY "Hide non-exported symbols with -fvisibility=hidden" ON)

# ---------------------------------------------------------------------------
# CCache / compiler cache
# ---------------------------------------------------------------------------
option(KOMARI_ENABLE_CCACHE "Use ccache to speed up rebuilds if available" ON)

# ---------------------------------------------------------------------------
# Cross-compilation helpers
# ---------------------------------------------------------------------------
# When cross-compiling via Docker (linux/$arch), the build script passes
# CMAKE_SYSTEM_NAME and CMAKE_LIBRARY_ARCHITECTURE explicitly. Detect that
# situation here so downstream modules can branch on it.
if(DEFINED CMAKE_SYSTEM_NAME AND NOT CMAKE_SYSTEM_NAME STREQUAL CMAKE_HOST_SYSTEM_NAME)
    set(KOMARI_CROSSCOMPILING ON CACHE BOOL "Cross-compilation detected" FORCE)
else()
    set(KOMARI_CROSSCOMPILING OFF CACHE BOOL "Not cross-compiling" FORCE)
endif()

if(KOMARI_VERBOSE_CMAKE)
    message(STATUS "  KOMARI_CROSSCOMPILING = ${KOMARI_CROSSCOMPILING}")
    message(STATUS "  KOMARI_BUILD_PROFILE  = ${KOMARI_BUILD_PROFILE}")
    message(STATUS "  KOMARI_BUILD_SUBDIR   = ${KOMARI_BUILD_SUBDIR}")
    message(STATUS "  KOMARI_BUILD_TESTS    = ${KOMARI_BUILD_TESTS}")
    message(STATUS "  KOMARI_ENABLE_LTO     = ${KOMARI_ENABLE_LTO}")
    message(STATUS "  KOMARI_WERROR         = ${KOMARI_WERROR}")
    message(STATUS "  KOMARI_ENABLE_CPACK   = ${KOMARI_ENABLE_CPACK}")
endif()
