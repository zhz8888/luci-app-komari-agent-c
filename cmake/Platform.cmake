# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# Platform detection and cross-compilation safeguards.
# Must be included AFTER project() so CMAKE_C_COMPILER_ID and friends are set.

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Host/target identification
# ---------------------------------------------------------------------------
if(NOT DEFINED KOMARI_HOST_SYSTEM_NAME)
    set(KOMARI_HOST_SYSTEM_NAME "${CMAKE_HOST_SYSTEM_NAME}")
endif()
if(NOT DEFINED KOMARI_TARGET_SYSTEM_NAME)
    set(KOMARI_TARGET_SYSTEM_NAME "${CMAKE_SYSTEM_NAME}")
endif()

if(KOMARI_TARGET_SYSTEM_NAME STREQUAL "Linux")
    set(KOMARI_TARGET_LINUX ON)
elseif(KOMARI_TARGET_SYSTEM_NAME STREQUAL "OpenWrt")
    set(KOMARI_TARGET_OPENWRT ON)
elseif(KOMARI_TARGET_SYSTEM_NAME STREQUAL "Darwin")
    set(KOMARI_TARGET_MACOS ON)
endif()

# Cross-compilation flag (overrides BuildOptions.cmake heuristic when possible)
if(NOT CMAKE_CROSSCOMPILING)
    if(KOMARI_HOST_SYSTEM_NAME AND KOMARI_TARGET_SYSTEM_NAME AND
       NOT KOMARI_HOST_SYSTEM_NAME STREQUAL KOMARI_TARGET_SYSTEM_NAME)
        set(CMAKE_CROSSCOMPILING ON CACHE BOOL "Cross-compiling" FORCE)
    endif()
endif()

message(STATUS "komari-agent-c platform:")
message(STATUS "  Host   = ${KOMARI_HOST_SYSTEM_NAME} (${CMAKE_HOST_SYSTEM_PROCESSOR})")
message(STATUS "  Target = ${KOMARI_TARGET_SYSTEM_NAME} (${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Cross  = ${CMAKE_CROSSCOMPILING}")

# ---------------------------------------------------------------------------
# Compiler identification
# ---------------------------------------------------------------------------
if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    set(KOMARI_COMPILER_GCC ON)
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
    set(KOMARI_COMPILER_CLANG ON)
elseif(CMAKE_C_COMPILER_ID MATCHES "AppleClang")
    set(KOMARI_COMPILER_APPLECLANG ON)
endif()

if(KOMARI_COMPILER_GCC OR KOMARI_COMPILER_CLANG OR KOMARI_COMPILER_APPLECLANG)
    set(KOMARI_COMPILER_GCC_LIKE ON)
endif()

# ---------------------------------------------------------------------------
# C standard enforcement
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Hard fail on unsupported platforms
# ---------------------------------------------------------------------------
# Source files reference Linux-only headers (<syslog.h>, /proc/net/dev,
# forkpty, etc.) and the project's common.h already #errors on non-Linux.
if(NOT KOMARI_TARGET_LINUX AND NOT KOMARI_TARGET_OPENWRT AND NOT KOMARI_TARGET_MACOS)
    message(FATAL_ERROR
        "Unsupported target platform: ${KOMARI_TARGET_SYSTEM_NAME}. "
        "komari-agent-c requires Linux or OpenWrt. "
        "macOS is allowed for syntax/configure checks only.")
endif()

# macOS is explicitly supported only for CMake configure / syntax checks
# (see CLAUDE.md). Real builds happen on Linux/OpenWrt or via Docker/WSL.
if(KOMARI_TARGET_MACOS)
    message(WARNING
        "Building on macOS is supported for CMake configuration checks only. "
        "Linking will likely fail due to missing Linux-specific APIs "
        "(forkpty, /proc filesystem, etc.). Use WSL or Docker for full builds.")
endif()

# ---------------------------------------------------------------------------
# Library architecture detection (multiarch)
# ---------------------------------------------------------------------------
# On Debian/Ubuntu multiarch, libraries live under
# /usr/lib/<multiarch-triplet>/. When cross-compiling via Docker we pass
# CMAKE_LIBRARY_ARCHITECTURE explicitly; otherwise derive it from the
# compiler's default target.
if(NOT CMAKE_LIBRARY_ARCHITECTURE AND KOMARI_COMPILER_GCC_LIKE)
    # Only attempt detection when not cross-compiling — the Docker build
    # script sets this variable explicitly.
    if(NOT CMAKE_CROSSCOMPILING)
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -dumpmachine
            OUTPUT_VARIABLE _gcc_machine
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE _gcc_machine_rc
        )
        if(_gcc_machine_rc EQUAL 0 AND _gcc_machine)
            set(CMAKE_LIBRARY_ARCHITECTURE "${_gcc_machine}" CACHE STRING
                "Library architecture triplet" FORCE)
        endif()
    endif()
endif()

if(KOMARI_VERBOSE_CMAKE AND CMAKE_LIBRARY_ARCHITECTURE)
    message(STATUS "  CMAKE_LIBRARY_ARCHITECTURE = ${CMAKE_LIBRARY_ARCHITECTURE}")
endif()

# ---------------------------------------------------------------------------
# Mark variables as advanced so they don't clutter the ccmake view.
# ---------------------------------------------------------------------------
mark_as_advanced(
    KOMARI_HOST_SYSTEM_NAME
    KOMARI_TARGET_SYSTEM_NAME
    KOMARI_TARGET_LINUX
    KOMARI_TARGET_OPENWRT
    KOMARI_TARGET_MACOS
    KOMARI_COMPILER_GCC
    KOMARI_COMPILER_CLANG
    KOMARI_COMPILER_APPLECLANG
    KOMARI_COMPILER_GCC_LIKE
)
