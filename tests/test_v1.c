/*
 * test_v1.c - v1 protocol payload construction unit tests
 *
 * Test scope:
 *   - v1_build_report_payload: valid cJSON produces a compact JSON string
 *   - v1_build_basic_info_payload: valid cJSON produces a compact JSON string
 *   - NULL argument handling for both functions
 *   - Ownership: the caller's cJSON object is not freed by the build function
 *     (caller retains responsibility)
 *
 * Note: The v1 protocol uses raw JSON directly (no JSON-RPC wrapping), so the
 * tests verify that the output is the compact serialization of the input
 * object and that ownership semantics match the documented contract.
 */

#include "unity.h"
#include "v1.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

/* ====== v1_build_report_payload ====== */

/* A valid cJSON object produces a non-NULL compact JSON string. */
void test_v1_build_report_payload_valid_input(void) {
    cJSON *report = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(report);
    cJSON_AddNumberToObject(report, "cpu_usage", 12.5);
    cJSON_AddNumberToObject(report, "uptime", 3600);
    cJSON_AddStringToObject(report, "message", "");

    char *output = NULL;
    TEST_ASSERT_EQUAL_INT(0, v1_build_report_payload(report, &output));
    TEST_ASSERT_NOT_NULL(output);

    /* The output must be a compact (unformatted) JSON string. */
    cJSON *parsed = cJSON_Parse(output);
    TEST_ASSERT_NOT_NULL(parsed);
    cJSON *cpu = cJSON_GetObjectItem(parsed, "cpu_usage");
    TEST_ASSERT_NOT_NULL(cpu);
    TEST_ASSERT_EQUAL_DOUBLE(12.5, cpu->valuedouble);
    cJSON *uptime = cJSON_GetObjectItem(parsed, "uptime");
    TEST_ASSERT_NOT_NULL(uptime);
    TEST_ASSERT_EQUAL_INT(3600, uptime->valueint);
    cJSON *message = cJSON_GetObjectItem(parsed, "message");
    TEST_ASSERT_NOT_NULL(message);
    TEST_ASSERT_EQUAL_STRING("", message->valuestring);

    cJSON_Delete(parsed);
    cJSON_Delete(report);
    free(output);
}

/* NULL arguments return -1. */
void test_v1_build_report_payload_null_args(void) {
    cJSON *report = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(report);

    /* NULL output pointer: must return -1 without freeing the input. */
    TEST_ASSERT_EQUAL_INT(-1, v1_build_report_payload(report, NULL));

    /* NULL report_data: must return -1 and set *output to NULL. */
    char *output = (char *)0xdeadbeef;
    TEST_ASSERT_EQUAL_INT(-1, v1_build_report_payload(NULL, &output));
    TEST_ASSERT_NULL(output);

    cJSON_Delete(report);
}

/* The caller retains ownership of the cJSON object: the build function must
 * not free it. After calling v1_build_report_payload, the input object must
 * still be valid and re-usable. */
void test_v1_build_report_payload_does_not_free_input(void) {
    cJSON *report = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(report);
    cJSON_AddNumberToObject(report, "x", 1);

    char *output1 = NULL;
    TEST_ASSERT_EQUAL_INT(0, v1_build_report_payload(report, &output1));
    free(output1);

    /* Must be able to call again with the same object — if the first call had
     * freed it, this would crash or return garbage. */
    char *output2 = NULL;
    TEST_ASSERT_EQUAL_INT(0, v1_build_report_payload(report, &output2));
    TEST_ASSERT_NOT_NULL(output2);
    free(output2);

    /* The object must still be valid for inspection. */
    cJSON *x = cJSON_GetObjectItem(report, "x");
    TEST_ASSERT_NOT_NULL(x);
    TEST_ASSERT_EQUAL_INT(1, x->valueint);

    cJSON_Delete(report);
}

/* ====== v1_build_basic_info_payload ====== */

/* A valid cJSON object produces a non-NULL compact JSON string. */
void test_v1_build_basic_info_payload_valid_input(void) {
    cJSON *info = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(info);
    cJSON_AddStringToObject(info, "cpu_name", "ARM Cortex-A72");
    cJSON_AddNumberToObject(info, "cpu_cores", 4);
    cJSON_AddStringToObject(info, "version", "1.0.0");

    char *output = NULL;
    TEST_ASSERT_EQUAL_INT(0, v1_build_basic_info_payload(info, &output));
    TEST_ASSERT_NOT_NULL(output);

    cJSON *parsed = cJSON_Parse(output);
    TEST_ASSERT_NOT_NULL(parsed);
    cJSON *cpu_name = cJSON_GetObjectItem(parsed, "cpu_name");
    TEST_ASSERT_EQUAL_STRING("ARM Cortex-A72", cpu_name->valuestring);
    cJSON *cpu_cores = cJSON_GetObjectItem(parsed, "cpu_cores");
    TEST_ASSERT_EQUAL_INT(4, cpu_cores->valueint);
    cJSON *version = cJSON_GetObjectItem(parsed, "version");
    TEST_ASSERT_EQUAL_STRING("1.0.0", version->valuestring);

    cJSON_Delete(parsed);
    cJSON_Delete(info);
    free(output);
}

/* NULL arguments return -1. */
void test_v1_build_basic_info_payload_null_args(void) {
    cJSON *info = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(info);

    TEST_ASSERT_EQUAL_INT(-1, v1_build_basic_info_payload(info, NULL));

    char *output = (char *)0xdeadbeef;
    TEST_ASSERT_EQUAL_INT(-1, v1_build_basic_info_payload(NULL, &output));
    TEST_ASSERT_NULL(output);

    cJSON_Delete(info);
}

/* An empty cJSON object produces "{}" (the compact serialization of an empty
 * object). This documents the boundary behavior. */
void test_v1_build_report_payload_empty_object(void) {
    cJSON *report = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(report);

    char *output = NULL;
    TEST_ASSERT_EQUAL_INT(0, v1_build_report_payload(report, &output));
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_EQUAL_STRING("{}", output);

    cJSON_Delete(report);
    free(output);
}

int main(void) {
    UNITY_BEGIN();

    /* v1_build_report_payload */
    RUN_TEST(test_v1_build_report_payload_valid_input);
    RUN_TEST(test_v1_build_report_payload_null_args);
    RUN_TEST(test_v1_build_report_payload_does_not_free_input);
    RUN_TEST(test_v1_build_report_payload_empty_object);

    /* v1_build_basic_info_payload */
    RUN_TEST(test_v1_build_basic_info_payload_valid_input);
    RUN_TEST(test_v1_build_basic_info_payload_null_args);

    return UNITY_END();
}
