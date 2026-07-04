/*
 * test_jsonrpc.c - JSON-RPC protocol module unit tests
 *
 * Test scope:
 *   - jsonrpc_new_notification notification construction and serialization
 *   - jsonrpc_new_request request construction and serialization
 *   - jsonrpc_parse_response response parsing
 *   - jsonrpc_parse_event event parsing
 *   - jsonrpc_build_report_payload report data packaging
 *   - jsonrpc_build_basic_info_payload basic info packaging
 *   - jsonrpc_build_ping_result_payload Ping result packaging
 *   - jsonrpc_build_report_request report request with ACK
 *   - Error handling (invalid JSON, NULL parameters)
 */

#include "unity.h"
#include "jsonrpc.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>

void setUp(void) {
}

void tearDown(void) {
}

/* ====== jsonrpc_new_notification tests ====== */

/* Test notification construction: verify JSON output format */
void test_jsonrpc_new_notification_basic(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "key", "value");

    cJSON *notif = jsonrpc_new_notification("agent.test", params);
    TEST_ASSERT_NOT_NULL(notif);

    /* Verify jsonrpc version */
    cJSON *jsonrpc = cJSON_GetObjectItem(notif, "jsonrpc");
    TEST_ASSERT_NOT_NULL(jsonrpc);
    TEST_ASSERT_TRUE(cJSON_IsString(jsonrpc));
    TEST_ASSERT_EQUAL_STRING("2.0", jsonrpc->valuestring);

    /* Verify method */
    cJSON *method = cJSON_GetObjectItem(notif, "method");
    TEST_ASSERT_NOT_NULL(method);
    TEST_ASSERT_TRUE(cJSON_IsString(method));
    TEST_ASSERT_EQUAL_STRING("agent.test", method->valuestring);

    /* Verify params exists */
    cJSON *p = cJSON_GetObjectItem(notif, "params");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(cJSON_IsObject(p));

    /* Verify notification does not contain id field */
    cJSON *id = cJSON_GetObjectItem(notif, "id");
    TEST_ASSERT_NULL(id);

    cJSON_Delete(notif);
}

/* Test notification construction: NULL params */
void test_jsonrpc_new_notification_null_params(void) {
    cJSON *notif = jsonrpc_new_notification("agent.test", NULL);
    TEST_ASSERT_NOT_NULL(notif);

    /* Verify jsonrpc and method */
    cJSON *jsonrpc = cJSON_GetObjectItem(notif, "jsonrpc");
    TEST_ASSERT_EQUAL_STRING("2.0", jsonrpc->valuestring);

    cJSON *method = cJSON_GetObjectItem(notif, "method");
    TEST_ASSERT_EQUAL_STRING("agent.test", method->valuestring);

    /* params should not exist */
    cJSON *p = cJSON_GetObjectItem(notif, "params");
    TEST_ASSERT_NULL(p);

    cJSON_Delete(notif);
}

/* Test notification construction: NULL method returns NULL */
void test_jsonrpc_new_notification_null_method(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON *notif = jsonrpc_new_notification(NULL, params);
    TEST_ASSERT_NULL(notif);

    /* params not taken over, need to free manually */
    cJSON_Delete(params);
}

/* ====== jsonrpc_new_request tests ====== */

/* Test request construction: verify JSON output format includes id */
void test_jsonrpc_new_request_basic(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "count", 42);

    cJSON *req = jsonrpc_new_request(100, "agent.query", params);
    TEST_ASSERT_NOT_NULL(req);

    /* Verify jsonrpc version */
    cJSON *jsonrpc = cJSON_GetObjectItem(req, "jsonrpc");
    TEST_ASSERT_EQUAL_STRING("2.0", jsonrpc->valuestring);

    /* Verify method */
    cJSON *method = cJSON_GetObjectItem(req, "method");
    TEST_ASSERT_EQUAL_STRING("agent.query", method->valuestring);

    /* Verify id exists and is a number */
    cJSON *id = cJSON_GetObjectItem(req, "id");
    TEST_ASSERT_NOT_NULL(id);
    TEST_ASSERT_TRUE(cJSON_IsNumber(id));
    TEST_ASSERT_EQUAL_INT(100, id->valueint);

    /* Verify params */
    cJSON *p = cJSON_GetObjectItem(req, "params");
    TEST_ASSERT_NOT_NULL(p);

    cJSON_Delete(req);
}

