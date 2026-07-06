/*
 * test_report.c - Report generation module unit tests
 *
 * Test scope:
 *   - report_generate produces JSON containing all required fields (cpu/ram/swap/load/disk/network/connections/uptime/process)
 *   - report_generate_basic_info produces JSON containing all basic info fields
 *   - Verify JSON format validity (using cJSON_Parse)
 *   - Verify field types are correct (numeric fields are numbers, string fields are strings)
 *
 * Note: report_generate and report_generate_basic_info invoke monitoring functions
 * to read the /proc filesystem; tests run on a real Linux system.
 */

#include "unity.h"
#include "report.h"
#include "monitoring.h"
#include "config.h"
#include "cJSON.h"
#include "v2.h"
#include "jsonrpc.h"

#include <string.h>
#include <stdlib.h>

/* Report buffer size */
#define REPORT_BUF_SIZE 8192

void setUp(void) {
    /* Set global config pointer for use by the monitoring module */
    monitoring_set_config(NULL);
}

void tearDown(void) {
    monitoring_set_config(NULL);
}

/* ====== report_generate tests ====== */

/* Test report_generate: verify return value and JSON format validity */
void test_report_generate_valid_json(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate(&config, buf, sizeof(buf));

    TEST_ASSERT_TRUE(len > 0);

    /* Use cJSON_Parse to verify JSON format validity */
    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));

    cJSON_Delete(root);
}

