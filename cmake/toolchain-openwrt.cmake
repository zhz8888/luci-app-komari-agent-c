# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# OpenWrt SDK cross-compilation toolchain file
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-openwrt.cmake ..
#
# OpenWrt SDK environment variables must be set in advance (by sourcing the OpenWrt SDK env file):
#   - TARGET_CC:        C compiler path
#   - TARGET_CXX:       C++ compiler path (optional)
#   - TARGET_CFLAGS:    C compiler flags
#   - TARGET_LDFLAGS:   Linker flags
#   - STAGING_DIR:      OpenWrt SDK staging directory path

# Read environment variable TARGET_CC
if(DEFINED ENV{TARGET_CC})
    set(CMAKE_C_COMPILER "$ENV{TARGET_CC}" CACHE PATH "C compiler" FORCE)
else()
    message(FATAL_ERROR "Environment variable TARGET_CC is not set, please source the OpenWrt SDK env file first")
endif()

# Read environment variable TARGET_CXX (if present)
if(DEFINED ENV{TARGET_CXX})
    set(CMAKE_CXX_COMPILER "$ENV{TARGET_CXX}" CACHE PATH "C++ compiler" FORCE)
endif()

# Read environment variable TARGET_CFLAGS
if(DEFINED ENV{TARGET_CFLAGS})
    set(CMAKE_C_FLAGS "$ENV{TARGET_CFLAGS}" CACHE STRING "C compiler flags" FORCE)
endif()

# Read environment variable TARGET_LDFLAGS
if(DEFINED ENV{TARGET_LDFLAGS})
    set(CMAKE_EXE_LINKER_FLAGS "$ENV{TARGET_LDFLAGS}" CACHE STRING "Executable linker flags" FORCE)
endif()

# Set CMAKE_FIND_ROOT_PATH to the staging_dir path
# Prefer STAGING_DIR environment variable, otherwise derive from TARGET_CC
if(DEFINED ENV{STAGING_DIR})
    set(CMAKE_FIND_ROOT_PATH "$ENV{STAGING_DIR}" CACHE PATH "Cross-compilation root path" FORCE)
elseif(DEFINED ENV{TARGET_CC})
    # Try to derive staging_dir from TARGET_CC path
    # Typical path: <sdk>/staging_dir/<target>/bin/<tuple>-gcc
    get_filename_component(_CC_PATH "$ENV{TARGET_CC}" DIRECTORY)
    get_filename_component(_CC_PARENT "${_CC_PATH}/.." ABSOLUTE)
    set(CMAKE_FIND_ROOT_PATH "${_CC_PARENT}" CACHE PATH "Cross-compilation root path" FORCE)
endif()

# Set search modes
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
