# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# Build configuration for the komari-agent-c executable and the
# komari_core static library that backs it (and the unit tests).

# ---------------------------------------------------------------------------
# Source tree layout
# ---------------------------------------------------------------------------
# Module subdirectories under src/. The list is also used to set up include
# paths so each module can include its sibling headers directly.
set(KOMARI_SUBDIRS
    core
    config
    network
    monitoring
    report
    terminal
    ping
    utils
    vendor
    protocol
    autodiscovery
    update
    platform
)

# ---------------------------------------------------------------------------
# Source file collection
# ---------------------------------------------------------------------------
# CONFIGURE_DEPENDS makes CMake re-scan the glob when files are added or
# removed, so developers do not need to re-run cmake manually.
file(GLOB_RECURSE KOMARI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

# Separate main.c from the rest: it is the executable entry point and
# must NOT be linked into the komari_core library (otherwise tests would
# end up with two main() symbols).
list(FILTER KOMARI_SOURCES EXCLUDE REGEX "${CMAKE_CURRENT_SOURCE_DIR}/core/main\.c$")
set(KOMARI_MAIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/core/main.c")

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------
# Public headers (installed) live under include/. Module-private headers
# live next to their .c files and are only visible to the komari_core
# target via target_include_directories below.
set(KOMARI_PUBLIC_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/komari-agent-c"
)

set(KOMARI_PRIVATE_INCLUDE_DIRS "")
foreach(dir ${KOMARI_SUBDIRS})
    list(APPEND KOMARI_PRIVATE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
endforeach()

# ---------------------------------------------------------------------------
# komari_core library
# ---------------------------------------------------------------------------
# A static library is used so the unit tests can link against the same
# code as the executable without recompiling individual .c files. When
# KOMARI_BUILD_SHARED is ON, a shared library is produced instead — this
# is mainly useful for downstream consumers that want to embed the agent.
if(KOMARI_BUILD_SHARED)
    add_library(komari_core SHARED ${KOMARI_SOURCES})
else()
    add_library(komari_core STATIC ${KOMARI_SOURCES})
endif()

target_include_directories(komari_core PUBLIC
    ${KOMARI_PUBLIC_INCLUDE_DIRS}
)
target_include_directories(komari_core PRIVATE
    ${KOMARI_PRIVATE_INCLUDE_DIRS}
)

# Position-independent code is required for the shared library variant and
# also enables PIE on the final executable.
set_target_properties(komari_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)

# Public link dependencies are exposed to consumers of komari_core; the
# system libraries (m, util, resolv, rt) are private implementation
# details and should not leak through.
target_link_libraries(komari_core PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads
)
target_link_libraries(komari_core PRIVATE
    ${KOMARI_SYSTEM_LIBS}
)

# Hide non-exported symbols when building a shared library.
if(KOMARI_BUILD_SHARED AND KOMARI_HARDEN_VISIBILITY)
    target_compile_options(komari_core PRIVATE -fvisibility=hidden)
endif()

# ---------------------------------------------------------------------------
# komari-agent-c executable
# ---------------------------------------------------------------------------
add_executable(komari-agent-c ${KOMARI_MAIN_SOURCE})

# main.c directly includes module-private headers (config.h, monitoring.h,
# etc.) which are NOT exposed via komari_core's PUBLIC include dirs. Add
# them here as PRIVATE so the executable can compile but consumers of
# komari_core do not see them.
target_include_directories(komari-agent-c PRIVATE
    ${KOMARI_PUBLIC_INCLUDE_DIRS}
    ${KOMARI_PRIVATE_INCLUDE_DIRS}
)

target_link_libraries(komari-agent-c PRIVATE komari_core)

set_target_properties(komari-agent-c PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
)

# Convenience alias so developers can run `cmake --build build --target komari`
add_custom_target(komari DEPENDS komari-agent-c)

# ---------------------------------------------------------------------------
# Source file grouping for IDEs (Visual Studio, Xcode, CLion, etc.)
# ---------------------------------------------------------------------------
function(komari_group_sources TARGET)
    foreach(source IN LISTS ARGN)
        # Strip the leading src/ so group names are clean.
        file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${source}")
        get_filename_component(_dir "${_rel}" DIRECTORY)
        if(_dir)
            string(REPLACE "/" "\\\\" _group "${_dir}")
        else()
            set(_group "")
        endif()
        source_group("${_group}" FILES "${source}")
    endforeach()
endfunction()

komari_group_sources(komari_core ${KOMARI_SOURCES})
komari_group_sources(komari-agent-c ${KOMARI_MAIN_SOURCE})

# ---------------------------------------------------------------------------
# Install rules are configured in the top-level CMakeLists.txt
# ---------------------------------------------------------------------------
