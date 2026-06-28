/*
 * test_report.c - 报告生成模块单元测试
 *
 * 测试内容：
 *   - report_generate 生成的 JSON 包含所有必需字段（cpu/ram/swap/load/disk/network/connections/uptime/process）
 *   - report_generate_basic_info 生成的 JSON 包含所有基础信息字段
 *   - 验证 JSON 格式有效性（使用 cJSON_Parse 验证）
 *   - 验证字段类型正确（数值字段为数值，字符串字段为字符串）
 *
 * 注意：report_generate 和 report_generate_basic_info 调用 monitoring 函数
 * 读取 /proc 文件系统，测试在真实 Linux 系统上运行。
 */

#include "unity.h"
#include "report.h"
#include "monitoring.h"
#include "config.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>

/* 报告缓冲区大小 */
#define REPORT_BUF_SIZE 8192

void setUp(void) {
    /* 设置全局配置指针，供 monitoring 模块使用 */
    monitoring_set_config(NULL);
}

void tearDown(void) {
    monitoring_set_config(NULL);
}

/* ====== report_generate 测试 ====== */

/* 测试 report_generate：验证返回值和 JSON 格式有效性 */
void test_report_generate_valid_json(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate(&config, buf, sizeof(buf));

    TEST_ASSERT_TRUE(len > 0);

    /* 使用 cJSON_Parse 验证 JSON 格式有效 */
    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));

    cJSON_Delete(root);
}

