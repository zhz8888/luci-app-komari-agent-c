# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License

# Unity test framework source files
set(UNITY_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/unity.c"
)

set(UNITY_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Compilation flags (consistent with the main project)
set(KOMARI_TEST_C_FLAGS "-std=c99 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -DUNITY_INCLUDE_DOUBLE -Wall -Wextra")

# Convert space-separated string to CMake list to avoid target_compile_options treating it as a single argument
separate_arguments(KOMARI_TEST_C_FLAGS UNIX_COMMAND "${KOMARI_TEST_C_FLAGS}")

# Check if compiler supports GCC-style flags
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(KOMARI_TEST_C_FLAGS_LIST ${KOMARI_TEST_C_FLAGS})
else()
    set(KOMARI_TEST_C_FLAGS_LIST -Wall -Wextra)
endif()

# Common test dependency source files (linked as needed)
# Each test declares its dependent source file list for easy future extension

# ====== Test target definitions ======

# Test target: test_utils
# Links ../src/utils/utils.c, ../src/utils/logger.c and ../src/vendor/cJSON.c
add_executable(test_utils
    ${UNITY_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/test_utils.c"
    "${CMAKE_SOURCE_DIR}/src/utils/utils.c"
    "${CMAKE_SOURCE_DIR}/src/utils/logger.c"
    "${CMAKE_SOURCE_DIR}/src/vendor/cJSON.c"
)

target_include_directories(test_utils PRIVATE
    ${UNITY_INCLUDE_DIRS}
    "${CMAKE_SOURCE_DIR}/src/utils"
    "${CMAKE_SOURCE_DIR}/src/vendor"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/komari-agent-c"
)

target_link_libraries(test_utils PRIVATE m)

target_compile_options(test_utils PRIVATE ${KOMARI_TEST_C_FLAGS_LIST})

add_test(NAME test_utils COMMAND test_utils)

# ====== Test target: test_config ======
# Links config.c, cJSON.c, utils.c, logger.c
add_executable(test_config
    ${UNITY_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/test_config.c"
    "${CMAKE_SOURCE_DIR}/src/config/config.c"
    "${CMAKE_SOURCE_DIR}/src/vendor/cJSON.c"
    "${CMAKE_SOURCE_DIR}/src/utils/utils.c"
    "${CMAKE_SOURCE_DIR}/src/utils/logger.c"
)

target_include_directories(test_config PRIVATE
    ${UNITY_INCLUDE_DIRS}
    "${CMAKE_SOURCE_DIR}/src/config"
    "${CMAKE_SOURCE_DIR}/src/vendor"
    "${CMAKE_SOURCE_DIR}/src/utils"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/komari-agent-c"
)

target_link_libraries(test_config PRIVATE m)

target_compile_options(test_config PRIVATE ${KOMARI_TEST_C_FLAGS_LIST})

add_test(NAME test_config COMMAND test_config)

# ====== Test target: test_idn ======
# Links idn.c
add_executable(test_idn
    ${UNITY_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/test_idn.c"
    "${CMAKE_SOURCE_DIR}/src/network/idn.c"
)

target_include_directories(test_idn PRIVATE
    ${UNITY_INCLUDE_DIRS}
    "${CMAKE_SOURCE_DIR}/src/network"
    "${CMAKE_SOURCE_DIR}/src/utils"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/komari-agent-c"
)

target_link_libraries(test_idn PRIVATE m)

target_compile_options(test_idn PRIVATE ${KOMARI_TEST_C_FLAGS_LIST})

add_test(NAME test_idn COMMAND test_idn)

# ====== Test target: test_monitoring ======
# Links monitoring.c, cJSON.c, utils.c, logger.c, config.c
add_executable(test_monitoring
    ${UNITY_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/test_monitoring.c"
    "${CMAKE_SOURCE_DIR}/src/monitoring/monitoring.c"
    "${CMAKE_SOURCE_DIR}/src/vendor/cJSON.c"
    "${CMAKE_SOURCE_DIR}/src/utils/utils.c"
    "${CMAKE_SOURCE_DIR}/src/utils/logger.c"
    "${CMAKE_SOURCE_DIR}/src/config/config.c"
)

target_include_directories(test_monitoring PRIVATE
    ${UNITY_INCLUDE_DIRS}
    "${CMAKE_SOURCE_DIR}/src/monitoring"
    "${CMAKE_SOURCE_DIR}/src/config"
    "${CMAKE_SOURCE_DIR}/src/vendor"
    "${CMAKE_SOURCE_DIR}/src/utils"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/komari-agent-c"
)

target_link_libraries(test_monitoring PRIVATE m)

target_compile_options(test_monitoring PRIVATE ${KOMARI_TEST_C_FLAGS_LIST})

add_test(NAME test_monitoring COMMAND test_monitoring)

# ====== Test target: test_websocket ======
# Links websocket.c, cJSON.c, utils.c, logger.c, v2.c, v1.c, compress.c, jsonrpc.c; depends on OpenSSL, ZLIB and Threads
add_executable(test_websocket
    ${UNITY_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/test_websocket.c"
    "${CMAKE_SOURCE_DIR}/src/network/websocket.c"
    "${CMAKE_SOURCE_DIR}/src/vendor/cJSON.c"
    "${CMAKE_SOURCE_DIR}/src/utils/utils.c"
    "${CMAKE_SOURCE_DIR}/src/utils/logger.c"
    "${CMAKE_SOURCE_DIR}/src/protocol/v2.c"
    "${CMAKE_SOURCE_DIR}/src/protocol/v1.c"
    "${CMAKE_SOURCE_DIR}/src/protocol/compress.c"
    "${CMAKE_SOURCE_DIR}/src/protocol/jsonrpc.c"
)

target_include_directories(test_websocket PRIVATE
    ${UNITY_INCLUDE_DIRS}
    "${CMAKE_SOURCE_DIR}/src/network"
    "${CMAKE_SOURCE_DIR}/src/config"
    "${CMAKE_SOURCE_DIR}/src/monitoring"
    "${CMAKE_SOURCE_DIR}/src/protocol"
    "${CMAKE_SOURCE_DIR}/src/vendor"
    "${CMAKE_SOURCE_DIR}/src/utils"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/komari-agent-c"
)

target_link_libraries(test_websocket PRIVATE
    m
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads
)

target_compile_options(test_websocket PRIVATE ${KOMARI_TEST_C_FLAGS_LIST})

add_test(NAME test_websocket COMMAND test_websocket)

# ====== Test target: test_jsonrpc ======
# Links jsonrpc.c, cJSON.c
add_executable(test_jsonrpc
    ${UNITY_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/test_jsonrpc.c"
    "${CMAKE_SOURCE_DIR}/src/protocol/jsonrpc.c"
    "${CMAKE_SOURCE_DIR}/src/vendor/cJSON.c"
)

target_include_directories(test_jsonrpc PRIVATE
    ${UNITY_INCLUDE_DIRS}
    "${CMAKE_SOURCE_DIR}/src/protocol"
    "${CMAKE_SOURCE_DIR}/src/vendor"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/komari-agent-c"
)

target_link_libraries(test_jsonrpc PRIVATE m)

target_compile_options(test_jsonrpc PRIVATE ${KOMARI_TEST_C_FLAGS_LIST})

add_test(NAME test_jsonrpc COMMAND test_jsonrpc)

# ====== Test target: test_v2 ======
# Links v2.c, jsonrpc.c, cJSON.c; depends on Threads (v2 state uses pthread mutex)
add_executable(test_v2
    ${UNITY_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/test_v2.c"
    "${CMAKE_SOURCE_DIR}/src/protocol/v2.c"
    "${CMAKE_SOURCE_DIR}/src/protocol/jsonrpc.c"
    "${CMAKE_SOURCE_DIR}/src/vendor/cJSON.c"
)

target_include_directories(test_v2 PRIVATE
    ${UNITY_INCLUDE_DIRS}
    "${CMAKE_SOURCE_DIR}/src/protocol"
    "${CMAKE_SOURCE_DIR}/src/vendor"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/komari-agent-c"
)

target_link_libraries(test_v2 PRIVATE
    m
    Threads::Threads
)

target_compile_options(test_v2 PRIVATE ${KOMARI_TEST_C_FLAGS_LIST})

add_test(NAME test_v2 COMMAND test_v2)

# ====== Test target: test_report ======
# Links report.c, monitoring.c, cJSON.c, utils.c, virtual.c, logger.c, config.c, gpu.c,
# v2.c, jsonrpc.c (for report_generate_v2 / report_generate_basic_info_v2); depends on OpenSSL, Threads
add_executable(test_report
    ${UNITY_SOURCES}
    "${CMAKE_CURRENT_SOURCE_DIR}/test_report.c"
    "${CMAKE_SOURCE_DIR}/src/report/report.c"
    "${CMAKE_SOURCE_DIR}/src/monitoring/monitoring.c"
    "${CMAKE_SOURCE_DIR}/src/monitoring/gpu.c"
    "${CMAKE_SOURCE_DIR}/src/vendor/cJSON.c"
    "${CMAKE_SOURCE_DIR}/src/utils/utils.c"
    "${CMAKE_SOURCE_DIR}/src/utils/virtual.c"
    "${CMAKE_SOURCE_DIR}/src/utils/logger.c"
    "${CMAKE_SOURCE_DIR}/src/config/config.c"
    "${CMAKE_SOURCE_DIR}/src/protocol/v2.c"
    "${CMAKE_SOURCE_DIR}/src/protocol/jsonrpc.c"
)

target_include_directories(test_report PRIVATE
    ${UNITY_INCLUDE_DIRS}
    "${CMAKE_SOURCE_DIR}/src/report"
    "${CMAKE_SOURCE_DIR}/src/monitoring"
    "${CMAKE_SOURCE_DIR}/src/config"
    "${CMAKE_SOURCE_DIR}/src/protocol"
    "${CMAKE_SOURCE_DIR}/src/vendor"
    "${CMAKE_SOURCE_DIR}/src/utils"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/include/komari-agent-c"
)

target_link_libraries(test_report PRIVATE
    m
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

target_compile_options(test_report PRIVATE ${KOMARI_TEST_C_FLAGS_LIST})

add_test(NAME test_report COMMAND test_report)