/* Test report_generate: verify all required fields are present */
void test_report_generate_required_fields(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate(&config, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* Verify cpu field exists and is an object */
    cJSON *cpu = cJSON_GetObjectItem(root, "cpu");
    TEST_ASSERT_NOT_NULL(cpu);
    TEST_ASSERT_TRUE(cJSON_IsObject(cpu));

    /* Verify cpu.usage exists and is a number */
    cJSON *cpu_usage = cJSON_GetObjectItem(cpu, "usage");
    TEST_ASSERT_NOT_NULL(cpu_usage);
    TEST_ASSERT_TRUE(cJSON_IsNumber(cpu_usage));

    /* Verify ram field exists and is an object */
    cJSON *ram = cJSON_GetObjectItem(root, "ram");
    TEST_ASSERT_NOT_NULL(ram);
    TEST_ASSERT_TRUE(cJSON_IsObject(ram));

    /* Verify ram.total and ram.used are numbers */
    cJSON *ram_total = cJSON_GetObjectItem(ram, "total");
    TEST_ASSERT_NOT_NULL(ram_total);
    TEST_ASSERT_TRUE(cJSON_IsNumber(ram_total));

    cJSON *ram_used = cJSON_GetObjectItem(ram, "used");
    TEST_ASSERT_NOT_NULL(ram_used);
    TEST_ASSERT_TRUE(cJSON_IsNumber(ram_used));

    /* Verify swap field exists and is an object */
    cJSON *swap = cJSON_GetObjectItem(root, "swap");
    TEST_ASSERT_NOT_NULL(swap);
    TEST_ASSERT_TRUE(cJSON_IsObject(swap));

    /* Verify load field exists and is an object */
    cJSON *load = cJSON_GetObjectItem(root, "load");
    TEST_ASSERT_NOT_NULL(load);
    TEST_ASSERT_TRUE(cJSON_IsObject(load));

    /* Verify load.load1/load5/load15 are numbers */
    cJSON *load1 = cJSON_GetObjectItem(load, "load1");
    TEST_ASSERT_NOT_NULL(load1);
    TEST_ASSERT_TRUE(cJSON_IsNumber(load1));

    /* Verify disk field exists and is an object */
    cJSON *disk = cJSON_GetObjectItem(root, "disk");
    TEST_ASSERT_NOT_NULL(disk);
    TEST_ASSERT_TRUE(cJSON_IsObject(disk));

    /* Verify network field exists and is an object */
    cJSON *network = cJSON_GetObjectItem(root, "network");
    TEST_ASSERT_NOT_NULL(network);
    TEST_ASSERT_TRUE(cJSON_IsObject(network));

    /* Verify connections field exists and is an object */
    cJSON *connections = cJSON_GetObjectItem(root, "connections");
    TEST_ASSERT_NOT_NULL(connections);
    TEST_ASSERT_TRUE(cJSON_IsObject(connections));

    /* Verify uptime is a number */
    cJSON *uptime = cJSON_GetObjectItem(root, "uptime");
    TEST_ASSERT_NOT_NULL(uptime);
    TEST_ASSERT_TRUE(cJSON_IsNumber(uptime));

    /* Verify process is a number */
    cJSON *process = cJSON_GetObjectItem(root, "process");
    TEST_ASSERT_NOT_NULL(process);
    TEST_ASSERT_TRUE(cJSON_IsNumber(process));

    /* Verify message is a string */
    cJSON *message = cJSON_GetObjectItem(root, "message");
    TEST_ASSERT_NOT_NULL(message);
    TEST_ASSERT_TRUE(cJSON_IsString(message));

    cJSON_Delete(root);
}

/* Test report_generate: verify field value sanity */
void test_report_generate_field_values(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    report_generate(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* CPU usage should be in [0, 100] range */
    cJSON *cpu_obj = cJSON_GetObjectItem(root, "cpu");
    TEST_ASSERT_NOT_NULL(cpu_obj);
    cJSON *cpu_usage = cJSON_GetObjectItem(cpu_obj, "usage");
    TEST_ASSERT_NOT_NULL(cpu_usage);
    TEST_ASSERT_TRUE(cpu_usage->valuedouble >= 0.0);
    TEST_ASSERT_TRUE(cpu_usage->valuedouble <= 100.0);

    /* Total memory should be >= 0 */
    cJSON *ram = cJSON_GetObjectItem(root, "ram");
    cJSON *ram_total = cJSON_GetObjectItem(ram, "total");
    TEST_ASSERT_TRUE(ram_total->valuedouble >= 0.0);

    /* Used memory should be <= total memory */
    cJSON *ram_used = cJSON_GetObjectItem(ram, "used");
    TEST_ASSERT_TRUE(ram_used->valuedouble <= ram_total->valuedouble);

    /* TCP connection count should be >= 0 */
    cJSON *connections = cJSON_GetObjectItem(root, "connections");
    cJSON *tcp = cJSON_GetObjectItem(connections, "tcp");
    TEST_ASSERT_TRUE(tcp->valuedouble >= 0.0);

    /* uptime should be >= 0 */
    cJSON *uptime = cJSON_GetObjectItem(root, "uptime");
    TEST_ASSERT_TRUE(uptime->valuedouble >= 0.0);

    /* process should be >= 0 */
    cJSON *process = cJSON_GetObjectItem(root, "process");
    TEST_ASSERT_TRUE(process->valuedouble >= 0.0);

    cJSON_Delete(root);
}

/* Test report_generate with NULL arguments */
void test_report_generate_null_args(void) {
    agent_config_t config;
    config_init(&config);
    char buf[REPORT_BUF_SIZE];

    TEST_ASSERT_EQUAL_INT(-1, report_generate(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate(&config, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate(&config, buf, 0));
}

/* Test report_generate: verify network subfields */
void test_report_generate_network_fields(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    report_generate(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    cJSON *network = cJSON_GetObjectItem(root, "network");
    TEST_ASSERT_NOT_NULL(network);

    /* Verify up/down/totalUp/totalDown fields exist and are numbers */
    const char *net_fields[] = {"up", "down", "totalUp", "totalDown"};
    for (size_t i = 0; i < sizeof(net_fields) / sizeof(net_fields[0]); i++) {
        cJSON *field = cJSON_GetObjectItem(network, net_fields[i]);
        TEST_ASSERT_NOT_NULL(field);
        TEST_ASSERT_TRUE(cJSON_IsNumber(field));
    }

    cJSON_Delete(root);
}

/* Test report_generate: verify connections subfields */
void test_report_generate_connections_fields(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    report_generate(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    cJSON *connections = cJSON_GetObjectItem(root, "connections");
    TEST_ASSERT_NOT_NULL(connections);

    /* Verify tcp and udp fields exist and are numbers */
    cJSON *tcp = cJSON_GetObjectItem(connections, "tcp");
    TEST_ASSERT_NOT_NULL(tcp);
    TEST_ASSERT_TRUE(cJSON_IsNumber(tcp));

    cJSON *udp = cJSON_GetObjectItem(connections, "udp");
    TEST_ASSERT_NOT_NULL(udp);
    TEST_ASSERT_TRUE(cJSON_IsNumber(udp));

    cJSON_Delete(root);
}

/* Test report_generate: verify load subfields */
void test_report_generate_load_fields(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    report_generate(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    cJSON *load = cJSON_GetObjectItem(root, "load");
    TEST_ASSERT_NOT_NULL(load);

    /* Verify load1/load5/load15 fields exist and are numbers */
    const char *load_fields[] = {"load1", "load5", "load15"};
    for (size_t i = 0; i < sizeof(load_fields) / sizeof(load_fields[0]); i++) {
        cJSON *field = cJSON_GetObjectItem(load, load_fields[i]);
        TEST_ASSERT_NOT_NULL(field);
        TEST_ASSERT_TRUE(cJSON_IsNumber(field));
    }

    cJSON_Delete(root);
}

/* ====== report_generate_basic_info tests ====== */

/* Test report_generate_basic_info: verify JSON format validity */
void test_report_generate_basic_info_valid_json(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate_basic_info(&config, buf, sizeof(buf));

    TEST_ASSERT_TRUE(len > 0);

    /* Use cJSON_Parse to verify JSON format validity */
    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));

    cJSON_Delete(root);
}

/* Test report_generate_basic_info: verify all required fields are present */
void test_report_generate_basic_info_required_fields(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate_basic_info(&config, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* Verify string fields exist and are of string type */
    const char *string_fields[] = {
        "cpu_name", "arch", "os", "kernel_version",
        "ipv4", "ipv6", "gpu_name", "virtualization", "version"
    };
    for (size_t i = 0; i < sizeof(string_fields) / sizeof(string_fields[0]); i++) {
        cJSON *field = cJSON_GetObjectItem(root, string_fields[i]);
        TEST_ASSERT_NOT_NULL(field);
        TEST_ASSERT_TRUE(cJSON_IsString(field));
    }

    /* Verify numeric fields exist and are of numeric type */
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

/* Test report_generate_basic_info: verify field value sanity */
void test_report_generate_basic_info_field_values(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    report_generate_basic_info(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* CPU name should be non-empty */
    cJSON *cpu_name = cJSON_GetObjectItem(root, "cpu_name");
    TEST_ASSERT_TRUE(strlen(cpu_name->valuestring) > 0);

    /* Architecture should be non-empty */
    cJSON *arch = cJSON_GetObjectItem(root, "arch");
    TEST_ASSERT_TRUE(strlen(arch->valuestring) > 0);

    /* OS should be non-empty */
    cJSON *os = cJSON_GetObjectItem(root, "os");
    TEST_ASSERT_TRUE(strlen(os->valuestring) > 0);

    /* Kernel version should be non-empty */
    cJSON *kernel = cJSON_GetObjectItem(root, "kernel_version");
    TEST_ASSERT_TRUE(strlen(kernel->valuestring) > 0);

    /* CPU core count should be > 0 */
    cJSON *cpu_cores = cJSON_GetObjectItem(root, "cpu_cores");
    TEST_ASSERT_TRUE(cpu_cores->valueint > 0);

    /* Total memory should be >= 0 */
    cJSON *mem_total = cJSON_GetObjectItem(root, "mem_total");
    TEST_ASSERT_TRUE(mem_total->valuedouble >= 0.0);

    /* Version should be a non-empty string */
    cJSON *version = cJSON_GetObjectItem(root, "version");
    TEST_ASSERT_TRUE(strlen(version->valuestring) > 0);

    cJSON_Delete(root);
}

/* Test report_generate_basic_info with NULL arguments */
void test_report_generate_basic_info_null_args(void) {
    agent_config_t config;
    config_init(&config);
    char buf[REPORT_BUF_SIZE];

    TEST_ASSERT_EQUAL_INT(-1, report_generate_basic_info(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate_basic_info(&config, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate_basic_info(&config, buf, 0));
}

/* Test report_generate_basic_info: with custom IPv4/IPv6 */
void test_report_generate_basic_info_custom_ip(void) {
    agent_config_t config;
    config_init(&config);
    strncpy(config.custom_ipv4, "10.0.0.1", sizeof(config.custom_ipv4) - 1);
    strncpy(config.custom_ipv6, "fe80::1", sizeof(config.custom_ipv6) - 1);

    char buf[REPORT_BUF_SIZE];
    report_generate_basic_info(&config, buf, sizeof(buf));

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* Verify custom IP is used */
    cJSON *ipv4 = cJSON_GetObjectItem(root, "ipv4");
    TEST_ASSERT_EQUAL_STRING("10.0.0.1", ipv4->valuestring);

    cJSON *ipv6 = cJSON_GetObjectItem(root, "ipv6");
    TEST_ASSERT_EQUAL_STRING("fe80::1", ipv6->valuestring);

    cJSON_Delete(root);
}

/* Test report_generate: returns -1 when the buffer is too small.
 * A 1-byte buffer can only hold a NUL terminator; any non-trivial JSON
 * payload must be rejected to avoid truncation. */
void test_report_generate_buffer_too_small(void) {
    agent_config_t config;
    config_init(&config);

    char tiny_buf[1];
    TEST_ASSERT_EQUAL_INT(-1, report_generate(&config, tiny_buf, sizeof(tiny_buf)));

    /* A 16-byte buffer is still far smaller than the full report JSON. */
    char small_buf[16];
    TEST_ASSERT_EQUAL_INT(-1, report_generate(&config, small_buf, sizeof(small_buf)));
}

/* Test report_generate_basic_info: returns -1 when the buffer is too small. */
void test_report_generate_basic_info_buffer_too_small(void) {
    agent_config_t config;
    config_init(&config);

    char tiny_buf[1];
    TEST_ASSERT_EQUAL_INT(-1, report_generate_basic_info(&config, tiny_buf, sizeof(tiny_buf)));

    char small_buf[16];
    TEST_ASSERT_EQUAL_INT(-1, report_generate_basic_info(&config, small_buf, sizeof(small_buf)));
}

/* Test report_generate_basic_info: gpu_name must always be a string field,
 * never NULL. When gpu_get_name fails (no GPU, lspci missing, etc.) the
 * implementation falls back to an empty string, so the JSON field is always
 * present and of string type — verifying graceful degradation rather than
 * a crash or NULL pointer dereference in downstream consumers. */
void test_report_generate_basic_info_gpu_field_always_string(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate_basic_info(&config, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    cJSON *gpu_name = cJSON_GetObjectItem(root, "gpu_name");
    TEST_ASSERT_NOT_NULL(gpu_name);
    TEST_ASSERT_TRUE(cJSON_IsString(gpu_name));
    /* Whether the GPU is present or not, the field must be a valid string. */

    cJSON_Delete(root);
}

/* ====== report_generate_v2 tests ====== */

/* Test report_generate_v2: verify return value and JSON-RPC 2.0 structure. */
void test_report_generate_v2_jsonrpc_structure(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate_v2(&config, buf, sizeof(buf));

    TEST_ASSERT_TRUE(len > 0);

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_TRUE(cJSON_IsObject(root));

    /* jsonrpc field must be "2.0" */
    cJSON *jsonrpc = cJSON_GetObjectItem(root, "jsonrpc");
    TEST_ASSERT_NOT_NULL(jsonrpc);
    TEST_ASSERT_TRUE(cJSON_IsString(jsonrpc));
    TEST_ASSERT_EQUAL_STRING(JSONRPC_VERSION, jsonrpc->valuestring);

    /* method field must be AGENT_REPORT ("agent.report") */
    cJSON *method = cJSON_GetObjectItem(root, "method");
    TEST_ASSERT_NOT_NULL(method);
    TEST_ASSERT_TRUE(cJSON_IsString(method));
    TEST_ASSERT_EQUAL_STRING(AGENT_REPORT, method->valuestring);

    /* v2 notification must not carry an id field */
    cJSON *id = cJSON_GetObjectItem(root, "id");
    TEST_ASSERT_NULL(id);

    cJSON_Delete(root);
}

/* Test report_generate_v2: params.report contains the original report fields. */
void test_report_generate_v2_params_contains_report(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate_v2(&config, buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* params must exist and be an object */
    cJSON *params = cJSON_GetObjectItem(root, "params");
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_TRUE(cJSON_IsObject(params));

    /* params.report must exist and be an object */
    cJSON *report = cJSON_GetObjectItem(params, "report");
    TEST_ASSERT_NOT_NULL(report);
    TEST_ASSERT_TRUE(cJSON_IsObject(report));

    /* Verify report contains the key v1 report fields */
    cJSON *cpu = cJSON_GetObjectItem(report, "cpu");
    TEST_ASSERT_NOT_NULL(cpu);
    TEST_ASSERT_TRUE(cJSON_IsObject(cpu));

    cJSON *cpu_usage = cJSON_GetObjectItem(cpu, "usage");
    TEST_ASSERT_NOT_NULL(cpu_usage);
    TEST_ASSERT_TRUE(cJSON_IsNumber(cpu_usage));

    cJSON *ram = cJSON_GetObjectItem(report, "ram");
    TEST_ASSERT_NOT_NULL(ram);
    TEST_ASSERT_TRUE(cJSON_IsObject(ram));

    cJSON *uptime = cJSON_GetObjectItem(report, "uptime");
    TEST_ASSERT_NOT_NULL(uptime);
    TEST_ASSERT_TRUE(cJSON_IsNumber(uptime));

    cJSON *process = cJSON_GetObjectItem(report, "process");
    TEST_ASSERT_NOT_NULL(process);
    TEST_ASSERT_TRUE(cJSON_IsNumber(process));

    cJSON_Delete(root);
}

/* Test report_generate_v2: params.report is structurally isomorphic with
 * the v1 report.
 *
 * Note: report_generate internally invokes sampling functions such as
 * monitoring_get_cpu_info; two consecutive calls may return different
 * cpu.usage / network.up values because the sampling window advances.
 * Therefore this test only compares the field structure (field names
 * present), not the numeric values. */
void test_report_generate_v2_report_matches_v1(void) {
    agent_config_t config;
    config_init(&config);

    /* Generate the v1 report. */
    char v1_buf[REPORT_BUF_SIZE];
    int v1_len = report_generate(&config, v1_buf, sizeof(v1_buf));
    TEST_ASSERT_TRUE(v1_len > 0);

    cJSON *v1_root = cJSON_Parse(v1_buf);
    TEST_ASSERT_NOT_NULL(v1_root);

    /* Generate the v2 report (which internally calls report_generate). */
    char v2_buf[REPORT_BUF_SIZE];
    int v2_len = report_generate_v2(&config, v2_buf, sizeof(v2_buf));
    TEST_ASSERT_TRUE(v2_len > 0);

    cJSON *v2_root = cJSON_Parse(v2_buf);
    TEST_ASSERT_NOT_NULL(v2_root);

    /* Verify v2.params.report is structurally isomorphic with the v1 root. */
    cJSON *params = cJSON_GetObjectItem(v2_root, "params");
    TEST_ASSERT_NOT_NULL(params);
    cJSON *report = cJSON_GetObjectItem(params, "report");
    TEST_ASSERT_NOT_NULL(report);

    /* Compare field-name presence only (skip values to avoid flakiness from
     * sampling jitter between the two calls). */
    const char *top_fields[] = {"cpu", "ram", "swap", "load", "disk",
                                "network", "connections", "uptime",
                                "process", "message"};
    for (size_t i = 0; i < sizeof(top_fields) / sizeof(top_fields[0]); i++) {
        TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(v1_root, top_fields[i]));
        TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(report, top_fields[i]));
    }

    /* uptime is the system uptime in seconds; both calls happen within the
     * same second so the values should match. Only assert numeric type here
     * to avoid spurious failures when the call straddles a second boundary. */
    cJSON *v1_uptime = cJSON_GetObjectItem(v1_root, "uptime");
    cJSON *v2_uptime = cJSON_GetObjectItem(report, "uptime");
    TEST_ASSERT_NOT_NULL(v1_uptime);
    TEST_ASSERT_NOT_NULL(v2_uptime);
    TEST_ASSERT_TRUE(cJSON_IsNumber(v1_uptime));
    TEST_ASSERT_TRUE(cJSON_IsNumber(v2_uptime));

    cJSON_Delete(v1_root);
    cJSON_Delete(v2_root);
}

/* Test report_generate_v2 with NULL arguments. */
void test_report_generate_v2_null_args(void) {
    agent_config_t config;
    config_init(&config);
    char buf[REPORT_BUF_SIZE];

    TEST_ASSERT_EQUAL_INT(-1, report_generate_v2(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate_v2(&config, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate_v2(&config, buf, 0));
}

/* Test report_generate_v2: returns -1 when the buffer is too small. */
void test_report_generate_v2_buffer_too_small(void) {
    agent_config_t config;
    config_init(&config);

    /* A 1-byte buffer can only hold a NUL terminator, no JSON. */
    char tiny_buf[1];
    TEST_ASSERT_EQUAL_INT(-1, report_generate_v2(&config, tiny_buf, sizeof(tiny_buf)));
}

/* ====== report_generate_basic_info_v2 tests ====== */

/* Test report_generate_basic_info_v2: JSON-RPC 2.0 structure correctness. */
void test_report_generate_basic_info_v2_jsonrpc_structure(void) {
    agent_config_t config;
    config_init(&config);

    char buf[REPORT_BUF_SIZE];
    int len = report_generate_basic_info_v2(&config, buf, sizeof(buf));

    TEST_ASSERT_TRUE(len > 0);

    cJSON *root = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(root);

    /* jsonrpc field must be "2.0" */
    cJSON *jsonrpc = cJSON_GetObjectItem(root, "jsonrpc");
    TEST_ASSERT_NOT_NULL(jsonrpc);
    TEST_ASSERT_TRUE(cJSON_IsString(jsonrpc));
    TEST_ASSERT_EQUAL_STRING(JSONRPC_VERSION, jsonrpc->valuestring);

    /* method field must be AGENT_BASIC_INFO ("agent.basicInfo") */
    cJSON *method = cJSON_GetObjectItem(root, "method");
    TEST_ASSERT_NOT_NULL(method);
    TEST_ASSERT_TRUE(cJSON_IsString(method));
    TEST_ASSERT_EQUAL_STRING(AGENT_BASIC_INFO, method->valuestring);

    /* v2 notification must not carry an id field */
    cJSON *id = cJSON_GetObjectItem(root, "id");
    TEST_ASSERT_NULL(id);

    /* params.info must exist and be an object */
    cJSON *params = cJSON_GetObjectItem(root, "params");
    TEST_ASSERT_NOT_NULL(params);
    TEST_ASSERT_TRUE(cJSON_IsObject(params));

    cJSON *info = cJSON_GetObjectItem(params, "info");
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(cJSON_IsObject(info));

    /* Verify info contains the basic info fields */
    cJSON *cpu_name = cJSON_GetObjectItem(info, "cpu_name");
    TEST_ASSERT_NOT_NULL(cpu_name);
    TEST_ASSERT_TRUE(cJSON_IsString(cpu_name));

    cJSON *version = cJSON_GetObjectItem(info, "version");
    TEST_ASSERT_NOT_NULL(version);
    TEST_ASSERT_TRUE(cJSON_IsString(version));

    cJSON_Delete(root);
}

/* Test report_generate_basic_info_v2 with NULL arguments. */
void test_report_generate_basic_info_v2_null_args(void) {
    agent_config_t config;
    config_init(&config);
    char buf[REPORT_BUF_SIZE];

    TEST_ASSERT_EQUAL_INT(-1, report_generate_basic_info_v2(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate_basic_info_v2(&config, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, report_generate_basic_info_v2(&config, buf, 0));
}

int main(void) {
    UNITY_BEGIN();

#ifdef __linux__
    /* All report_generate tests invoke monitoring_get_* which read /proc;
     * skip on non-Linux platforms where /proc does not exist. */
    RUN_TEST(test_report_generate_valid_json);
    RUN_TEST(test_report_generate_required_fields);
    RUN_TEST(test_report_generate_field_values);
    RUN_TEST(test_report_generate_null_args);
    RUN_TEST(test_report_generate_network_fields);
    RUN_TEST(test_report_generate_connections_fields);
    RUN_TEST(test_report_generate_load_fields);

    RUN_TEST(test_report_generate_basic_info_valid_json);
    RUN_TEST(test_report_generate_basic_info_required_fields);
    RUN_TEST(test_report_generate_basic_info_field_values);
    RUN_TEST(test_report_generate_basic_info_null_args);
    RUN_TEST(test_report_generate_basic_info_custom_ip);
    RUN_TEST(test_report_generate_basic_info_buffer_too_small);
    RUN_TEST(test_report_generate_basic_info_gpu_field_always_string);

    RUN_TEST(test_report_generate_buffer_too_small);

    RUN_TEST(test_report_generate_v2_jsonrpc_structure);
    RUN_TEST(test_report_generate_v2_params_contains_report);
    RUN_TEST(test_report_generate_v2_report_matches_v1);
    RUN_TEST(test_report_generate_v2_null_args);
    RUN_TEST(test_report_generate_v2_buffer_too_small);

    RUN_TEST(test_report_generate_basic_info_v2_jsonrpc_structure);
    RUN_TEST(test_report_generate_basic_info_v2_null_args);
#endif

    return UNITY_END();
}
