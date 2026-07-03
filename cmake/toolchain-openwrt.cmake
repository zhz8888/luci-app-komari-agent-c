# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# OpenWrt SDK cross-compilation toolchain file.
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-openwrt.cmake ..
#   cmake --preset openwrt
#
# OpenWrt SDK environment variables must be set in advance (by sourcing the
# OpenWrt SDK env file):
#   - TARGET_CC        C compiler path
#   - TARGET_CXX       C++ compiler path (optional)
#   - TARGET_CFLAGS    C compiler flags
#   - TARGET_LDFLAGS   Linker flags
#   - STAGING_DIR      OpenWrt SDK staging directory path

# ---------------------------------------------------------------------------
# Sanity checks — fail fast with a clear message instead of a cryptic error
# deep inside CMake's compiler detection.
# ---------------------------------------------------------------------------
if(NOT DEFINED ENV{TARGET_CC})
    message(FATAL_ERROR
        "Environment variable TARGET_CC is not set.\n"
        "Please source the OpenWrt SDK environment file first, e.g.:\n"
        "  . /path/to/openwrt-sdk/env.sh\n"
        "or set TARGET_CC manually to the cross-compiler binary.")
endif()

if(NOT DEFINED ENV{STAGING_DIR})
    message(WARNING
        "Environment variable STAGING_DIR is not set. "
        "CMake will attempt to derive the staging directory from TARGET_CC, "
        "but library/header discovery may fail.")
endif()

# ---------------------------------------------------------------------------
# System identification
# ---------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME       Linux)
set(CMAKE_SYSTEM_PROCESSOR  "$ENV{ARCH}" CACHE STRING "Target processor")

# Try to infer the processor architecture from the compiler triplet when
# ARCH is not exported by the OpenWrt SDK env.
if(NOT CMAKE_SYSTEM_PROCESSOR OR CMAKE_SYSTEM_PROCESSOR STREQUAL "")
    execute_process(
        COMMAND "$ENV{TARGET_CC}" -dumpmachine
        OUTPUT_VARIABLE _openwrt_triplet
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _openwrt_triplet_rc
    )
    if(_openwrt_triplet_rc EQUAL 0 AND _openwrt_triplet)
        # Extract the architecture prefix (e.g. "mipsel" from "mipsel-openwrt-linux-musl")
        string(REGEX MATCH "^([^-]+)" _openwrt_arch "${_openwrt_triplet}")
        set(CMAKE_SYSTEM_PROCESSOR "${_openwrt_arch}" CACHE STRING "Target processor" FORCE)
    endif()
endif()

# ---------------------------------------------------------------------------
# Compiler configuration
# ---------------------------------------------------------------------------
set(CMAKE_C_COMPILER "$ENV{TARGET_CC}" CACHE PATH "C compiler" FORCE)

if(DEFINED ENV{TARGET_CXX})
    set(CMAKE_CXX_COMPILER "$ENV{TARGET_CXX}" CACHE PATH "C++ compiler" FORCE)
endif()

# Append user-supplied flags to the CMake-managed flag variables. We use
# string(APPEND) here (not FORCE) so subsequent -D overrides on the
# command line still take precedence.
if(DEFINED ENV{TARGET_CFLAGS})
    string(APPEND CMAKE_C_FLAGS " $ENV{TARGET_CFLAGS}")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}" CACHE STRING "C compiler flags" FORCE)
endif()

if(DEFINED ENV{TARGET_LDFLAGS})
    string(APPEND CMAKE_EXE_LINKER_FLAGS " $ENV{TARGET_LDFLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" CACHE STRING "Executable linker flags" FORCE)
endif()