/* Test request construction: id is 0 */
void test_jsonrpc_new_request_zero_id(void) {
    cJSON *req = jsonrpc_new_request(0, "agent.test", NULL);
    TEST_ASSERT_NOT_NULL(req);

    cJSON *id = cJSON_GetObjectItem(req, "id");
    TEST_ASSERT_NOT_NULL(id);
    TEST_ASSERT_TRUE(cJSON_IsNumber(id));
    TEST_ASSERT_EQUAL_INT(0, id->valueint);

    cJSON_Delete(req);
}

/* Test request construction: NULL method returns NULL */
void test_jsonrpc_new_request_null_method(void) {
    cJSON *req = jsonrpc_new_request(1, NULL, NULL);
    TEST_ASSERT_NULL(req);
}

/* ====== jsonrpc_build_report_payload tests ====== */

/* Test BuildReportPayload: pass cJSON report data, verify output */
void test_jsonrpc_build_report_payload(void) {
    cJSON *report_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(report_data, "cpu", 55.5);
    cJSON_AddNumberToObject(report_data, "mem", 70.2);

    cJSON *payload = jsonrpc_build_report_payload(report_data);
    TEST_ASSERT_NOT_NULL(payload);

    /* Verify method is agent.report */
    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_REPORT, method->valuestring);

    /* Verify params contains report object */
    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);

    cJSON *report = cJSON_GetObjectItem(params, "report");
    TEST_ASSERT_NOT_NULL(report);
    TEST_ASSERT_TRUE(cJSON_IsObject(report));

    /* Verify report content */
    cJSON *cpu = cJSON_GetObjectItem(report, "cpu");
    TEST_ASSERT_NOT_NULL(cpu);
    TEST_ASSERT_TRUE(cJSON_IsNumber(cpu));

    cJSON_Delete(payload);
}

/* Test BuildReportPayload: NULL data */
void test_jsonrpc_build_report_payload_null_data(void) {
    cJSON *payload = jsonrpc_build_report_payload(NULL);
    TEST_ASSERT_NOT_NULL(payload);

    /* Verify method is still agent.report */
    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_REPORT, method->valuestring);

    /* params exists but report does not */
    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);

    cJSON *report = cJSON_GetObjectItem(params, "report");
    TEST_ASSERT_NULL(report);

    cJSON_Delete(payload);
}

/* ====== jsonrpc_build_basic_info_payload tests ====== */

/* Test BuildBasicInfoPayload: pass cJSON basic info, verify output */
void test_jsonrpc_build_basic_info_payload(void) {
    cJSON *info_data = cJSON_CreateObject();
    cJSON_AddStringToObject(info_data, "os", "OpenWrt");
    cJSON_AddStringToObject(info_data, "arch", "x86_64");

    cJSON *payload = jsonrpc_build_basic_info_payload(info_data);
    TEST_ASSERT_NOT_NULL(payload);

    /* Verify method is agent.basicInfo */
    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_BASIC_INFO, method->valuestring);

    /* Verify params contains info object */
    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);

    cJSON *info = cJSON_GetObjectItem(params, "info");
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(cJSON_IsObject(info));

    /* Verify info content */
    cJSON *os = cJSON_GetObjectItem(info, "os");
    TEST_ASSERT_EQUAL_STRING("OpenWrt", os->valuestring);

    cJSON_Delete(payload);
}

/* Test BuildBasicInfoPayload: NULL data */
void test_jsonrpc_build_basic_info_payload_null_data(void) {
    cJSON *payload = jsonrpc_build_basic_info_payload(NULL);
    TEST_ASSERT_NOT_NULL(payload);

    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_BASIC_INFO, method->valuestring);

    cJSON_Delete(payload);
}

/* ====== jsonrpc_build_ping_result_payload tests ====== */

