/*
 * test_config.c - 配置模块单元测试
 *
 * 测试内容：
 *   - config_init 默认值初始化
 *   - config_load_from_env 环境变量加载
 *   - config_load_from_file JSON 文件加载
 *   - 配置优先级（环境变量 > 配置文件）
 */

#include "unity.h"
#include "config.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 临时配置文件路径 */
#define TEST_CONFIG_FILE_PATH "/tmp/test_komari_config.json"
#define TEST_CONFIG_PRIORITY_PATH "/tmp/test_komari_config_priority.json"

void setUp(void) {
    /* 每个测试前清理所有相关环境变量，避免相互干扰 */
    unsetenv("AGENT_TOKEN");
    unsetenv("AGENT_ENDPOINT");
    unsetenv("AGENT_CUSTOM_DNS");
    unsetenv("AGENT_INCLUDE_NICS");
    unsetenv("AGENT_EXCLUDE_NICS");
    unsetenv("AGENT_CUSTOM_IPV4");
    unsetenv("AGENT_CUSTOM_IPV6");
    unsetenv("AGENT_LANGUAGE");
    unsetenv("AGENT_INTERVAL");
    unsetenv("AGENT_MAX_RETRIES");
    unsetenv("AGENT_RECONNECT_INTERVAL");
    unsetenv("AGENT_INFO_REPORT_INTERVAL");
    unsetenv("AGENT_MONTH_ROTATE");
    unsetenv("AGENT_DISABLE_AUTO_UPDATE");
    unsetenv("AGENT_DISABLE_WEB_SSH");
    unsetenv("AGENT_IGNORE_UNSAFE_CERT");
    unsetenv("AGENT_MEMORY_INCLUDE_CACHE");
    unsetenv("AGENT_ENABLE_GPU");
}

void tearDown(void) {
    /* 每个测试后清理环境变量 */
    unsetenv("AGENT_TOKEN");
    unsetenv("AGENT_ENDPOINT");
    unsetenv("AGENT_CUSTOM_DNS");
    unsetenv("AGENT_INCLUDE_NICS");
    unsetenv("AGENT_EXCLUDE_NICS");
    unsetenv("AGENT_CUSTOM_IPV4");
    unsetenv("AGENT_CUSTOM_IPV6");
    unsetenv("AGENT_LANGUAGE");
    unsetenv("AGENT_INTERVAL");
    unsetenv("AGENT_MAX_RETRIES");
    unsetenv("AGENT_RECONNECT_INTERVAL");
    unsetenv("AGENT_INFO_REPORT_INTERVAL");
    unsetenv("AGENT_MONTH_ROTATE");
    unsetenv("AGENT_DISABLE_AUTO_UPDATE");
    unsetenv("AGENT_DISABLE_WEB_SSH");
    unsetenv("AGENT_IGNORE_UNSAFE_CERT");
    unsetenv("AGENT_MEMORY_INCLUDE_CACHE");
    unsetenv("AGENT_ENABLE_GPU");
}

/* 测试 config_init：验证所有字段被正确初始化为默认值 */
void test_config_init_defaults(void) {
    agent_config_t config;
    config_init(&config);

    /* 验证数值字段默认值 */
    TEST_ASSERT_EQUAL_DOUBLE(1.0, config.interval);
    TEST_ASSERT_EQUAL_INT(5, config.max_retries);
    TEST_ASSERT_EQUAL_INT(5, config.reconnect_interval);
    TEST_ASSERT_EQUAL_INT(30, config.info_report_interval);
    TEST_ASSERT_EQUAL_INT(0, config.month_rotate);

    /* 验证布尔字段默认值（均为 false） */
    TEST_ASSERT_FALSE(config.disable_auto_update);
    TEST_ASSERT_FALSE(config.disable_web_ssh);
    TEST_ASSERT_FALSE(config.ignore_unsafe_cert);
    TEST_ASSERT_FALSE(config.memory_include_cache);
    TEST_ASSERT_FALSE(config.memory_report_raw_used);
    TEST_ASSERT_FALSE(config.enable_gpu);
    TEST_ASSERT_FALSE(config.get_ip_addr_from_nic);
    TEST_ASSERT_FALSE(config.show_warning);

    /* 验证字符串字段默认值 */
    TEST_ASSERT_EQUAL_STRING("", config.token);
    TEST_ASSERT_EQUAL_STRING("", config.endpoint);
    TEST_ASSERT_EQUAL_STRING("", config.custom_dns);
    TEST_ASSERT_EQUAL_STRING("", config.include_nics);
    TEST_ASSERT_EQUAL_STRING("", config.exclude_nics);
    TEST_ASSERT_EQUAL_STRING("", config.custom_ipv4);
    TEST_ASSERT_EQUAL_STRING("", config.custom_ipv6);
    TEST_ASSERT_EQUAL_STRING("auto", config.language);
}