# ---------------------------------------------------------------------------
# Sysroot / find path configuration
# ---------------------------------------------------------------------------
# Prefer STAGING_DIR; otherwise derive from TARGET_CC path.
# Typical OpenWrt SDK layout:
#   <sdk>/staging_dir/<target>/bin/<tuple>-gcc
#   <sdk>/staging_dir/<target>/usr/lib/libssl.so
#   <sdk>/staging_dir/<target>/usr/include/openssl/ssl.h
if(DEFINED ENV{STAGING_DIR})
    set(CMAKE_FIND_ROOT_PATH "$ENV{STAGING_DIR}" CACHE PATH "Cross-compilation root path" FORCE)
elseif(DEFINED ENV{TARGET_CC})
    get_filename_component(_cc_path "$ENV{TARGET_CC}" DIRECTORY)
    get_filename_component(_cc_parent "${_cc_path}/.." ABSOLUTE)
    set(CMAKE_FIND_ROOT_PATH "${_cc_parent}" CACHE PATH "Cross-compilation root path" FORCE)
    message(STATUS "OpenWrt toolchain: derived CMAKE_FIND_ROOT_PATH = ${_cc_parent}")
endif()

# Search mode configuration:
#   PROGRAM   -> never use root path (host tools only)
#   LIBRARY   -> only use root path (target libraries)
#   INCLUDE   -> only use root path (target headers)
#   PACKAGE   -> only use root path (target CMake packages)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# ---------------------------------------------------------------------------
# OpenWrt-specific path hints (staging_dir layout)
# ---------------------------------------------------------------------------
# OpenWrt SDK installs libraries and headers under staging_dir/<target>/usr
# rather than the root of staging_dir. Add these as additional search paths
# so find_package(OpenSSL) / find_package(ZLIB) work out of the box.
if(DEFINED ENV{STAGING_DIR})
    list(APPEND CMAKE_LIBRARY_PATH
        "$ENV{STAGING_DIR}/usr/lib"
        "$ENV{STAGING_DIR}/lib"
    )
    list(APPEND CMAKE_INCLUDE_PATH
        "$ENV{STAGING_DIR}/usr/include"
        "$ENV{STAGING_DIR}/include"
    )
endif()

# ---------------------------------------------------------------------------
# Mark toolchain-internal variables as advanced to keep ccmake/gui clean.
# ---------------------------------------------------------------------------
mark_as_advanced(
    CMAKE_C_COMPILER
    CMAKE_CXX_COMPILER
    CMAKE_FIND_ROOT_PATH
    CMAKE_FIND_ROOT_PATH_MODE_PROGRAM
    CMAKE_FIND_ROOT_PATH_MODE_LIBRARY
    CMAKE_FIND_ROOT_PATH_MODE_INCLUDE
    CMAKE_FIND_ROOT_PATH_MODE_PACKAGE
)

# Verbose diagnostics when KOMARI_VERBOSE_CMAKE is enabled.
if(DEFINED KOMARI_VERBOSE_CMAKE AND KOMARI_VERBOSE_CMAKE)
    message(STATUS "==== OpenWrt toolchain configuration ====")
    message(STATUS "  CMAKE_C_COMPILER       = ${CMAKE_C_COMPILER}")
    message(STATUS "  CMAKE_CXX_COMPILER     = ${CMAKE_CXX_COMPILER}")
    message(STATUS "  CMAKE_SYSTEM_NAME      = ${CMAKE_SYSTEM_NAME}")
    message(STATUS "  CMAKE_SYSTEM_PROCESSOR = ${CMAKE_SYSTEM_PROCESSOR}")
    message(STATUS "  CMAKE_FIND_ROOT_PATH   = ${CMAKE_FIND_ROOT_PATH}")
    message(STATUS "  CMAKE_C_FLAGS          = ${CMAKE_C_FLAGS}")
    message(STATUS "  CMAKE_EXE_LINKER_FLAGS = ${CMAKE_EXE_LINKER_FLAGS}")
    message(STATUS "  STAGING_DIR            = $ENV{STAGING_DIR}")
    message(STATUS "==========================================")
endif()
