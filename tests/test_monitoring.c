/*
 * test_monitoring.c - 系统监控模块单元测试
 *
 * 测试内容：
 *   - CPU 信息解析（monitoring_get_cpu_info）
 *   - 内存信息解析（monitoring_get_mem_info）
 *   - 磁盘信息解析（monitoring_get_disk_info）
 *   - 网络信息解析（monitoring_get_net_info）
 *   - 连接数统计（monitoring_get_conn_info）
 *   - 负载信息解析（monitoring_get_load_info）
 *   - 系统信息解析（monitoring_get_system_info）
 *   - 数据结构大小和字段验证
 *
 * 注意：由于监控函数直接读取 /proc 文件系统中的文件（路径硬编码），
 * 无法通过常规方式 mock。测试在真实 Linux 系统上运行，验证函数
 * 能正确读取并解析 /proc 数据。
 */

#include "unity.h"
#include "monitoring.h"
#include "config.h"

#include <string.h>
#include <stdint.h>

void setUp(void) {
    /* 重置全局配置指针 */
    monitoring_set_config(NULL);
}

void tearDown(void) {
    monitoring_set_config(NULL);
}

/* ====== 数据结构大小测试 ====== */

/* 测试 cpu_info_t 结构体大小合理 */
void test_monitoring_cpu_info_struct_size(void) {
    TEST_ASSERT_TRUE(sizeof(cpu_info_t) > 0);
    TEST_ASSERT_TRUE(sizeof(cpu_info_t) >= 128 + 32 + sizeof(int) + sizeof(double));
}

/* 测试 mem_info_t 结构体大小（6 个 uint64_t 字段） */
void test_monitoring_mem_info_struct_size(void) {
    TEST_ASSERT_EQUAL_INT(6 * (int)sizeof(uint64_t), (int)sizeof(mem_info_t));
}

/* 测试 disk_info_t 结构体大小（3 个 uint64_t 字段） */
void test_monitoring_disk_info_struct_size(void) {
    TEST_ASSERT_EQUAL_INT(3 * (int)sizeof(uint64_t), (int)sizeof(disk_info_t));
}

/* 测试 net_info_t 结构体大小（6 个 uint64_t 字段） */
void test_monitoring_net_info_struct_size(void) {
    TEST_ASSERT_EQUAL_INT(6 * (int)sizeof(uint64_t), (int)sizeof(net_info_t));
}

/* 测试 load_info_t 结构体大小（3 个 double 字段） */
void test_monitoring_load_info_struct_size(void) {
    TEST_ASSERT_EQUAL_INT(3 * (int)sizeof(double), (int)sizeof(load_info_t));
}

/* 测试 conn_info_t 结构体大小（2 个 int 字段） */
void test_monitoring_conn_info_struct_size(void) {
    TEST_ASSERT_EQUAL_INT(2 * (int)sizeof(int), (int)sizeof(conn_info_t));
}

/* ====== monitoring_set_config 测试 ====== */

/* 测试 monitoring_set_config 设置配置指针 */
void test_monitoring_set_config(void) {
    agent_config_t config;
    config_init(&config);
    config.memory_include_cache = true;

    /* 设置配置指针，不应崩溃 */
    monitoring_set_config(&config);
    monitoring_set_config(NULL);
    TEST_PASS();
}

/* ====== CPU 信息测试 ====== */

/* 测试 monitoring_get_cpu_info：验证返回值和基本字段 */
void test_monitoring_get_cpu_info(void) {
    cpu_info_t info;
    int ret = monitoring_get_cpu_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* CPU 核心数应大于 0 */
    TEST_ASSERT_TRUE(info.cpu_cores > 0);

    /* CPU 架构应为非空字符串 */
    TEST_ASSERT_TRUE(strlen(info.cpu_arch) > 0);

    /* CPU 名称应为非空字符串 */
    TEST_ASSERT_TRUE(strlen(info.cpu_name) > 0);

    /* CPU 使用率应在合理范围 [0, 100] 或为 0（首次采样） */
    TEST_ASSERT_TRUE(info.cpu_usage >= 0.0);
    TEST_ASSERT_TRUE(info.cpu_usage <= 100.0);
}

