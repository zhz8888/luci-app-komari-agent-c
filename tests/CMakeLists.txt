# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# Unit test definitions for komari-agent-c. Tests use the Unity v2.5.2
# framework shipped in this directory and link against the komari_core
# static library to avoid recompiling individual source files.

# ---------------------------------------------------------------------------
# Unity test framework
# ---------------------------------------------------------------------------
set(UNITY_SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/unity.c")
set(UNITY_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")

# Unity's TEST_ASSERT_EQUAL_DOUBLE requires -DUNITY_INCLUDE_DOUBLE.
add_definitions(-DUNITY_INCLUDE_DOUBLE)

# ---------------------------------------------------------------------------
# Helper: add a unit test target
# ---------------------------------------------------------------------------
# Usage:
#   komari_add_test(<name>
#       SOURCES <test_source.c> [<extra_source.c>...]
#       LIBS    <extra_lib>...
#   )
#
# The test automatically links against:
#   - Unity framework (unity.c)
#   - komari_core static library (which transitively brings OpenSSL, ZLIB,
#     Threads, and the discovered system libraries)
#   - Math library (-lm)
function(komari_add_test NAME)
    set(options)
    set(one_value_args)
    set(multi_value_args SOURCES LIBS)
    cmake_parse_arguments(KAT "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    # Test executable sources: Unity + caller-provided test source(s)
    set(_sources ${UNITY_SOURCES} ${KAT_SOURCES})

    add_executable(${NAME} ${_sources})

    # Unity headers + public/private komari headers (transitively via komari_core)
    target_include_directories(${NAME} PRIVATE
        ${UNITY_INCLUDE_DIRS}
    )

    # The komari_core target's PUBLIC include dirs already cover include/
    # and include/komari-agent-c, plus its PRIVATE dirs cover all module
    # subdirectories — but PRIVATE dirs do not propagate to consumers.
    # Tests that need to include e.g. "utils.h" directly must do so via
    # the public header (komari-agent-c.h), so we additionally expose the
    # module headers here for backward compatibility with existing tests.
    set(KOMARI_TEST_SUBDIRS
        core config network monitoring report terminal ping
        utils vendor protocol autodiscovery update
    )
    foreach(_dir ${KOMARI_TEST_SUBDIRS})
        target_include_directories(${NAME} PRIVATE
            "${CMAKE_SOURCE_DIR}/src/${_dir}")
    endforeach()

    # Link the static library (transitively pulls OpenSSL/ZLIB/Threads).
    target_link_libraries(${NAME} PRIVATE
        komari_core
        m
        ${KAT_LIBS}
    )

    # Unity test functions are declared in the test source file but have no
    # forward declarations — silence -Wmissing-prototypes for test targets.
    # Also silence -Wstrict-prototypes since Unity's RUN_TEST macro passes
    # function pointers without parameters.
    if(KOMARI_COMPILER_GCC_LIKE)
        target_compile_options(${NAME} PRIVATE
            -Wno-missing-prototypes
            -Wno-strict-prototypes
        )
    endif()

    # Register with CTest
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

# ---------------------------------------------------------------------------
# Test targets
# ---------------------------------------------------------------------------
# Each test links komari_core for the implementation of the modules it
# exercises; only the test driver itself needs to be specified here.

# test_utils — exercises utils.c, logger.c, cJSON.c
komari_add_test(test_utils
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_utils.c"
)

# test_config — exercises config.c + cJSON/utils/logger
komari_add_test(test_config
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_config.c"
)

# test_idn — exercises network/idn.c
komari_add_test(test_idn
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_idn.c"
)

# test_monitoring — exercises monitoring.c + dependencies
komari_add_test(test_monitoring
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_monitoring.c"
)

# test_websocket — exercises websocket.c + protocol stack (OpenSSL/ZLIB)
komari_add_test(test_websocket
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_websocket.c"
)

# test_jsonrpc — exercises jsonrpc.c + cJSON
komari_add_test(test_jsonrpc
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_jsonrpc.c"
)

# test_v2 — exercises v2.c + jsonrpc.c (depends on Threads for v2 state mutex)
komari_add_test(test_v2
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_v2.c"
)

# test_report — exercises report.c + monitoring + protocol (OpenSSL/Threads)
komari_add_test(test_report
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/test_report.c"
)
