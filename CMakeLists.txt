# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# Top-level CMake configuration for komari-agent-c.
#
# Quick start:
#   cmake -B build && cmake --build build          # default Release
#   cmake -B build -DCMAKE_BUILD_TYPE=Debug        # debug build
#   cmake -B build -DKOMARI_ENABLE_SANITIZERS=ON   # ASan + UBSan
#   cmake -B build -DKOMARI_ENABLE_CCACHE=ON       # use ccache if available
#   cmake -B build -DKOMARI_BUILD_TESTS=OFF        # skip tests
#
# For standardized presets see CMakePresets.json.

cmake_minimum_required(VERSION 3.10)

# ---------------------------------------------------------------------------
# Load user-facing options BEFORE project() so they can influence toolchain.
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(BuildOptions)

# ---------------------------------------------------------------------------
# Parse version from include/komari-agent-c/version.h
# ---------------------------------------------------------------------------
include(Version)
komari_parse_version("${CMAKE_CURRENT_SOURCE_DIR}/include/komari-agent-c/version.h")

project(komari-agent-c
    VERSION "${KOMARI_AGENT_C_VERSION}"
    LANGUAGES C
    DESCRIPTION "Komari monitoring agent (C implementation)"
    HOMEPAGE_URL "https://github.com/zhz8888/luci-app-komari-agent-c"
)

# ---------------------------------------------------------------------------
# Post-project platform detection and dependency discovery
# ---------------------------------------------------------------------------
include(Platform)
include(CompilerFlags)
include(Dependencies)

# ---------------------------------------------------------------------------
# Global output directories (kept in build tree, also used by tests)
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---------------------------------------------------------------------------
# Default install layout (GNU-style)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(src)

if(KOMARI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
# Install rules are shared between the binary and openwrt profiles; the
# OpenWrt SDK's Build/Compile step ignores CMAKE_INSTALL_PREFIX and uses
# its own Package/install define instead, so these rules only matter for
# standalone `cmake --install` usage.
install(TARGETS komari-agent-c
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/openwrt/files/komari-agent-c.init"
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/init.d
    RENAME komari-agent-c
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/openwrt/files/komari-agent-c.config"
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/config
    RENAME komari-agent-c
)

# ---------------------------------------------------------------------------
# CPack packaging (DEB / RPM / TGZ) — binary profile only
# ---------------------------------------------------------------------------
# The OpenWrt SDK produces .ipk/.apk packages via its own packager, so
# CPack is disabled in the openwrt profile to avoid duplicate packaging
# rules and potential conflicts.
if(KOMARI_ENABLE_CPACK)
    include(CPack)

    set(CPACK_PACKAGE_NAME              "komari-agent-c")
    set(CPACK_PACKAGE_VERSION           "${KOMARI_AGENT_C_VERSION_STRING}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Komari monitoring agent (C implementation)")
    set(CPACK_PACKAGE_HOMEPAGE_URL      "https://github.com/zhz8888/luci-app-komari-agent-c")
    set(CPACK_PACKAGE_CONTACT           "zhz8888")
    set(CPACK_PACKAGE_LICENSE           "MIT")
    set(CPACK_RESOURCE_FILE_LICENSE      "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_STRIP_FILES                ON)
    set(CPACK_PACKAGE_EXECUTABLES        "komari-agent-c;komari-agent-c")

    # Detect target architecture for package naming
    if(CMAKE_SYSTEM_PROCESSOR)
        set(CPACK_PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
    else()
        set(CPACK_PACKAGE_ARCH "unknown")
    endif()

    # Component layout — single "runtime" component for the agent binary
    set(CPACK_COMPONENTS_ALL runtime)
    set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Komari Agent runtime")

    # Source package configuration (TGZ by default; DEB/RPM when available)
    set(CPACK_GENERATOR "TGZ")
    find_program(DPKG_BUILDER dpkg-buildpackage)
    if(DPKG_BUILDER)
        list(APPEND CPACK_GENERATOR "DEB")
    endif()
    find_program(RPMBUILDER rpmbuild)
    if(RPMBUILDER)
        list(APPEND CPACK_GENERATOR "RPM")
    endif()

    # DEB-specific
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CPACK_PACKAGE_ARCH}")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER   "zhz8888 <zhz8888@users.noreply.github.com>")
    set(CPACK_DEBIAN_PACKAGE_SECTION      "net")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY     "optional")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS      "libssl3 | libssl1.1, zlib1g (>= 1:1.2.11)")

    # RPM-specific
    set(CPACK_RPM_PACKAGE_ARCHITECTURE    "${CPACK_PACKAGE_ARCH}")
    set(CPACK_RPM_PACKAGE_LICENSE         "MIT")
    set(CPACK_RPM_PACKAGE_GROUP           "Applications/Internet")
    set(CPACK_RPM_PACKAGE_REQUIRES        "openssl >= 1.1.0, zlib >= 1.2.11")
endif()

# Display final configuration summary
message(STATUS "")
message(STATUS "==== komari-agent-c configuration summary ====")
message(STATUS "  Version:        ${KOMARI_AGENT_C_VERSION_STRING}")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Profile:        ${KOMARI_BUILD_PROFILE} (subdir: ${KOMARI_BUILD_SUBDIR})")
message(STATUS "  Compiler:       ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "  Target:         ${CMAKE_SYSTEM_NAME} (${CMAKE_SYSTEM_PROCESSOR})")
message(STATUS "  Cross-comp:     ${CMAKE_CROSSCOMPILING}")
message(STATUS "  Tests:          ${KOMARI_BUILD_TESTS}")
message(STATUS "  CPack:          ${KOMARI_ENABLE_CPACK}")
message(STATUS "  Sanitizers:     ${KOMARI_ENABLE_SANITIZERS}")
message(STATUS "  Coverage:       ${KOMARI_ENABLE_COVERAGE}")
message(STATUS "  LTO:            ${KOMARI_ENABLE_LTO}")
message(STATUS "  PIE:            ${KOMARI_ENABLE_PIE}")
message(STATUS "  Werror:         ${KOMARI_WERROR}")
message(STATUS "  ccache:         ${KOMARI_ENABLE_CCACHE} (${CCACHE_PROGRAM})")
message(STATUS "  OpenSSL:        ${OPENSSL_VERSION}")
message(STATUS "  ZLIB:           ${ZLIB_VERSION_STRING}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "==============================================")
message(STATUS "")