/* Test BuildPingResultPayload */
void test_jsonrpc_build_ping_result_payload(void) {
    cJSON *ping_result = cJSON_CreateObject();
    cJSON_AddNumberToObject(ping_result, "value", 15);
    cJSON_AddStringToObject(ping_result, "type", "icmp");

    cJSON *payload = jsonrpc_build_ping_result_payload(200, ping_result);
    TEST_ASSERT_NOT_NULL(payload);

    /* Verify method is agent.pingResult */
    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_PING_RESULT, method->valuestring);

    /* Verify id */
    cJSON *id = cJSON_GetObjectItem(payload, "id");
    TEST_ASSERT_EQUAL_INT(200, id->valueint);

    /* Verify params is directly ping_result (not nested under a field) */
    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);
    cJSON *value = cJSON_GetObjectItem(params, "value");
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL_INT(15, value->valueint);

    cJSON_Delete(payload);
}

/* ====== jsonrpc_build_report_request tests ====== */

/* Test BuildReportRequest: report request with ACK */
void test_jsonrpc_build_report_request(void) {
    cJSON *report_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(report_data, "cpu", 30.0);

    int ack_ids[] = {1, 2, 3};

    cJSON *payload = jsonrpc_build_report_request(300, report_data, ack_ids, 3);
    TEST_ASSERT_NOT_NULL(payload);

    /* Verify method is agent.report */
    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_REPORT, method->valuestring);

    /* Verify id */
    cJSON *id = cJSON_GetObjectItem(payload, "id");
    TEST_ASSERT_EQUAL_INT(300, id->valueint);

    /* Verify params contains report */
    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);

    cJSON *report = cJSON_GetObjectItem(params, "report");
    TEST_ASSERT_NOT_NULL(report);

    /* Verify ack_event_ids array */
    cJSON *ack_array = cJSON_GetObjectItem(params, "ack_event_ids");
    TEST_ASSERT_NOT_NULL(ack_array);
    TEST_ASSERT_TRUE(cJSON_IsArray(ack_array));
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(ack_array));

    /* Verify array content */
    cJSON *ack_item;
    int i = 0;
    cJSON_ArrayForEach(ack_item, ack_array) {
        TEST_ASSERT_EQUAL_INT(ack_ids[i], ack_item->valueint);
        i++;
    }

    cJSON_Delete(payload);
}

/* Test BuildReportRequest: no ACK */
void test_jsonrpc_build_report_request_no_acks(void) {
    cJSON *report_data = cJSON_CreateObject();

    cJSON *payload = jsonrpc_build_report_request(301, report_data, NULL, 0);
    TEST_ASSERT_NOT_NULL(payload);

    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);

    /* ack_event_ids should be an empty array */
    cJSON *ack_array = cJSON_GetObjectItem(params, "ack_event_ids");
    TEST_ASSERT_NOT_NULL(ack_array);
    TEST_ASSERT_TRUE(cJSON_IsArray(ack_array));
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(ack_array));

    cJSON_Delete(payload);
}

/* ====== jsonrpc_parse_response tests ====== */

/* Test Response parsing: success response */
void test_jsonrpc_parse_response_success(void) {
    const char *json_str =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"status\":\"ok\"}}";

    jsonrpc_response_t response;
    int ret = jsonrpc_parse_response(json_str, &response);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Verify jsonrpc version */
    TEST_ASSERT_NOT_NULL(response.jsonrpc);
    TEST_ASSERT_EQUAL_STRING("2.0", response.jsonrpc);

    /* Verify id */
    TEST_ASSERT_TRUE(response.has_id);
    TEST_ASSERT_EQUAL_INT(1, response.id);

    /* Verify result */
    TEST_ASSERT_NOT_NULL(response.result);
    cJSON *status = cJSON_GetObjectItem(response.result, "status");
    TEST_ASSERT_EQUAL_STRING("ok", status->valuestring);

    /* Verify error is NULL */
    TEST_ASSERT_NULL(response.error);

    jsonrpc_free_response(&response);
}