/* 测试 report_generate：验证包含所有必需字段 */
void test_report_generate_required_fields(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate(&config, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* 验证 cpu 字段存在且为对象 */
    cJSON *cpu = cJSON_GetObjectItem(root, "cpu");
    TEST_ASSERT_NOT_NULL(cpu);
    TEST_ASSERT_TRUE(cJSON_IsObject(cpu));

    /* 验证 cpu.usage 存在且为数值 */
    cJSON *cpu_usage = cJSON_GetObjectItem(cpu, "usage");
    TEST_ASSERT_NOT_NULL(cpu_usage);
    TEST_ASSERT_TRUE(cJSON_IsNumber(cpu_usage));

    /* 验证 ram 字段存在且为对象 */
    cJSON *ram = cJSON_GetObjectItem(root, "ram");
    TEST_ASSERT_NOT_NULL(ram);
    TEST_ASSERT_TRUE(cJSON_IsObject(ram));

    /* 验证 ram.total 和 ram.used 为数值 */
    cJSON *ram_total = cJSON_GetObjectItem(ram, "total");
    TEST_ASSERT_NOT_NULL(ram_total);
    TEST_ASSERT_TRUE(cJSON_IsNumber(ram_total));

    cJSON *ram_used = cJSON_GetObjectItem(ram, "used");
    TEST_ASSERT_NOT_NULL(ram_used);
    TEST_ASSERT_TRUE(cJSON_IsNumber(ram_used));

    /* 验证 swap 字段存在且为对象 */
    cJSON *swap = cJSON_GetObjectItem(root, "swap");
    TEST_ASSERT_NOT_NULL(swap);
    TEST_ASSERT_TRUE(cJSON_IsObject(swap));

    /* 验证 load 字段存在且为对象 */
    cJSON *load = cJSON_GetObjectItem(root, "load");
    TEST_ASSERT_NOT_NULL(load);
    TEST_ASSERT_TRUE(cJSON_IsObject(load));

    /* 验证 load.load1/load5/load15 为数值 */
    cJSON *load1 = cJSON_GetObjectItem(load, "load1");
    TEST_ASSERT_NOT_NULL(load1);
    TEST_ASSERT_TRUE(cJSON_IsNumber(load1));

    /* 验证 disk 字段存在且为对象 */
    cJSON *disk = cJSON_GetObjectItem(root, "disk");
    TEST_ASSERT_NOT_NULL(disk);
    TEST_ASSERT_TRUE(cJSON_IsObject(disk));

    /* 验证 network 字段存在且为对象 */
    cJSON *network = cJSON_GetObjectItem(root, "network");
    TEST_ASSERT_NOT_NULL(network);
    TEST_ASSERT_TRUE(cJSON_IsObject(network));

    /* 验证 connections 字段存在且为对象 */
    cJSON *connections = cJSON_GetObjectItem(root, "connections");
    TEST_ASSERT_NOT_NULL(connections);
    TEST_ASSERT_TRUE(cJSON_IsObject(connections));

    /* 验证 uptime 为数值 */
    cJSON *uptime = cJSON_GetObjectItem(root, "uptime");
    TEST_ASSERT_NOT_NULL(uptime);
    TEST_ASSERT_TRUE(cJSON_IsNumber(uptime));

    /* 验证 process 为数值 */
    cJSON *process = cJSON_GetObjectItem(root, "process");
    TEST_ASSERT_NOT_NULL(process);
    TEST_ASSERT_TRUE(cJSON_IsNumber(process));

    /* 验证 message 为字符串 */
    cJSON *message = cJSON_GetObjectItem(root, "message");
    TEST_ASSERT_NOT_NULL(message);
    TEST_ASSERT_TRUE(cJSON_IsString(message));

    cJSON_Delete(root);
}

/* 测试 report_generate：验证字段值合理性 */
void test_report_generate_field_values(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    report_generate(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* CPU 使用率应在 [0, 100] 范围内 */
    cJSON *cpu_obj = cJSON_GetObjectItem(root, "cpu");
    TEST_ASSERT_NOT_NULL(cpu_obj);
    cJSON *cpu_usage = cJSON_GetObjectItem(cpu_obj, "usage");
    TEST_ASSERT_NOT_NULL(cpu_usage);
    TEST_ASSERT_TRUE(cpu_usage->valuedouble >= 0.0);
    TEST_ASSERT_TRUE(cpu_usage->valuedouble <= 100.0);

    /* 内存总量应 >= 0 */
    cJSON *ram = cJSON_GetObjectItem(root, "ram");
    cJSON *ram_total = cJSON_GetObjectItem(ram, "total");
    TEST_ASSERT_TRUE(ram_total->valuedouble >= 0.0);

    /* 已用内存应 <= 总内存 */
    cJSON *ram_used = cJSON_GetObjectItem(ram, "used");
    TEST_ASSERT_TRUE(ram_used->valuedouble <= ram_total->valuedouble);

    /* TCP 连接数应 >= 0 */
    cJSON *connections = cJSON_GetObjectItem(root, "connections");
    cJSON *tcp = cJSON_GetObjectItem(connections, "tcp");
    TEST_ASSERT_TRUE(tcp->valuedouble >= 0.0);

    /* uptime 应 >= 0 */
    cJSON *uptime = cJSON_GetObjectItem(root, "uptime");
    TEST_ASSERT_TRUE(uptime->valuedouble >= 0.0);

    /* process 应 >= 0 */
    cJSON *process = cJSON_GetObjectItem(root, "process");
    TEST_ASSERT_TRUE(process->valuedouble >= 0.0);

    cJSON_Delete(root);
}

/* 测试 report_generate 传入 NULL 参数 */
void test_report_generate_null_args(void) {
    agent_config_t config;
    config_init(&config);
    char buf[REPORT_BUF_SIZE];

    TEST_ASSERT_EQUAL_INT(-1, report_generate(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate(&config, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate(&config, buf, 0));
}

/* 测试 report_generate：验证 network 子字段 */
void test_report_generate_network_fields(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    report_generate(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    cJSON *network = cJSON_GetObjectItem(root, "network");
    TEST_ASSERT_NOT_NULL(network);

    /* 验证 up/down/totalUp/totalDown 字段存在且为数值 */
    const char *net_fields[] = {"up", "down", "totalUp", "totalDown"};
    for (size_t i = 0; i < sizeof(net_fields) / sizeof(net_fields[0]); i++) {
        cJSON *field = cJSON_GetObjectItem(network, net_fields[i]);
        TEST_ASSERT_NOT_NULL(field);
        TEST_ASSERT_TRUE(cJSON_IsNumber(field));
    }

    cJSON_Delete(root);
}

/* 测试 report_generate：验证 connections 子字段 */
void test_report_generate_connections_fields(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    report_generate(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    cJSON *connections = cJSON_GetObjectItem(root, "connections");
    TEST_ASSERT_NOT_NULL(connections);

    /* 验证 tcp 和 udp 字段存在且为数值 */
    cJSON *tcp = cJSON_GetObjectItem(connections, "tcp");
    TEST_ASSERT_NOT_NULL(tcp);
    TEST_ASSERT_TRUE(cJSON_IsNumber(tcp));

    cJSON *udp = cJSON_GetObjectItem(connections, "udp");
    TEST_ASSERT_NOT_NULL(udp);
    TEST_ASSERT_TRUE(cJSON_IsNumber(udp));

    cJSON_Delete(root);
}

/* 测试 report_generate：验证 load 子字段 */
void test_report_generate_load_fields(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    report_generate(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    cJSON *load = cJSON_GetObjectItem(root, "load");
    TEST_ASSERT_NOT_NULL(load);

    /* 验证 load1/load5/load15 字段存在且为数值 */
    const char *load_fields[] = {"load1", "load5", "load15"};
    for (size_t i = 0; i < sizeof(load_fields) / sizeof(load_fields[0]); i++) {
        cJSON *field = cJSON_GetObjectItem(load, load_fields[i]);
        TEST_ASSERT_NOT_NULL(field);
        TEST_ASSERT_TRUE(cJSON_IsNumber(field));
    }

    cJSON_Delete(root);
}

/* ====== report_generate_basic_info 测试 ====== */

/* 测试 report_generate_basic_info：验证 JSON 格式有效性 */
void test_report_generate_basic_info_valid_json(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate_basic_info(&config, buf, sizeof(buf));

    TEST_ASSERT_TRUE(len > 0);

    /* 使用 cJSON_Parse 验证 JSON 格式有效 */
    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));

    cJSON_Delete(root);
}

/* 测试 report_generate_basic_info：验证包含所有必需字段 */
void test_report_generate_basic_info_required_fields(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate_basic_info(&config, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* 验证字符串字段存在且为字符串类型 */
    const char *string_fields[] = {
        "cpu_name", "arch", "os", "kernel_version",
        "ipv4", "ipv6", "gpu_name", "virtualization", "version"
    };
    for (size_t i = 0; i < sizeof(string_fields) / sizeof(string_fields[0]); i++) {
        cJSON *field = cJSON_GetObjectItem(root, string_fields[i]);
        TEST_ASSERT_NOT_NULL(field);
        TEST_ASSERT_TRUE(cJSON_IsString(field));
    }

    /* 验证数值字段存在且为数值类型 */
    const char *number_fields[] = {
        "cpu_cores", "mem_total", "swap_total", "disk_total"
    };
    for (size_t i = 0; i < sizeof(number_fields) / sizeof(number_fields[0]); i++) {
        cJSON *field = cJSON_GetObjectItem(root, number_fields[i]);
        TEST_ASSERT_NOT_NULL(field);
        TEST_ASSERT_TRUE(cJSON_IsNumber(field));
    }

    cJSON_Delete(root);
}

/* 测试 report_generate_basic_info：验证字段值合理性 */
void test_report_generate_basic_info_field_values(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    report_generate_basic_info(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* CPU 名称应非空 */
    cJSON *cpu_name = cJSON_GetObjectItem(root, "cpu_name");
    TEST_ASSERT_TRUE(strlen(cpu_name->valuestring) > 0);

    /* 架构应非空 */
    cJSON *arch = cJSON_GetObjectItem(root, "arch");
    TEST_ASSERT_TRUE(strlen(arch->valuestring) > 0);

    /* OS 应非空 */
    cJSON *os = cJSON_GetObjectItem(root, "os");
    TEST_ASSERT_TRUE(strlen(os->valuestring) > 0);

    /* 内核版本应非空 */
    cJSON *kernel = cJSON_GetObjectItem(root, "kernel_version");
    TEST_ASSERT_TRUE(strlen(kernel->valuestring) > 0);

    /* CPU 核心数应 > 0 */
    cJSON *cpu_cores = cJSON_GetObjectItem(root, "cpu_cores");
    TEST_ASSERT_TRUE(cpu_cores->valueint > 0);

    /* 内存总量应 >= 0 */
    cJSON *mem_total = cJSON_GetObjectItem(root, "mem_total");
    TEST_ASSERT_TRUE(mem_total->valuedouble >= 0.0);

    /* 版本应为非空字符串 */
    cJSON *version = cJSON_GetObjectItem(root, "version");
    TEST_ASSERT_TRUE(strlen(version->valuestring) > 0);

    cJSON_Delete(root);
}

/* 测试 report_generate_basic_info 传入 NULL 参数 */
void test_report_generate_basic_info_null_args(void) {
    agent_config_t config;
    config_init(&config);
    char buf[REPORT_BUF_SIZE];

    TEST_ASSERT_EQUAL_INT(-1, report_generate_basic_info(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate_basic_info(&config, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate_basic_info(&config, buf, 0));
}

/* 测试 report_generate_basic_info：使用自定义 IPv4/IPv6 */
void test_report_generate_basic_info_custom_ip(void) {
    agent_config_t config;
    config_init(&config);
    strncpy(config.custom_ipv4, "10.0.0.1", sizeof(config.custom_ipv4) - 1);
    strncpy(config.custom_ipv6, "fe80::1", sizeof(config.custom_ipv6) - 1);

    char buf[REPORT_BUF_SIZE];
    report_generate_basic_info(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* 验证自定义 IP 被使用 */
    cJSON *ipv4 = cJSON_GetObjectItem(root, "ipv4");
    TEST_ASSERT_EQUAL_STRING("10.0.0.1", ipv4->valuestring);

    cJSON *ipv6 = cJSON_GetObjectItem(root, "ipv6");
    TEST_ASSERT_EQUAL_STRING("fe80::1", ipv6->valuestring);

    cJSON_Delete(root);
}

int main(void) {
    UNITY_BEGIN();

    /* report_generate 测试 */
    RUN_TEST(test_report_generate_valid_json);
    RUN_TEST(test_report_generate_required_fields);
    RUN_TEST(test_report_generate_field_values);
    RUN_TEST(test_report_generate_null_args);
    RUN_TEST(test_report_generate_network_fields);
    RUN_TEST(test_report_generate_connections_fields);
    RUN_TEST(test_report_generate_load_fields);

    /* report_generate_basic_info 测试 */
    RUN_TEST(test_report_generate_basic_info_valid_json);
    RUN_TEST(test_report_generate_basic_info_required_fields);
    RUN_TEST(test_report_generate_basic_info_field_values);
    RUN_TEST(test_report_generate_basic_info_null_args);
    RUN_TEST(test_report_generate_basic_info_custom_ip);

    return UNITY_END();
}