/* 测试 config_init 传入 NULL：不应崩溃 */
void test_config_init_null(void) {
    config_init(NULL);
    TEST_PASS();
}

/* 测试 config_load_from_env：设置环境变量后验证字符串字段被正确加载 */
void test_config_load_from_env_strings(void) {
    agent_config_t config;
    config_init(&config);

    setenv("AGENT_TOKEN", "test_token_12345", 1);
    setenv("AGENT_ENDPOINT", "wss://example.com/api/ws", 1);
    setenv("AGENT_CUSTOM_DNS", "8.8.8.8", 1);
    setenv("AGENT_LANGUAGE", "zh-CN", 1);

    int ret = config_load_from_env(&config);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING("test_token_12345", config.token);
    TEST_ASSERT_EQUAL_STRING("wss://example.com/api/ws", config.endpoint);
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", config.custom_dns);
    TEST_ASSERT_EQUAL_STRING("zh-CN", config.language);
}

/* 测试 config_load_from_env：数值和布尔字段加载 */
void test_config_load_from_env_numbers_and_bools(void) {
    agent_config_t config;
    config_init(&config);

    setenv("AGENT_INTERVAL", "2.5", 1);
    setenv("AGENT_MAX_RETRIES", "10", 1);
    setenv("AGENT_RECONNECT_INTERVAL", "15", 1);
    setenv("AGENT_INFO_REPORT_INTERVAL", "60", 1);
    setenv("AGENT_DISABLE_WEB_SSH", "true", 1);
    setenv("AGENT_ENABLE_GPU", "1", 1);
    setenv("AGENT_MEMORY_INCLUDE_CACHE", "yes", 1);

    int ret = config_load_from_env(&config);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_DOUBLE(2.5, config.interval);
    TEST_ASSERT_EQUAL_INT(10, config.max_retries);
    TEST_ASSERT_EQUAL_INT(15, config.reconnect_interval);
    TEST_ASSERT_EQUAL_INT(60, config.info_report_interval);
    TEST_ASSERT_TRUE(config.disable_web_ssh);
    TEST_ASSERT_TRUE(config.enable_gpu);
    TEST_ASSERT_TRUE(config.memory_include_cache);
}

/* 测试 config_load_from_env：布尔值的多种表示形式 */
void test_config_load_from_env_bool_variants(void) {
    agent_config_t config;

    /* "yes" 应为 true */
    config_init(&config);
    setenv("AGENT_DISABLE_WEB_SSH", "yes", 1);
    config_load_from_env(&config);
    TEST_ASSERT_TRUE(config.disable_web_ssh);
    unsetenv("AGENT_DISABLE_WEB_SSH");

    /* "1" 应为 true */
    config_init(&config);
    setenv("AGENT_DISABLE_WEB_SSH", "1", 1);
    config_load_from_env(&config);
    TEST_ASSERT_TRUE(config.disable_web_ssh);
    unsetenv("AGENT_DISABLE_WEB_SSH");

    /* "TRUE"（大写）应为 true */
    config_init(&config);
    setenv("AGENT_DISABLE_WEB_SSH", "TRUE", 1);
    config_load_from_env(&config);
    TEST_ASSERT_TRUE(config.disable_web_ssh);
    unsetenv("AGENT_DISABLE_WEB_SSH");

    /* "0" 应为 false */
    config_init(&config);
    setenv("AGENT_DISABLE_WEB_SSH", "0", 1);
    config_load_from_env(&config);
    TEST_ASSERT_FALSE(config.disable_web_ssh);
    unsetenv("AGENT_DISABLE_WEB_SSH");

    /* "no" 应为 false */
    config_init(&config);
    setenv("AGENT_DISABLE_WEB_SSH", "no", 1);
    config_load_from_env(&config);
    TEST_ASSERT_FALSE(config.disable_web_ssh);
    unsetenv("AGENT_DISABLE_WEB_SSH");
}

