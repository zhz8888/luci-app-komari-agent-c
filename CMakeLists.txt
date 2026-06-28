# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License

cmake_minimum_required(VERSION 3.10)

# Read version number from include/komari-agent-c/version.h
set(_VERSION_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/komari-agent-c/version.h")

if(NOT EXISTS "${_VERSION_HEADER}")
    message(FATAL_ERROR "Version header file not found: ${_VERSION_HEADER}")
endif()

file(READ "${_VERSION_HEADER}" _VERSION_CONTENT)

# Extract version number using regex
string(REGEX MATCH "KOMARI_AGENT_C_VERSION_MAJOR[ \t]+([0-9]+)" _MATCH "${_VERSION_CONTENT}")
set(KOMARI_AGENT_C_VERSION_MAJOR "${CMAKE_MATCH_1}")

string(REGEX MATCH "KOMARI_AGENT_C_VERSION_MINOR[ \t]+([0-9]+)" _MATCH "${_VERSION_CONTENT}")
set(KOMARI_AGENT_C_VERSION_MINOR "${CMAKE_MATCH_1}")

string(REGEX MATCH "KOMARI_AGENT_C_VERSION_PATCH[ \t]+([0-9]+)" _MATCH "${_VERSION_CONTENT}")
set(KOMARI_AGENT_C_VERSION_PATCH "${CMAKE_MATCH_1}")

string(REGEX MATCH "KOMARI_AGENT_C_VERSION_STRING[ \t]+\"([^\"]+)\"" _MATCH "${_VERSION_CONTENT}")
set(KOMARI_AGENT_C_VERSION_STRING "${CMAKE_MATCH_1}")

# Note: Cannot use NOT check, because CMake treats string "0" as false
if(KOMARI_AGENT_C_VERSION_MAJOR STREQUAL "" OR KOMARI_AGENT_C_VERSION_MINOR STREQUAL "" OR KOMARI_AGENT_C_VERSION_PATCH STREQUAL "")
    message(FATAL_ERROR "Failed to parse version number from version.h")
endif()

set(KOMARI_AGENT_C_VERSION "${KOMARI_AGENT_C_VERSION_MAJOR}.${KOMARI_AGENT_C_VERSION_MINOR}.${KOMARI_AGENT_C_VERSION_PATCH}")

project(komari-agent-c
    VERSION "${KOMARI_AGENT_C_VERSION}"
    LANGUAGES C
)

message(STATUS "Komari Agent version: ${KOMARI_AGENT_C_VERSION} (string: ${KOMARI_AGENT_C_VERSION_STRING})")

# C standard: C99
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Default build type is Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Compilation flags (unified requirements)
set(KOMARI_C_FLAGS "-std=c99 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra")

# Convert space-separated string to CMake list to avoid add_compile_options treating it as a single argument
separate_arguments(KOMARI_C_FLAGS UNIX_COMMAND "${KOMARI_C_FLAGS}")

# Check if compiler supports GCC-style flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(${KOMARI_C_FLAGS})
else()
    # Other compilers only add recognizable flags
    add_compile_options(-Wall -Wextra)
endif()

# Dependency lookup
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Testing switch
option(BUILD_TESTING "Build unit tests" ON)

# Subdirectories
add_subdirectory(src)

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install targets
install(TARGETS komari-agent-c
    RUNTIME DESTINATION /usr/bin
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/openwrt/files/komari-agent-c.init"
    DESTINATION /etc/init.d
    RENAME komari-agent-c
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/openwrt/files/komari-agent-c.config"
    DESTINATION /etc/config
    RENAME komari-agent-c
)
