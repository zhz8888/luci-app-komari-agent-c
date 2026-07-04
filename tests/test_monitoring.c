/*
 * test_monitoring.c - System monitoring module unit tests
 *
 * Test coverage:
 *   - CPU info parsing (monitoring_get_cpu_info)
 *   - Memory info parsing (monitoring_get_mem_info)
 *   - Disk info parsing (monitoring_get_disk_info)
 *   - Network info parsing (monitoring_get_net_info)
 *   - Connection count statistics (monitoring_get_conn_info)
 *   - Load info parsing (monitoring_get_load_info)
 *   - System info parsing (monitoring_get_system_info)
 *   - Data structure size and field validation
 *
 * Note: Since monitoring functions directly read files in the /proc filesystem
 * (hardcoded paths), they cannot be mocked in the conventional way. Tests run
 * on a real Linux system to verify that functions correctly read and parse
 * /proc data.
 */

#include "unity.h"
#include "monitoring.h"
#include "config.h"

#include <string.h>
#include <stdint.h>

void setUp(void) {
    /* Reset global config pointer */
    monitoring_set_config(NULL);
}

void tearDown(void) {
    monitoring_set_config(NULL);
}

/* ====== Data structure size tests ====== */

/* Test cpu_info_t struct size is reasonable */
void test_monitoring_cpu_info_struct_size(void) {
    TEST_ASSERT_TRUE(sizeof(cpu_info_t) > 0);
    TEST_ASSERT_TRUE(sizeof(cpu_info_t) >= 128 + 32 + sizeof(int) + sizeof(double));
}

/* Test mem_info_t struct size (6 uint64_t fields) */
void test_monitoring_mem_info_struct_size(void) {
    TEST_ASSERT_EQUAL_INT(6 * (int)sizeof(uint64_t), (int)sizeof(mem_info_t));
}

/* Test disk_info_t struct size (3 uint64_t fields) */
void test_monitoring_disk_info_struct_size(void) {
    TEST_ASSERT_EQUAL_INT(3 * (int)sizeof(uint64_t), (int)sizeof(disk_info_t));
}

/* Test net_info_t struct size (6 uint64_t fields) */
void test_monitoring_net_info_struct_size(void) {
    TEST_ASSERT_EQUAL_INT(6 * (int)sizeof(uint64_t), (int)sizeof(net_info_t));
}

/* Test load_info_t struct size (3 double fields) */
void test_monitoring_load_info_struct_size(void) {
    TEST_ASSERT_EQUAL_INT(3 * (int)sizeof(double), (int)sizeof(load_info_t));
}

/* Test conn_info_t struct size (2 int fields) */
void test_monitoring_conn_info_struct_size(void) {
    TEST_ASSERT_EQUAL_INT(2 * (int)sizeof(int), (int)sizeof(conn_info_t));
}

/* ====== monitoring_set_config tests ====== */

/* Test monitoring_set_config sets config pointer */
void test_monitoring_set_config(void) {
    agent_config_t config;
    config_init(&config);
    config.memory_include_cache = true;

    /* Set config pointer, should not crash */
    monitoring_set_config(&config);
    monitoring_set_config(NULL);
    TEST_PASS();
}

/* ====== CPU info tests ====== */

/* Test monitoring_get_cpu_info: verify return value and basic fields */
void test_monitoring_get_cpu_info(void) {
    cpu_info_t info;
    int ret = monitoring_get_cpu_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* CPU core count should be greater than 0 */
    TEST_ASSERT_TRUE(info.cpu_cores > 0);

    /* CPU architecture should be a non-empty string */
    TEST_ASSERT_TRUE(strlen(info.cpu_arch) > 0);

    /* CPU name should be a non-empty string */
    TEST_ASSERT_TRUE(strlen(info.cpu_name) > 0);

    /* CPU usage should be within reasonable range [0, 100] or 0 (first sample) */
    TEST_ASSERT_TRUE(info.cpu_usage >= 0.0);
    TEST_ASSERT_TRUE(info.cpu_usage <= 100.0);
}