/* Test Response parsing: error response */
void test_jsonrpc_parse_response_error(void) {
    const char *json_str =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"error\":{\"code\":-32600,\"message\":\"Invalid Request\"}}";

    jsonrpc_response_t response;
    int ret = jsonrpc_parse_response(json_str, &response);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Verify error object */
    TEST_ASSERT_NOT_NULL(response.error);
    TEST_ASSERT_EQUAL_INT(-32600, response.error->code);
    TEST_ASSERT_EQUAL_STRING("Invalid Request", response.error->message);

    /* Verify result is NULL */
    TEST_ASSERT_NULL(response.result);

    jsonrpc_free_response(&response);
}

/* Test Response parsing: string id */
void test_jsonrpc_parse_response_string_id(void) {
    const char *json_str =
        "{\"jsonrpc\":\"2.0\",\"id\":\"req-abc\",\"result\":42}";

    jsonrpc_response_t response;
    int ret = jsonrpc_parse_response(json_str, &response);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_TRUE(response.has_id);
    /* String id "req-abc" converted via strtol, result is 0 */
    TEST_ASSERT_EQUAL_INT(0, response.id);

    jsonrpc_free_response(&response);
}

/* Test Response parsing: invalid JSON */
void test_jsonrpc_parse_response_invalid_json(void) {
    const char *json_str = "invalid json";

    jsonrpc_response_t response;
    int ret = jsonrpc_parse_response(json_str, &response);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* Test Response parsing: NULL parameters */
void test_jsonrpc_parse_response_null_args(void) {
    TEST_ASSERT_EQUAL_INT(-1, jsonrpc_parse_response("{}", NULL));
    TEST_ASSERT_EQUAL_INT(-1, jsonrpc_parse_response(NULL, NULL));
}

/* Test Response parsing: with error.data field */
void test_jsonrpc_parse_response_error_with_data(void) {
    const char *json_str =
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"error\":{\"code\":-32601,\"message\":\"Method not found\",\"data\":{\"detail\":\"unknown method\"}}}";

    jsonrpc_response_t response;
    int ret = jsonrpc_parse_response(json_str, &response);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_NOT_NULL(response.error);
    TEST_ASSERT_EQUAL_INT(-32601, response.error->code);
    TEST_ASSERT_EQUAL_STRING("Method not found", response.error->message);
    TEST_ASSERT_NOT_NULL(response.error->data);

    jsonrpc_free_response(&response);
}

/* ====== jsonrpc_parse_event tests ====== */

/* Test Event parsing: basic event */
void test_jsonrpc_parse_event_basic(void) {
    const char *json_str =
        "{\"id\":\"evt-001\",\"method\":\"agent.exec\",\"params\":{\"command\":\"ls\"}}";

    jsonrpc_event_t event;
    int ret = jsonrpc_parse_event(json_str, &event);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Verify id */
    TEST_ASSERT_NOT_NULL(event.id);
    TEST_ASSERT_EQUAL_STRING("evt-001", event.id);

    /* Verify method */
    TEST_ASSERT_NOT_NULL(event.method);
    TEST_ASSERT_EQUAL_STRING("agent.exec", event.method);

    /* Verify params */
    TEST_ASSERT_NOT_NULL(event.params);
    cJSON *cmd = cJSON_GetObjectItem(event.params, "command");
    TEST_ASSERT_EQUAL_STRING("ls", cmd->valuestring);

    /* Verify optional fields are NULL */
    TEST_ASSERT_NULL(event.created_at);
    TEST_ASSERT_NULL(event.expires_at);

    jsonrpc_free_event(&event);
}

/* Test Event parsing: with created_at and expires_at */
void test_jsonrpc_parse_event_with_timestamps(void) {
    const char *json_str =
        "{\"id\":\"evt-002\",\"method\":\"agent.ping\","
        "\"created_at\":\"2024-01-01T00:00:00Z\","
        "\"expires_at\":\"2024-12-31T23:59:59Z\"}";

    jsonrpc_event_t event;
    int ret = jsonrpc_parse_event(json_str, &event);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_EQUAL_STRING("evt-002", event.id);
    TEST_ASSERT_EQUAL_STRING("agent.ping", event.method);
    TEST_ASSERT_EQUAL_STRING("2024-01-01T00:00:00Z", event.created_at);
    TEST_ASSERT_EQUAL_STRING("2024-12-31T23:59:59Z", event.expires_at);

    jsonrpc_free_event(&event);
}

/* Test Event parsing: missing optional fields */
void test_jsonrpc_parse_event_minimal(void) {
    const char *json_str = "{\"method\":\"agent.message\"}";

    jsonrpc_event_t event;
    int ret = jsonrpc_parse_event(json_str, &event);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* id is NULL (not provided) */
    TEST_ASSERT_NULL(event.id);

    /* method should exist */
    TEST_ASSERT_NOT_NULL(event.method);
    TEST_ASSERT_EQUAL_STRING("agent.message", event.method);

    /* params is NULL */
    TEST_ASSERT_NULL(event.params);

    jsonrpc_free_event(&event);
}

/* Test Event parsing: invalid JSON */
void test_jsonrpc_parse_event_invalid_json(void) {
    const char *json_str = "not valid json";

    jsonrpc_event_t event;
    int ret = jsonrpc_parse_event(json_str, &event);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* Test Event parsing: NULL parameters */
void test_jsonrpc_parse_event_null_args(void) {
    TEST_ASSERT_EQUAL_INT(-1, jsonrpc_parse_event("{}", NULL));
    TEST_ASSERT_EQUAL_INT(-1, jsonrpc_parse_event(NULL, NULL));
}

/* ====== jsonrpc_free_* tests ====== */

/* Test jsonrpc_free_response with NULL */
void test_jsonrpc_free_response_null(void) {
    jsonrpc_free_response(NULL);
    TEST_PASS();
}

/* Test jsonrpc_free_event with NULL */
void test_jsonrpc_free_event_null(void) {
    jsonrpc_free_event(NULL);
    TEST_PASS();
}

/* Test jsonrpc_free_request with NULL */
void test_jsonrpc_free_request_null(void) {
    jsonrpc_free_request(NULL);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();

    /* Notification construction tests */
    RUN_TEST(test_jsonrpc_new_notification_basic);
    RUN_TEST(test_jsonrpc_new_notification_null_params);
    RUN_TEST(test_jsonrpc_new_notification_null_method);

    /* Request construction tests */
    RUN_TEST(test_jsonrpc_new_request_basic);
    RUN_TEST(test_jsonrpc_new_request_zero_id);
    RUN_TEST(test_jsonrpc_new_request_null_method);

    /* BuildReportPayload tests */
    RUN_TEST(test_jsonrpc_build_report_payload);
    RUN_TEST(test_jsonrpc_build_report_payload_null_data);

    /* BuildBasicInfoPayload tests */
    RUN_TEST(test_jsonrpc_build_basic_info_payload);
    RUN_TEST(test_jsonrpc_build_basic_info_payload_null_data);

    /* BuildPingResultPayload tests */
    RUN_TEST(test_jsonrpc_build_ping_result_payload);

    /* BuildReportRequest tests */
    RUN_TEST(test_jsonrpc_build_report_request);
    RUN_TEST(test_jsonrpc_build_report_request_no_acks);

    /* Response parsing tests */
    RUN_TEST(test_jsonrpc_parse_response_success);
    RUN_TEST(test_jsonrpc_parse_response_error);
    RUN_TEST(test_jsonrpc_parse_response_string_id);
    RUN_TEST(test_jsonrpc_parse_response_invalid_json);
    RUN_TEST(test_jsonrpc_parse_response_null_args);
    RUN_TEST(test_jsonrpc_parse_response_error_with_data);

    /* Event parsing tests */
    RUN_TEST(test_jsonrpc_parse_event_basic);
    RUN_TEST(test_jsonrpc_parse_event_with_timestamps);
    RUN_TEST(test_jsonrpc_parse_event_minimal);
    RUN_TEST(test_jsonrpc_parse_event_invalid_json);
    RUN_TEST(test_jsonrpc_parse_event_null_args);

    /* Free function NULL tests */
    RUN_TEST(test_jsonrpc_free_response_null);
    RUN_TEST(test_jsonrpc_free_event_null);
    RUN_TEST(test_jsonrpc_free_request_null);

    return UNITY_END();
}