/* 测试 config_load_from_env 传入 NULL */
void test_config_load_from_env_null(void) {
    int ret = config_load_from_env(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* 测试 config_load_from_env：未设置环境变量时保持默认值 */
void test_config_load_from_env_keep_defaults(void) {
    agent_config_t config;
    config_init(&config);

    int ret = config_load_from_env(&config);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* 未设置环境变量，默认值应保持不变 */
    TEST_ASSERT_EQUAL_DOUBLE(1.0, config.interval);
    TEST_ASSERT_EQUAL_INT(5, config.max_retries);
    TEST_ASSERT_EQUAL_STRING("auto", config.language);
}

/* 测试 config_load_from_file：创建临时 JSON 配置文件，验证字段被正确加载 */
void test_config_load_from_file_basic(void) {
    const char *json_content =
        "{"
        "\"token\": \"file_token_abc\","
        "\"endpoint\": \"wss://file.example.com/api\","
        "\"custom_dns\": \"1.1.1.1\","
        "\"interval\": 3.5,"
        "\"max_retries\": 15,"
        "\"reconnect_interval\": 20,"
        "\"info_report_interval\": 45,"
        "\"disable_web_ssh\": true,"
        "\"enable_gpu\": false,"
        "\"ignore_unsafe_cert\": true,"
        "\"language\": \"en\""
        "}";

    FILE *f = fopen(TEST_CONFIG_FILE_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(json_content, f);
    fclose(f);

    agent_config_t config;
    config_init(&config);

    int ret = config_load_from_file(&config, TEST_CONFIG_FILE_PATH);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* 验证字符串字段 */
    TEST_ASSERT_EQUAL_STRING("file_token_abc", config.token);
    TEST_ASSERT_EQUAL_STRING("wss://file.example.com/api", config.endpoint);
    TEST_ASSERT_EQUAL_STRING("1.1.1.1", config.custom_dns);
    TEST_ASSERT_EQUAL_STRING("en", config.language);

    /* 验证数值字段 */
    TEST_ASSERT_EQUAL_DOUBLE(3.5, config.interval);
    TEST_ASSERT_EQUAL_INT(15, config.max_retries);
    TEST_ASSERT_EQUAL_INT(20, config.reconnect_interval);
    TEST_ASSERT_EQUAL_INT(45, config.info_report_interval);

    /* 验证布尔字段 */
    TEST_ASSERT_TRUE(config.disable_web_ssh);
    TEST_ASSERT_FALSE(config.enable_gpu);
    TEST_ASSERT_TRUE(config.ignore_unsafe_cert);

    remove(TEST_CONFIG_FILE_PATH);
}

/* 测试 config_load_from_file：文件不存在时返回错误 */
void test_config_load_from_file_not_found(void) {
    agent_config_t config;
    config_init(&config);

    int ret = config_load_from_file(&config, "/nonexistent/path/config.json");
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* 测试 config_load_from_file：无效 JSON 格式 */
void test_config_load_from_file_invalid_json(void) {
    const char *json_content = "{ invalid json content !!! }";
    FILE *f = fopen(TEST_CONFIG_FILE_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(json_content, f);
    fclose(f);

    agent_config_t config;
    config_init(&config);

    int ret = config_load_from_file(&config, TEST_CONFIG_FILE_PATH);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    remove(TEST_CONFIG_FILE_PATH);
}

/* 测试 config_load_from_file：空 JSON 对象 */
void test_config_load_from_file_empty_object(void) {
    const char *json_content = "{}";
    FILE *f = fopen(TEST_CONFIG_FILE_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(json_content, f);
    fclose(f);

    agent_config_t config;
    config_init(&config);

    int ret = config_load_from_file(&config, TEST_CONFIG_FILE_PATH);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* 空 JSON 不应改变默认值 */
    TEST_ASSERT_EQUAL_DOUBLE(1.0, config.interval);
    TEST_ASSERT_EQUAL_INT(5, config.max_retries);
    TEST_ASSERT_EQUAL_STRING("auto", config.language);

    remove(TEST_CONFIG_FILE_PATH);
}

/* 测试 config_load_from_file 传入 NULL 参数 */
void test_config_load_from_file_null_args(void) {
    agent_config_t config;
    config_init(&config);

    TEST_ASSERT_EQUAL_INT(-1, config_load_from_file(NULL, TEST_CONFIG_FILE_PATH));
    TEST_ASSERT_EQUAL_INT(-1, config_load_from_file(&config, NULL));
}

/* 测试配置优先级：环境变量覆盖配置文件值
 *
 * 配置加载顺序为：UCI → 配置文件 → 环境变量 → 命令行参数
 * 后加载的会覆盖先加载的值。此测试验证环境变量覆盖配置文件。
 * 注意：UCI 和命令行参数不在测试范围内（UCI 依赖 OpenWrt 环境，命令行参数在 main.c 中处理）。
 */
void test_config_priority_env_over_file(void) {
    const char *json_content =
        "{"
        "\"token\": \"file_token\","
        "\"endpoint\": \"wss://file.example.com\","
        "\"max_retries\": 20,"
        "\"language\": \"en\""
        "}";

    FILE *f = fopen(TEST_CONFIG_PRIORITY_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(json_content, f);
    fclose(f);

    agent_config_t config;
    config_init(&config);

    /* 第一步：从文件加载 */
    int ret = config_load_from_file(&config, TEST_CONFIG_PRIORITY_PATH);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("file_token", config.token);
    TEST_ASSERT_EQUAL_INT(20, config.max_retries);
    TEST_ASSERT_EQUAL_STRING("en", config.language);

    /* 第二步：设置环境变量，模拟命令行或运行时覆盖 */
    setenv("AGENT_TOKEN", "env_token_override", 1);
    setenv("AGENT_MAX_RETRIES", "99", 1);

    ret = config_load_from_env(&config);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* 环境变量应覆盖配置文件的值 */
    TEST_ASSERT_EQUAL_STRING("env_token_override", config.token);
    TEST_ASSERT_EQUAL_INT(99, config.max_retries);

    /* 未被环境变量覆盖的字段保持配置文件的值 */
    TEST_ASSERT_EQUAL_STRING("wss://file.example.com", config.endpoint);
    TEST_ASSERT_EQUAL_STRING("en", config.language);

    remove(TEST_CONFIG_PRIORITY_PATH);
}

/* 测试配置优先级：配置文件覆盖默认值 */
void test_config_priority_file_over_defaults(void) {
    const char *json_content =
        "{"
        "\"token\": \"file_only_token\","
        "\"interval\": 5.0"
        "}";

    FILE *f = fopen(TEST_CONFIG_PRIORITY_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(json_content, f);
    fclose(f);

    agent_config_t config;
    config_init(&config);

    /* 默认值验证 */
    TEST_ASSERT_EQUAL_DOUBLE(1.0, config.interval);
    TEST_ASSERT_EQUAL_STRING("", config.token);

    /* 从文件加载后覆盖默认值 */
    config_load_from_file(&config, TEST_CONFIG_PRIORITY_PATH);
    TEST_ASSERT_EQUAL_DOUBLE(5.0, config.interval);
    TEST_ASSERT_EQUAL_STRING("file_only_token", config.token);

    /* 未在文件中指定的字段保持默认值 */
    TEST_ASSERT_EQUAL_INT(5, config.max_retries);

    remove(TEST_CONFIG_PRIORITY_PATH);
}

/* 测试 config_print：验证输出不崩溃 */
void test_config_print_basic(void) {
    agent_config_t config;
    config_init(&config);
    strcpy(config.token, "print_test_token");
    strcpy(config.endpoint, "wss://print.example.com");

    /* 调用 config_print，不崩溃即通过 */
    config_print(&config);
    TEST_PASS();
}

/* 测试 config_print 传入 NULL */
void test_config_print_null(void) {
    config_print(NULL);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_config_init_defaults);
    RUN_TEST(test_config_init_null);
    RUN_TEST(test_config_load_from_env_strings);
    RUN_TEST(test_config_load_from_env_numbers_and_bools);
    RUN_TEST(test_config_load_from_env_bool_variants);
    RUN_TEST(test_config_load_from_env_null);
    RUN_TEST(test_config_load_from_env_keep_defaults);
    RUN_TEST(test_config_load_from_file_basic);
    RUN_TEST(test_config_load_from_file_not_found);
    RUN_TEST(test_config_load_from_file_invalid_json);
    RUN_TEST(test_config_load_from_file_empty_object);
    RUN_TEST(test_config_load_from_file_null_args);
    RUN_TEST(test_config_priority_env_over_file);
    RUN_TEST(test_config_priority_file_over_defaults);
    RUN_TEST(test_config_print_basic);
    RUN_TEST(test_config_print_null);

    return UNITY_END();
}