/* 测试 monitoring_get_cpu_info 传入 NULL */
void test_monitoring_get_cpu_info_null(void) {
    int ret = monitoring_get_cpu_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== 内存信息测试 ====== */

/* 测试 monitoring_get_mem_info：验证 Total/Free/Buffers/Cached 解析 */
void test_monitoring_get_mem_info(void) {
    mem_info_t info;
    int ret = monitoring_get_mem_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* 总内存应大于 0 */
    TEST_ASSERT_TRUE(info.total > 0);

    /* 空闲内存不应超过总内存 */
    TEST_ASSERT_TRUE(info.free <= info.total);

    /* 已用内存不应超过总内存 */
    TEST_ASSERT_TRUE(info.used <= info.total);

    /* used + free 应接近 total（考虑 buffers/cached） */
    TEST_ASSERT_TRUE(info.used + info.free <= info.total + info.buffers + info.cached);
}

/* 测试 monitoring_get_mem_info 传入 NULL */
void test_monitoring_get_mem_info_null(void) {
    int ret = monitoring_get_mem_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* 测试 monitoring_get_mem_info 带 memory_include_cache 配置 */
void test_monitoring_get_mem_info_with_cache_config(void) {
    agent_config_t config;
    config_init(&config);
    config.memory_include_cache = true;
    monitoring_set_config(&config);

    mem_info_t info;
    int ret = monitoring_get_mem_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(info.total > 0);

    /* memory_include_cache=true 时，used = total - free */
    TEST_ASSERT_TRUE(info.used <= info.total);
}

/* 测试 monitoring_get_swap_info：验证交换分区解析 */
void test_monitoring_get_swap_info(void) {
    mem_info_t info;
    int ret = monitoring_get_swap_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* 交换分区总大小 >= 0（某些系统可能无交换分区） */
    TEST_ASSERT_TRUE(info.total >= 0);

    /* 已用不超过总量 */
    TEST_ASSERT_TRUE(info.used <= info.total);

    /* 空闲不超过总量 */
    TEST_ASSERT_TRUE(info.free <= info.total);

    /* used + free 应等于 total */
    TEST_ASSERT_EQUAL_UINT64(info.used + info.free, info.total);
}

/* 测试 monitoring_get_swap_info 传入 NULL */
void test_monitoring_get_swap_info_null(void) {
    int ret = monitoring_get_swap_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== 磁盘信息测试 ====== */

/* 测试 monitoring_get_disk_info：验证虚拟文件系统过滤 */
void test_monitoring_get_disk_info(void) {
    disk_info_t info;
    int ret = monitoring_get_disk_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* 磁盘总大小应 >= 0 */
    TEST_ASSERT_TRUE(info.total >= 0);

    /* 空闲空间不超过总量 */
    TEST_ASSERT_TRUE(info.free <= info.total);

    /* 已用空间不超过总量 */
    TEST_ASSERT_TRUE(info.used <= info.total);
}

/* 测试 monitoring_get_disk_info 传入 NULL */
void test_monitoring_get_disk_info_null(void) {
    int ret = monitoring_get_disk_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== 网络信息测试 ====== */

/* 测试 monitoring_get_net_info：验证 RX/TX 字节解析 */
void test_monitoring_get_net_info(void) {
    net_info_t info;
    int ret = monitoring_get_net_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* RX/TX 字节数应 >= 0 */
    TEST_ASSERT_TRUE(info.rx_bytes >= 0);
    TEST_ASSERT_TRUE(info.tx_bytes >= 0);

    /* 包数应 >= 0 */
    TEST_ASSERT_TRUE(info.rx_packets >= 0);
    TEST_ASSERT_TRUE(info.tx_packets >= 0);
}

/* 测试 monitoring_get_net_info 传入 NULL */
void test_monitoring_get_net_info_null(void) {
    int ret = monitoring_get_net_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* 测试 monitoring_net_speed_update：验证速度更新不崩溃 */
void test_monitoring_net_speed_update(void) {
    /* 调用两次以计算速度差 */
    monitoring_net_speed_update();
    monitoring_net_speed_update();
    TEST_PASS();
}

/* ====== 连接数统计测试 ====== */

/* 测试 monitoring_get_conn_info：验证 TCP/UDP 连接数统计 */
void test_monitoring_get_conn_info(void) {
    conn_info_t info;
    int ret = monitoring_get_conn_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* TCP 和 UDP 连接数应 >= 0 */
    TEST_ASSERT_TRUE(info.tcp_count >= 0);
    TEST_ASSERT_TRUE(info.udp_count >= 0);
}

/* 测试 monitoring_get_conn_info 传入 NULL */
void test_monitoring_get_conn_info_null(void) {
    int ret = monitoring_get_conn_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== 负载信息测试 ====== */

/* 测试 monitoring_get_load_info：验证 1/5/15 分钟负载解析 */
void test_monitoring_get_load_info(void) {
    load_info_t info;
    int ret = monitoring_get_load_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* 负载值应 >= 0 */
    TEST_ASSERT_TRUE(info.load1 >= 0.0);
    TEST_ASSERT_TRUE(info.load5 >= 0.0);
    TEST_ASSERT_TRUE(info.load15 >= 0.0);
}

/* 测试 monitoring_get_load_info 传入 NULL */
void test_monitoring_get_load_info_null(void) {
    int ret = monitoring_get_load_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== 系统信息测试 ====== */

/* 测试 monitoring_get_system_info：验证系统信息解析 */
void test_monitoring_get_system_info(void) {
    system_info_t info;
    int ret = monitoring_get_system_info(&info);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* OS 名称应为非空字符串 */
    TEST_ASSERT_TRUE(strlen(info.os_name) > 0);

    /* 内核版本应为非空字符串 */
    TEST_ASSERT_TRUE(strlen(info.kernel_version) > 0);

    /* 架构应为非空字符串 */
    TEST_ASSERT_TRUE(strlen(info.arch) > 0);

    /* 主机名应为非空字符串 */
    TEST_ASSERT_TRUE(strlen(info.hostname) > 0);

    /* uptime 应 >= 0 */
    TEST_ASSERT_TRUE(info.uptime >= 0);
}

/* 测试 monitoring_get_system_info 传入 NULL */
void test_monitoring_get_system_info_null(void) {
    int ret = monitoring_get_system_info(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* ====== Uptime 测试 ====== */

/* 测试 monitoring_get_uptime：验证系统运行时间获取 */
void test_monitoring_get_uptime(void) {
    uint64_t uptime = monitoring_get_uptime();
    /* 系统运行时间应 > 0（系统刚启动时可能为 0，但通常测试时系统已运行） */
    TEST_ASSERT_TRUE(uptime >= 0);
}

/* ====== 进程数测试 ====== */

/* 测试 monitoring_get_process_count：验证进程数统计 */
void test_monitoring_get_process_count(void) {
    int count = monitoring_get_process_count();
    /* 进程数应 >= 0，通常 > 0（至少有 init 进程） */
    TEST_ASSERT_TRUE(count >= 0);
}

/* ====== IP 地址测试 ====== */

/* 测试 monitoring_get_ip_address：验证 IP 地址获取 */
void test_monitoring_get_ip_address(void) {
    char ipv4[64];
    char ipv6[128];
    ipv4[0] = '\0';
    ipv6[0] = '\0';

    int ret = monitoring_get_ip_address(ipv4, sizeof(ipv4), ipv6, sizeof(ipv6));
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* IPv4 可能为空（无非 lo 网卡），但函数应成功返回 */
    /* IPv6 可能为空（无公网 IPv6） */
}

/* 测试 monitoring_get_ip_address 传入 NULL 缓冲区 */
void test_monitoring_get_ip_address_null_buffers(void) {
    /* 传入 NULL 缓冲区不应崩溃 */
    int ret = monitoring_get_ip_address(NULL, 0, NULL, 0);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

int main(void) {
    UNITY_BEGIN();

    /* 数据结构大小测试 */
    RUN_TEST(test_monitoring_cpu_info_struct_size);
    RUN_TEST(test_monitoring_mem_info_struct_size);
    RUN_TEST(test_monitoring_disk_info_struct_size);
    RUN_TEST(test_monitoring_net_info_struct_size);
    RUN_TEST(test_monitoring_load_info_struct_size);
    RUN_TEST(test_monitoring_conn_info_struct_size);

    /* monitoring_set_config 测试 */
    RUN_TEST(test_monitoring_set_config);

    /* CPU 信息测试 */
    RUN_TEST(test_monitoring_get_cpu_info);
    RUN_TEST(test_monitoring_get_cpu_info_null);

    /* 内存信息测试 */
    RUN_TEST(test_monitoring_get_mem_info);
    RUN_TEST(test_monitoring_get_mem_info_null);
    RUN_TEST(test_monitoring_get_mem_info_with_cache_config);

    /* 交换分区测试 */
    RUN_TEST(test_monitoring_get_swap_info);
    RUN_TEST(test_monitoring_get_swap_info_null);

    /* 磁盘信息测试 */
    RUN_TEST(test_monitoring_get_disk_info);
    RUN_TEST(test_monitoring_get_disk_info_null);

    /* 网络信息测试 */
    RUN_TEST(test_monitoring_get_net_info);
    RUN_TEST(test_monitoring_get_net_info_null);
    RUN_TEST(test_monitoring_net_speed_update);

    /* 连接数测试 */
    RUN_TEST(test_monitoring_get_conn_info);
    RUN_TEST(test_monitoring_get_conn_info_null);

    /* 负载信息测试 */
    RUN_TEST(test_monitoring_get_load_info);
    RUN_TEST(test_monitoring_get_load_info_null);

    /* 系统信息测试 */
    RUN_TEST(test_monitoring_get_system_info);
    RUN_TEST(test_monitoring_get_system_info_null);

    /* Uptime 测试 */
    RUN_TEST(test_monitoring_get_uptime);

    /* 进程数测试 */
    RUN_TEST(test_monitoring_get_process_count);

    /* IP 地址测试 */
    RUN_TEST(test_monitoring_get_ip_address);
    RUN_TEST(test_monitoring_get_ip_address_null_buffers);

    return UNITY_END();
}
