# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# Parse the project version from include/komari-agent-c/version.h.
# This is invoked BEFORE project() so the version can be propagated to
# the project() command and into CPack / install metadata.

include_guard(GLOBAL)

function(komari_parse_version HEADER_PATH)
    if(NOT EXISTS "${HEADER_PATH}")
        message(FATAL_ERROR "Version header not found: ${HEADER_PATH}")
    endif()

    file(READ "${HEADER_PATH}" _version_content)

    string(REGEX MATCH "KOMARI_AGENT_C_VERSION_MAJOR[ \t]+([0-9]+)" _match "${_version_content}")
    set(_major "${CMAKE_MATCH_1}")

    string(REGEX MATCH "KOMARI_AGENT_C_VERSION_MINOR[ \t]+([0-9]+)" _match "${_version_content}")
    set(_minor "${CMAKE_MATCH_1}")

    string(REGEX MATCH "KOMARI_AGENT_C_VERSION_PATCH[ \t]+([0-9]+)" _match "${_version_content}")
    set(_patch "${CMAKE_MATCH_1}")

    string(REGEX MATCH "KOMARI_AGENT_C_VERSION_STRING[ \t]+\"([^\"]+)\"" _match "${_version_content}")
    set(_string "${CMAKE_MATCH_1}")

    # Cannot use NOT check — CMake treats "0" as false.
    if(_major STREQUAL "" OR _minor STREQUAL "" OR _patch STREQUAL "")
        message(FATAL_ERROR "Failed to parse version from ${HEADER_PATH}")
    endif()

    set(KOMARI_AGENT_C_VERSION_MAJOR "${_major}" PARENT_SCOPE)
    set(KOMARI_AGENT_C_VERSION_MINOR "${_minor}" PARENT_SCOPE)
    set(KOMARI_AGENT_C_VERSION_PATCH "${_patch}" PARENT_SCOPE)
    set(KOMARI_AGENT_C_VERSION_STRING "${_string}" PARENT_SCOPE)
    set(KOMARI_AGENT_C_VERSION "${_major}.${_minor}.${_patch}" PARENT_SCOPE)

    message(STATUS "Komari Agent version: ${_major}.${_minor}.${_patch} (string: ${_string})")
endfunction()
