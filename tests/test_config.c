/*
 * test_config.c - Configuration module unit tests
 *
 * Test scope:
 *   - config_init default value initialization
 *   - config_load_from_env environment variable loading
 *   - config_load_from_file JSON file loading
 *   - Configuration priority (env vars > config file)
 */

#include "unity.h"
#include "config.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* UCI line parser helper (extracted from config.c for testability).
 * Not declared in config.h to keep the public API unchanged. */
extern int config_parse_uci_line(agent_config_t *config, const char *raw_line);

/* Temporary config file path */
#define TEST_CONFIG_FILE_PATH "/tmp/test_komari_config.json"
#define TEST_CONFIG_PRIORITY_PATH "/tmp/test_komari_config_priority.json"

void setUp(void) {
    /* Clear all related environment variables before each test to avoid mutual interference */
    unsetenv("AGENT_TOKEN");
    unsetenv("AGENT_ENDPOINT");
    unsetenv("AGENT_CUSTOM_DNS");
    unsetenv("AGENT_INCLUDE_NICS");
    unsetenv("AGENT_EXCLUDE_NICS");
    unsetenv("AGENT_CUSTOM_IPV4");
    unsetenv("AGENT_CUSTOM_IPV6");
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
    /* Clear environment variables after each test */
    unsetenv("AGENT_TOKEN");
    unsetenv("AGENT_ENDPOINT");
    unsetenv("AGENT_CUSTOM_DNS");
    unsetenv("AGENT_INCLUDE_NICS");
    unsetenv("AGENT_EXCLUDE_NICS");
    unsetenv("AGENT_CUSTOM_IPV4");
    unsetenv("AGENT_CUSTOM_IPV6");
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

/* Test config_init: verify all fields are correctly initialized to default values */
void test_config_init_defaults(void) {
    agent_config_t config;
    config_init(&config);

    /* Verify numeric field default values */
    TEST_ASSERT_EQUAL_DOUBLE(1.0, config.interval);
    TEST_ASSERT_EQUAL_INT(5, config.max_retries);
    TEST_ASSERT_EQUAL_INT(5, config.reconnect_interval);
    TEST_ASSERT_EQUAL_INT(30, config.info_report_interval);
    TEST_ASSERT_EQUAL_INT(0, config.month_rotate);

    /* Verify boolean field default values (all false) */
    TEST_ASSERT_FALSE(config.disable_auto_update);
    TEST_ASSERT_FALSE(config.disable_web_ssh);
    TEST_ASSERT_FALSE(config.ignore_unsafe_cert);
    TEST_ASSERT_FALSE(config.memory_include_cache);
    TEST_ASSERT_FALSE(config.memory_report_raw_used);
    TEST_ASSERT_FALSE(config.enable_gpu);
    TEST_ASSERT_FALSE(config.get_ip_addr_from_nic);
    TEST_ASSERT_FALSE(config.show_warning);

    /* Verify string field default values */
    TEST_ASSERT_EQUAL_STRING("", config.token);
    TEST_ASSERT_EQUAL_STRING("", config.endpoint);
    TEST_ASSERT_EQUAL_STRING("", config.custom_dns);
    TEST_ASSERT_EQUAL_STRING("", config.include_nics);
    TEST_ASSERT_EQUAL_STRING("", config.exclude_nics);
    TEST_ASSERT_EQUAL_STRING("", config.custom_ipv4);
    TEST_ASSERT_EQUAL_STRING("", config.custom_ipv6);
}

/* Test config_init with NULL: should not crash */
void test_config_init_null(void) {
    config_init(NULL);
    TEST_PASS();
}

/* Test config_load_from_env: set environment variables and verify string fields are loaded correctly */
void test_config_load_from_env_strings(void) {
    agent_config_t config;
    config_init(&config);

    setenv("AGENT_TOKEN", "test_token_12345", 1);
    setenv("AGENT_ENDPOINT", "wss://example.com/api/ws", 1);
    setenv("AGENT_CUSTOM_DNS", "8.8.8.8", 1);

    int ret = config_load_from_env(&config);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING("test_token_12345", config.token);
    TEST_ASSERT_EQUAL_STRING("wss://example.com/api/ws", config.endpoint);
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", config.custom_dns);
}

/* Test config_load_from_env: numeric and boolean field loading */
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

/* Test config_load_from_env: multiple representations of boolean values */
void test_config_load_from_env_bool_variants(void) {
    agent_config_t config;

    /* "yes" should be true */
    config_init(&config);
    setenv("AGENT_DISABLE_WEB_SSH", "yes", 1);
    config_load_from_env(&config);
    TEST_ASSERT_TRUE(config.disable_web_ssh);
    unsetenv("AGENT_DISABLE_WEB_SSH");

    /* "1" should be true */
    config_init(&config);
    setenv("AGENT_DISABLE_WEB_SSH", "1", 1);
    config_load_from_env(&config);
    TEST_ASSERT_TRUE(config.disable_web_ssh);
    unsetenv("AGENT_DISABLE_WEB_SSH");

    /* "TRUE" (uppercase) should be true */
    config_init(&config);
    setenv("AGENT_DISABLE_WEB_SSH", "TRUE", 1);
    config_load_from_env(&config);
    TEST_ASSERT_TRUE(config.disable_web_ssh);
    unsetenv("AGENT_DISABLE_WEB_SSH");

    /* "0" should be false */
    config_init(&config);
    setenv("AGENT_DISABLE_WEB_SSH", "0", 1);
    config_load_from_env(&config);
    TEST_ASSERT_FALSE(config.disable_web_ssh);
    unsetenv("AGENT_DISABLE_WEB_SSH");

    /* "no" should be false */
    config_init(&config);
    setenv("AGENT_DISABLE_WEB_SSH", "no", 1);
    config_load_from_env(&config);
    TEST_ASSERT_FALSE(config.disable_web_ssh);
    unsetenv("AGENT_DISABLE_WEB_SSH");
}

/* Test config_load_from_env with NULL */
void test_config_load_from_env_null(void) {
    int ret = config_load_from_env(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* Test config_load_from_env: keep default values when environment variables are not set */
void test_config_load_from_env_keep_defaults(void) {
    agent_config_t config;
    config_init(&config);

    int ret = config_load_from_env(&config);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Without environment variables set, default values should remain unchanged */
    TEST_ASSERT_EQUAL_DOUBLE(1.0, config.interval);
    TEST_ASSERT_EQUAL_INT(5, config.max_retries);
}

/* Test config_load_from_file: create a temporary JSON config file and verify fields are loaded correctly */
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
        "\"ignore_unsafe_cert\": true"
        "}";

    FILE *f = fopen(TEST_CONFIG_FILE_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(json_content, f);
    fclose(f);

    agent_config_t config;
    config_init(&config);

    int ret = config_load_from_file(&config, TEST_CONFIG_FILE_PATH);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Verify string fields */
    TEST_ASSERT_EQUAL_STRING("file_token_abc", config.token);
    TEST_ASSERT_EQUAL_STRING("wss://file.example.com/api", config.endpoint);
    TEST_ASSERT_EQUAL_STRING("1.1.1.1", config.custom_dns);

    /* Verify numeric fields */
    TEST_ASSERT_EQUAL_DOUBLE(3.5, config.interval);
    TEST_ASSERT_EQUAL_INT(15, config.max_retries);
    TEST_ASSERT_EQUAL_INT(20, config.reconnect_interval);
    TEST_ASSERT_EQUAL_INT(45, config.info_report_interval);

    /* Verify boolean fields */
    TEST_ASSERT_TRUE(config.disable_web_ssh);
    TEST_ASSERT_FALSE(config.enable_gpu);
    TEST_ASSERT_TRUE(config.ignore_unsafe_cert);

    remove(TEST_CONFIG_FILE_PATH);
}

/* Test config_load_from_file: return error when file does not exist */
void test_config_load_from_file_not_found(void) {
    agent_config_t config;
    config_init(&config);

    int ret = config_load_from_file(&config, "/nonexistent/path/config.json");
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* Test config_load_from_file: invalid JSON format */
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

/* Test config_load_from_file: empty JSON object */
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

    /* Empty JSON should not change default values */
    TEST_ASSERT_EQUAL_DOUBLE(1.0, config.interval);
    TEST_ASSERT_EQUAL_INT(5, config.max_retries);

    remove(TEST_CONFIG_FILE_PATH);
}

/* Test config_load_from_file with NULL arguments */
void test_config_load_from_file_null_args(void) {
    agent_config_t config;
    config_init(&config);

    TEST_ASSERT_EQUAL_INT(-1, config_load_from_file(NULL, TEST_CONFIG_FILE_PATH));
    TEST_ASSERT_EQUAL_INT(-1, config_load_from_file(&config, NULL));
}

/* Test configuration priority: environment variables override config file values
 *
 * Configuration load order: UCI -> config file -> environment variables -> command-line args
 * Later-loaded values override earlier ones. This test verifies env vars override config file.
 * Note: UCI and command-line args are out of test scope (UCI depends on OpenWrt environment,
 * command-line args are handled in main.c).
 */
void test_config_priority_env_over_file(void) {
    const char *json_content =
        "{"
        "\"token\": \"file_token\","
        "\"endpoint\": \"wss://file.example.com\","
        "\"max_retries\": 20"
        "}";

    FILE *f = fopen(TEST_CONFIG_PRIORITY_PATH, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs(json_content, f);
    fclose(f);

    agent_config_t config;
    config_init(&config);

    /* Step 1: load from file */
    int ret = config_load_from_file(&config, TEST_CONFIG_PRIORITY_PATH);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("file_token", config.token);
    TEST_ASSERT_EQUAL_INT(20, config.max_retries);

    /* Step 2: set environment variables to simulate command-line or runtime override */
    setenv("AGENT_TOKEN", "env_token_override", 1);
    setenv("AGENT_MAX_RETRIES", "99", 1);

    ret = config_load_from_env(&config);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Environment variables should override config file values */
    TEST_ASSERT_EQUAL_STRING("env_token_override", config.token);
    TEST_ASSERT_EQUAL_INT(99, config.max_retries);

    /* Fields not overridden by environment variables keep config file values */
    TEST_ASSERT_EQUAL_STRING("wss://file.example.com", config.endpoint);

    remove(TEST_CONFIG_PRIORITY_PATH);
}

/* Test configuration priority: config file overrides default values */
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

    /* Default value verification */
    TEST_ASSERT_EQUAL_DOUBLE(1.0, config.interval);
    TEST_ASSERT_EQUAL_STRING("", config.token);

    /* Override default values after loading from file */
    config_load_from_file(&config, TEST_CONFIG_PRIORITY_PATH);
    TEST_ASSERT_EQUAL_DOUBLE(5.0, config.interval);
    TEST_ASSERT_EQUAL_STRING("file_only_token", config.token);

    /* Fields not specified in the file keep default values */
    TEST_ASSERT_EQUAL_INT(5, config.max_retries);

    remove(TEST_CONFIG_PRIORITY_PATH);
}

/* Test config_print: verify output does not crash */
void test_config_print_basic(void) {
    agent_config_t config;
    config_init(&config);
    strcpy(config.token, "print_test_token");
    strcpy(config.endpoint, "wss://print.example.com");

    /* Call config_print, pass if no crash */
    config_print(&config);
    TEST_PASS();
}

/* Test config_print with NULL */
void test_config_print_null(void) {
    config_print(NULL);
    TEST_PASS();
}

/* ====== UCI line parsing tests ======
 *
 * config_load_from_uci depends on popen("uci show ..."), which cannot be tested
 * directly in a non-OpenWrt environment. The parsing logic has been extracted into
 * the config_parse_uci_line helper function. The following cases directly verify
 * this function, covering the core bug scenario (both package name and section
 * name contain dots).
 */

/* Test UCI line parsing: core bug scenario
 * Input komari-agent-c.komari-agent-c.token='xxx'
 * Old logic uses strchr to find the first dot, key is wrongly "komari-agent-c.token", branch does not match
 * New logic uses strrchr to find the last dot before '=', key is correctly "token" */
void test_config_parse_uci_line_token(void) {
    agent_config_t config;
    config_init(&config);

    int ret = config_parse_uci_line(&config, "komari-agent-c.komari-agent-c.token='uci_token_xyz'");
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("uci_token_xyz", config.token);
}

/* Test UCI line parsing: parse multiple fields line by line */
void test_config_parse_uci_line_multiple_fields(void) {
    agent_config_t config;
    config_init(&config);

    TEST_ASSERT_EQUAL_INT(0, config_parse_uci_line(&config, "komari-agent-c.komari-agent-c.endpoint='wss://uci.example.com/ws'"));
    TEST_ASSERT_EQUAL_INT(0, config_parse_uci_line(&config, "komari-agent-c.komari-agent-c.interval=2.5"));
    TEST_ASSERT_EQUAL_INT(0, config_parse_uci_line(&config, "komari-agent-c.komari-agent-c.disable_web_ssh='1'"));
    TEST_ASSERT_EQUAL_INT(0, config_parse_uci_line(&config, "komari-agent-c.komari-agent-c.enable_gpu='true'"));
    TEST_ASSERT_EQUAL_INT(0, config_parse_uci_line(&config, "komari-agent-c.komari-agent-c.max_retries=10"));
    TEST_ASSERT_EQUAL_INT(0, config_parse_uci_line(&config, "komari-agent-c.komari-agent-c.custom_dns='1.1.1.1'"));

    TEST_ASSERT_EQUAL_STRING("wss://uci.example.com/ws", config.endpoint);
    TEST_ASSERT_EQUAL_DOUBLE(2.5, config.interval);
    TEST_ASSERT_TRUE(config.disable_web_ssh);
    TEST_ASSERT_TRUE(config.enable_gpu);
    TEST_ASSERT_EQUAL_INT(10, config.max_retries);
    TEST_ASSERT_EQUAL_STRING("1.1.1.1", config.custom_dns);
}

/* Test UCI line parsing: unquoted value */
void test_config_parse_uci_line_unquoted_value(void) {
    agent_config_t config;
    config_init(&config);

    TEST_ASSERT_EQUAL_INT(0, config_parse_uci_line(&config, "komari-agent-c.komari-agent-c.token=plain_token"));
    TEST_ASSERT_EQUAL_STRING("plain_token", config.token);
}

/* Test UCI line parsing: unknown key does not override existing value */
void test_config_parse_uci_line_unknown_key_preserves_value(void) {
    agent_config_t config;
    config_init(&config);

    TEST_ASSERT_EQUAL_INT(0, config_parse_uci_line(&config, "komari-agent-c.komari-agent-c.token='keep_me'"));
    TEST_ASSERT_EQUAL_STRING("keep_me", config.token);

    /* Unknown options should be ignored and not corrupt existing token */
    TEST_ASSERT_EQUAL_INT(0, config_parse_uci_line(&config, "komari-agent-c.komari-agent-c.unknown_option='ignore'"));
    TEST_ASSERT_EQUAL_STRING("keep_me", config.token);
}

/* Test UCI line parsing: malformed lines and NULL arguments return -1 */
void test_config_parse_uci_line_malformed(void) {
    agent_config_t config;
    config_init(&config);

    /* Missing '=' */
    TEST_ASSERT_EQUAL_INT(-1, config_parse_uci_line(&config, "komari-agent-c.komari-agent-c.token"));
    /* Missing '.' (cannot extract option name) */
    TEST_ASSERT_EQUAL_INT(-1, config_parse_uci_line(&config, "token=value"));
    /* Empty line */
    TEST_ASSERT_EQUAL_INT(-1, config_parse_uci_line(&config, ""));
    /* NULL argument */
    TEST_ASSERT_EQUAL_INT(-1, config_parse_uci_line(NULL, "komari-agent-c.komari-agent-c.token='x'"));
    TEST_ASSERT_EQUAL_INT(-1, config_parse_uci_line(&config, NULL));
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

    /* UCI line parsing tests */
    RUN_TEST(test_config_parse_uci_line_token);
    RUN_TEST(test_config_parse_uci_line_multiple_fields);
    RUN_TEST(test_config_parse_uci_line_unquoted_value);
    RUN_TEST(test_config_parse_uci_line_unknown_key_preserves_value);
    RUN_TEST(test_config_parse_uci_line_malformed);

    return UNITY_END();
}