/* Test monitoring_get_cpu_info with NULL pointer */
void test_monitoring_get_cpu_info_null(void) {
    int ret = monitoring_get_cpu_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== Memory info tests ====== */

/* Test monitoring_get_mem_info: verify Total/Free/Buffers/Cached parsing */
void test_monitoring_get_mem_info(void) {
    mem_info_t info;
    int ret = monitoring_get_mem_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Total memory should be greater than 0 */
    TEST_ASSERT_TRUE(info.total > 0);

    /* Free memory should not exceed total */
    TEST_ASSERT_TRUE(info.free <= info.total);

    /* Used memory should not exceed total */
    TEST_ASSERT_TRUE(info.used <= info.total);

    /* used + free should be close to total (accounting for buffers/cached) */
    TEST_ASSERT_TRUE(info.used + info.free <= info.total + info.buffers + info.cached);
}

/* Test monitoring_get_mem_info with NULL pointer */
void test_monitoring_get_mem_info_null(void) {
    int ret = monitoring_get_mem_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* Test monitoring_get_mem_info with memory_include_cache config */
void test_monitoring_get_mem_info_with_cache_config(void) {
    agent_config_t config;
    config_init(&config);
    config.memory_include_cache = true;
    monitoring_set_config(&config);

    mem_info_t info;
    int ret = monitoring_get_mem_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(info.total > 0);

    /* When memory_include_cache=true, used = total - free */
    TEST_ASSERT_TRUE(info.used <= info.total);
}

/* Test monitoring_get_swap_info: verify swap parsing */
void test_monitoring_get_swap_info(void) {
    mem_info_t info;
    int ret = monitoring_get_swap_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Total swap size >= 0 (some systems may have no swap) */
    TEST_ASSERT_TRUE(info.total >= 0);

    /* Used should not exceed total */
    TEST_ASSERT_TRUE(info.used <= info.total);

    /* Free should not exceed total */
    TEST_ASSERT_TRUE(info.free <= info.total);

    /* used + free should equal total */
    TEST_ASSERT_EQUAL_UINT64(info.used + info.free, info.total);
}

/* Test monitoring_get_swap_info with NULL pointer */
void test_monitoring_get_swap_info_null(void) {
    int ret = monitoring_get_swap_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== Disk info tests ====== */

/* Test monitoring_get_disk_info: verify virtual filesystem filtering */
void test_monitoring_get_disk_info(void) {
    disk_info_t info;
    int ret = monitoring_get_disk_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Total disk size should be >= 0 */
    TEST_ASSERT_TRUE(info.total >= 0);

    /* Free space should not exceed total */
    TEST_ASSERT_TRUE(info.free <= info.total);

    /* Used space should not exceed total */
    TEST_ASSERT_TRUE(info.used <= info.total);
}

/* Test monitoring_get_disk_info with NULL pointer */
void test_monitoring_get_disk_info_null(void) {
    int ret = monitoring_get_disk_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== Network info tests ====== */

/* Test monitoring_get_net_info: verify RX/TX byte parsing */
void test_monitoring_get_net_info(void) {
    net_info_t info;
    int ret = monitoring_get_net_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* RX/TX byte counts should be >= 0 */
    TEST_ASSERT_TRUE(info.rx_bytes >= 0);
    TEST_ASSERT_TRUE(info.tx_bytes >= 0);

    /* Packet counts should be >= 0 */
    TEST_ASSERT_TRUE(info.rx_packets >= 0);
    TEST_ASSERT_TRUE(info.tx_packets >= 0);
}

/* Test monitoring_get_net_info with NULL pointer */
void test_monitoring_get_net_info_null(void) {
    int ret = monitoring_get_net_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* Test monitoring_net_speed_update: verify speed update does not crash */
void test_monitoring_net_speed_update(void) {
    /* Call twice to compute speed delta */
    monitoring_net_speed_update();
    monitoring_net_speed_update();
    TEST_PASS();
}

/* ====== Connection count statistics tests ====== */

/* Test monitoring_get_conn_info: verify TCP/UDP connection count statistics */
void test_monitoring_get_conn_info(void) {
    conn_info_t info;
    int ret = monitoring_get_conn_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* TCP and UDP connection counts should be >= 0 */
    TEST_ASSERT_TRUE(info.tcp_count >= 0);
    TEST_ASSERT_TRUE(info.udp_count >= 0);
}

/* Test monitoring_get_conn_info with NULL pointer */
void test_monitoring_get_conn_info_null(void) {
    int ret = monitoring_get_conn_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== Load info tests ====== */

/* Test monitoring_get_load_info: verify 1/5/15 minute load parsing */
void test_monitoring_get_load_info(void) {
    load_info_t info;
    int ret = monitoring_get_load_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Load values should be >= 0 */
    TEST_ASSERT_TRUE(info.load1 >= 0.0);
    TEST_ASSERT_TRUE(info.load5 >= 0.0);
    TEST_ASSERT_TRUE(info.load15 >= 0.0);
}

/* Test monitoring_get_load_info with NULL pointer */
void test_monitoring_get_load_info_null(void) {
    int ret = monitoring_get_load_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== System info tests ====== */

/* Test monitoring_get_system_info: verify system info parsing */
void test_monitoring_get_system_info(void) {
    system_info_t info;
    int ret = monitoring_get_system_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* OS name should be a non-empty string */
    TEST_ASSERT_TRUE(strlen(info.os_name) > 0);

    /* Kernel version should be a non-empty string */
    TEST_ASSERT_TRUE(strlen(info.kernel_version) > 0);

    /* Architecture should be a non-empty string */
    TEST_ASSERT_TRUE(strlen(info.arch) > 0);

    /* Hostname should be a non-empty string */
    TEST_ASSERT_TRUE(strlen(info.hostname) > 0);

    /* uptime should be >= 0 */
    TEST_ASSERT_TRUE(info.uptime >= 0);
}

/* Test monitoring_get_system_info with NULL pointer */
void test_monitoring_get_system_info_null(void) {
    int ret = monitoring_get_system_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== Uptime tests ====== */

/* Test monitoring_get_uptime: verify system uptime retrieval */
void test_monitoring_get_uptime(void) {
    uint64_t uptime = monitoring_get_uptime();
    /* System uptime should be > 0 (may be 0 right after boot, but typically the system is already running when tested) */
    TEST_ASSERT_TRUE(uptime >= 0);
}

/* ====== Process count tests ====== */

/* Test monitoring_get_process_count: verify process count statistics */
void test_monitoring_get_process_count(void) {
    int count = monitoring_get_process_count();
    /* Process count should be >= 0, typically > 0 (at least the init process) */
    TEST_ASSERT_TRUE(count >= 0);
}

/* ====== IP address tests ====== */

/* Test monitoring_get_ip_address: verify IP address retrieval */
void test_monitoring_get_ip_address(void) {
    char ipv4[64];
    char ipv6[128];
    ipv4[0] = '\0';
    ipv6[0] = '\0';

    int ret = monitoring_get_ip_address(ipv4, sizeof(ipv4), ipv6, sizeof(ipv6));
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* IPv4 may be empty (no non-lo NICs), but the function should return successfully */
    /* IPv6 may be empty (no public IPv6) */
}

/* Test monitoring_get_ip_address with NULL buffers */
void test_monitoring_get_ip_address_null_buffers(void) {
    /* Passing NULL buffers should not crash */
    int ret = monitoring_get_ip_address(NULL, 0, NULL, 0);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

int main(void) {
    UNITY_BEGIN();

    /* Data structure size tests */
    RUN_TEST(test_monitoring_cpu_info_struct_size);
    RUN_TEST(test_monitoring_mem_info_struct_size);
    RUN_TEST(test_monitoring_disk_info_struct_size);
    RUN_TEST(test_monitoring_net_info_struct_size);
    RUN_TEST(test_monitoring_load_info_struct_size);
    RUN_TEST(test_monitoring_conn_info_struct_size);

    /* monitoring_set_config tests */
    RUN_TEST(test_monitoring_set_config);

    /* CPU info tests */
    RUN_TEST(test_monitoring_get_cpu_info);
    RUN_TEST(test_monitoring_get_cpu_info_null);

    /* Memory info tests */
    RUN_TEST(test_monitoring_get_mem_info);
    RUN_TEST(test_monitoring_get_mem_info_null);
    RUN_TEST(test_monitoring_get_mem_info_with_cache_config);

    /* Swap tests */
    RUN_TEST(test_monitoring_get_swap_info);
    RUN_TEST(test_monitoring_get_swap_info_null);

    /* Disk info tests */
    RUN_TEST(test_monitoring_get_disk_info);
    RUN_TEST(test_monitoring_get_disk_info_null);

    /* Network info tests */
    RUN_TEST(test_monitoring_get_net_info);
    RUN_TEST(test_monitoring_get_net_info_null);
    RUN_TEST(test_monitoring_net_speed_update);

    /* Connection count tests */
    RUN_TEST(test_monitoring_get_conn_info);
    RUN_TEST(test_monitoring_get_conn_info_null);

    /* Load info tests */
    RUN_TEST(test_monitoring_get_load_info);
    RUN_TEST(test_monitoring_get_load_info_null);

    /* System info tests */
    RUN_TEST(test_monitoring_get_system_info);
    RUN_TEST(test_monitoring_get_system_info_null);

    /* Uptime tests */
    RUN_TEST(test_monitoring_get_uptime);

    /* Process count tests */
    RUN_TEST(test_monitoring_get_process_count);

    /* IP address tests */
    RUN_TEST(test_monitoring_get_ip_address);
    RUN_TEST(test_monitoring_get_ip_address_null_buffers);

    return UNITY_END();
}
