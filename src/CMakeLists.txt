# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License

# Use file(GLOB CONFIGURE_DEPENDS) to collect all .c source files (including all subdirectories)
file(GLOB_RECURSE KOMARI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
)

# Source code module subdirectories (used for include path configuration)
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
)

# Configure include paths: public headers + each module subdirectory
set(KOMARI_INCLUDE_DIRS
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/komari-agent-c"
)

foreach(dir ${KOMARI_SUBDIRS})
    list(APPEND KOMARI_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
endforeach()

# Add executable target
add_executable(komari-agent-c ${KOMARI_SOURCES})

# Set include directories
target_include_directories(komari-agent-c PRIVATE ${KOMARI_INCLUDE_DIRS})

# Link libraries: pthread, ssl, crypto, m, resolv, z
target_link_libraries(komari-agent-c PRIVATE
    ${CMAKE_THREAD_LIBS_INIT}
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    m
    resolv
)

# Output to ${CMAKE_BINARY_DIR}/bin/
set_target_properties(komari-agent-c PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Install rules are configured in the root CMakeLists.txt
