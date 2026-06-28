/*
 * test_jsonrpc.c - JSON-RPC 协议模块单元测试
 *
 * 测试内容：
 *   - jsonrpc_new_notification 通知构造和序列化
 *   - jsonrpc_new_request 请求构造和序列化
 *   - jsonrpc_parse_response 响应解析
 *   - jsonrpc_parse_event 事件解析
 *   - jsonrpc_build_report_payload 上报数据包装
 *   - jsonrpc_build_basic_info_payload 基础信息包装
 *   - jsonrpc_build_ping_result_payload Ping 结果包装
 *   - jsonrpc_build_report_request 带 ACK 的上报请求
 *   - 错误处理（无效 JSON、NULL 参数）
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

/* ====== jsonrpc_new_notification 测试 ====== */

/* 测试通知构造：验证 JSON 输出格式 */
void test_jsonrpc_new_notification_basic(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "key", "value");

    cJSON *notif = jsonrpc_new_notification("agent.test", params);
    TEST_ASSERT_NOT_NULL(notif);

    /* 验证 jsonrpc 版本 */
    cJSON *jsonrpc = cJSON_GetObjectItem(notif, "jsonrpc");
    TEST_ASSERT_NOT_NULL(jsonrpc);
    TEST_ASSERT_TRUE(cJSON_IsString(jsonrpc));
    TEST_ASSERT_EQUAL_STRING("2.0", jsonrpc->valuestring);

    /* 验证 method */
    cJSON *method = cJSON_GetObjectItem(notif, "method");
    TEST_ASSERT_NOT_NULL(method);
    TEST_ASSERT_TRUE(cJSON_IsString(method));
    TEST_ASSERT_EQUAL_STRING("agent.test", method->valuestring);

    /* 验证 params 存在 */
    cJSON *p = cJSON_GetObjectItem(notif, "params");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(cJSON_IsObject(p));

    /* 验证通知不包含 id 字段 */
    cJSON *id = cJSON_GetObjectItem(notif, "id");
    TEST_ASSERT_NULL(id);

    cJSON_Delete(notif);
}

/* 测试通知构造：NULL params */
void test_jsonrpc_new_notification_null_params(void) {
    cJSON *notif = jsonrpc_new_notification("agent.test", NULL);
    TEST_ASSERT_NOT_NULL(notif);

    /* 验证 jsonrpc 和 method */
    cJSON *jsonrpc = cJSON_GetObjectItem(notif, "jsonrpc");
    TEST_ASSERT_EQUAL_STRING("2.0", jsonrpc->valuestring);

    cJSON *method = cJSON_GetObjectItem(notif, "method");
    TEST_ASSERT_EQUAL_STRING("agent.test", method->valuestring);

    /* params 不应存在 */
    cJSON *p = cJSON_GetObjectItem(notif, "params");
    TEST_ASSERT_NULL(p);

    cJSON_Delete(notif);
}

/* 测试通知构造：NULL method 返回 NULL */
void test_jsonrpc_new_notification_null_method(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON *notif = jsonrpc_new_notification(NULL, params);
    TEST_ASSERT_NULL(notif);

    /* params 未被接管，需要手动释放 */
    cJSON_Delete(params);
}

/* ====== jsonrpc_new_request 测试 ====== */

/* 测试请求构造：验证 JSON 输出格式包含 id */
void test_jsonrpc_new_request_basic(void) {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "count", 42);

    cJSON *req = jsonrpc_new_request(100, "agent.query", params);
    TEST_ASSERT_NOT_NULL(req);

    /* 验证 jsonrpc 版本 */
    cJSON *jsonrpc = cJSON_GetObjectItem(req, "jsonrpc");
    TEST_ASSERT_EQUAL_STRING("2.0", jsonrpc->valuestring);

    /* 验证 method */
    cJSON *method = cJSON_GetObjectItem(req, "method");
    TEST_ASSERT_EQUAL_STRING("agent.query", method->valuestring);

    /* 验证 id 存在且为数值 */
    cJSON *id = cJSON_GetObjectItem(req, "id");
    TEST_ASSERT_NOT_NULL(id);
    TEST_ASSERT_TRUE(cJSON_IsNumber(id));
    TEST_ASSERT_EQUAL_INT(100, id->valueint);

    /* 验证 params */
    cJSON *p = cJSON_GetObjectItem(req, "params");
    TEST_ASSERT_NOT_NULL(p);

    cJSON_Delete(req);
}

/* 测试请求构造：id 为 0 */
void test_jsonrpc_new_request_zero_id(void) {
    cJSON *req = jsonrpc_new_request(0, "agent.test", NULL);
    TEST_ASSERT_NOT_NULL(req);

    cJSON *id = cJSON_GetObjectItem(req, "id");
    TEST_ASSERT_NOT_NULL(id);
    TEST_ASSERT_TRUE(cJSON_IsNumber(id));
    TEST_ASSERT_EQUAL_INT(0, id->valueint);

    cJSON_Delete(req);
}

/* 测试请求构造：NULL method 返回 NULL */
void test_jsonrpc_new_request_null_method(void) {
    cJSON *req = jsonrpc_new_request(1, NULL, NULL);
    TEST_ASSERT_NULL(req);
}

/* ====== jsonrpc_build_report_payload 测试 ====== */

/* 测试 BuildReportPayload：传入 cJSON 报告数据，验证输出 */
void test_jsonrpc_build_report_payload(void) {
    cJSON *report_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(report_data, "cpu", 55.5);
    cJSON_AddNumberToObject(report_data, "mem", 70.2);

    cJSON *payload = jsonrpc_build_report_payload(report_data);
    TEST_ASSERT_NOT_NULL(payload);

    /* 验证 method 为 agent.report */
    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_REPORT, method->valuestring);

    /* 验证 params 包含 report 对象 */
    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);

    cJSON *report = cJSON_GetObjectItem(params, "report");
    TEST_ASSERT_NOT_NULL(report);
    TEST_ASSERT_TRUE(cJSON_IsObject(report));

    /* 验证 report 内容 */
    cJSON *cpu = cJSON_GetObjectItem(report, "cpu");
    TEST_ASSERT_NOT_NULL(cpu);
    TEST_ASSERT_TRUE(cJSON_IsNumber(cpu));

    cJSON_Delete(payload);
}

/* 测试 BuildReportPayload：NULL 数据 */
void test_jsonrpc_build_report_payload_null_data(void) {
    cJSON *payload = jsonrpc_build_report_payload(NULL);
    TEST_ASSERT_NOT_NULL(payload);

    /* 验证 method 仍为 agent.report */
    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_REPORT, method->valuestring);

    /* params 存在但 report 不存在 */
    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);

    cJSON *report = cJSON_GetObjectItem(params, "report");
    TEST_ASSERT_NULL(report);

    cJSON_Delete(payload);
}

/* ====== jsonrpc_build_basic_info_payload 测试 ====== */

/* 测试 BuildBasicInfoPayload：传入 cJSON 基础信息，验证输出 */
void test_jsonrpc_build_basic_info_payload(void) {
    cJSON *info_data = cJSON_CreateObject();
    cJSON_AddStringToObject(info_data, "os", "OpenWrt");
    cJSON_AddStringToObject(info_data, "arch", "x86_64");

    cJSON *payload = jsonrpc_build_basic_info_payload(info_data);
    TEST_ASSERT_NOT_NULL(payload);

    /* 验证 method 为 agent.basicInfo */
    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_BASIC_INFO, method->valuestring);

    /* 验证 params 包含 info 对象 */
    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);

    cJSON *info = cJSON_GetObjectItem(params, "info");
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE(cJSON_IsObject(info));

    /* 验证 info 内容 */
    cJSON *os = cJSON_GetObjectItem(info, "os");
    TEST_ASSERT_EQUAL_STRING("OpenWrt", os->valuestring);

    cJSON_Delete(payload);
}

/* 测试 BuildBasicInfoPayload：NULL 数据 */
void test_jsonrpc_build_basic_info_payload_null_data(void) {
    cJSON *payload = jsonrpc_build_basic_info_payload(NULL);
    TEST_ASSERT_NOT_NULL(payload);

    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_BASIC_INFO, method->valuestring);

    cJSON_Delete(payload);
}

/* ====== jsonrpc_build_ping_result_payload 测试 ====== */

/* 测试 BuildPingResultPayload */
void test_jsonrpc_build_ping_result_payload(void) {
    cJSON *ping_result = cJSON_CreateObject();
    cJSON_AddNumberToObject(ping_result, "value", 15);
    cJSON_AddStringToObject(ping_result, "type", "icmp");

    cJSON *payload = jsonrpc_build_ping_result_payload(200, ping_result);
    TEST_ASSERT_NOT_NULL(payload);

    /* 验证 method 为 agent.pingResult */
    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_PING_RESULT, method->valuestring);

    /* 验证 id */
    cJSON *id = cJSON_GetObjectItem(payload, "id");
    TEST_ASSERT_EQUAL_INT(200, id->valueint);

    /* 验证 params 直接为 ping_result（不是嵌套在某个字段下） */
    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);
    cJSON *value = cJSON_GetObjectItem(params, "value");
    TEST_ASSERT_NOT_NULL(value);
    TEST_ASSERT_EQUAL_INT(15, value->valueint);

    cJSON_Delete(payload);
}

/* ====== jsonrpc_build_report_request 测试 ====== */

/* 测试 BuildReportRequest：带 ACK 的上报请求 */
void test_jsonrpc_build_report_request(void) {
    cJSON *report_data = cJSON_CreateObject();
    cJSON_AddNumberToObject(report_data, "cpu", 30.0);

    int ack_ids[] = {1, 2, 3};

    cJSON *payload = jsonrpc_build_report_request(300, report_data, ack_ids, 3);
    TEST_ASSERT_NOT_NULL(payload);

    /* 验证 method 为 agent.report */
    cJSON *method = cJSON_GetObjectItem(payload, "method");
    TEST_ASSERT_EQUAL_STRING(AGENT_REPORT, method->valuestring);

    /* 验证 id */
    cJSON *id = cJSON_GetObjectItem(payload, "id");
    TEST_ASSERT_EQUAL_INT(300, id->valueint);

    /* 验证 params 包含 report */
    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);

    cJSON *report = cJSON_GetObjectItem(params, "report");
    TEST_ASSERT_NOT_NULL(report);

    /* 验证 ack_event_ids 数组 */
    cJSON *ack_array = cJSON_GetObjectItem(params, "ack_event_ids");
    TEST_ASSERT_NOT_NULL(ack_array);
    TEST_ASSERT_TRUE(cJSON_IsArray(ack_array));
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetArraySize(ack_array));

    /* 验证数组内容 */
    cJSON *ack_item;
    int i = 0;
    cJSON_ArrayForEach(ack_item, ack_array) {
        TEST_ASSERT_EQUAL_INT(ack_ids[i], ack_item->valueint);
        i++;
    }

    cJSON_Delete(payload);
}

/* 测试 BuildReportRequest：无 ACK */
void test_jsonrpc_build_report_request_no_acks(void) {
    cJSON *report_data = cJSON_CreateObject();

    cJSON *payload = jsonrpc_build_report_request(301, report_data, NULL, 0);
    TEST_ASSERT_NOT_NULL(payload);

    cJSON *params = cJSON_GetObjectItem(payload, "params");
    TEST_ASSERT_NOT_NULL(params);

    /* ack_event_ids 应为空数组 */
    cJSON *ack_array = cJSON_GetObjectItem(params, "ack_event_ids");
    TEST_ASSERT_NOT_NULL(ack_array);
    TEST_ASSERT_TRUE(cJSON_IsArray(ack_array));
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(ack_array));

    cJSON_Delete(payload);
}

/* ====== jsonrpc_parse_response 测试 ====== */

/* 测试 Response 解析：成功响应 */
void test_jsonrpc_parse_response_success(void) {
    const char *json_str =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{\"status\":\"ok\"}}";

    jsonrpc_response_t response;
    int ret = jsonrpc_parse_response(json_str, &response);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* 验证 jsonrpc 版本 */
    TEST_ASSERT_NOT_NULL(response.jsonrpc);
    TEST_ASSERT_EQUAL_STRING("2.0", response.jsonrpc);

    /* 验证 id */
    TEST_ASSERT_TRUE(response.has_id);
    TEST_ASSERT_EQUAL_INT(1, response.id);

    /* 验证 result */
    TEST_ASSERT_NOT_NULL(response.result);
    cJSON *status = cJSON_GetObjectItem(response.result, "status");
    TEST_ASSERT_EQUAL_STRING("ok", status->valuestring);

    /* 验证 error 为 NULL */
    TEST_ASSERT_NULL(response.error);

    jsonrpc_free_response(&response);
}

/* 测试 Response 解析：错误响应 */
void test_jsonrpc_parse_response_error(void) {
    const char *json_str =
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"error\":{\"code\":-32600,\"message\":\"Invalid Request\"}}";

    jsonrpc_response_t response;
    int ret = jsonrpc_parse_response(json_str, &response);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* 验证 error 对象 */
    TEST_ASSERT_NOT_NULL(response.error);
    TEST_ASSERT_EQUAL_INT(-32600, response.error->code);
    TEST_ASSERT_EQUAL_STRING("Invalid Request", response.error->message);

    /* 验证 result 为 NULL */
    TEST_ASSERT_NULL(response.result);

    jsonrpc_free_response(&response);
}

/* 测试 Response 解析：带字符串 id */
void test_jsonrpc_parse_response_string_id(void) {
    const char *json_str =
        "{\"jsonrpc\":\"2.0\",\"id\":\"req-abc\",\"result\":42}";

    jsonrpc_response_t response;
    int ret = jsonrpc_parse_response(json_str, &response);
    TEST_ASSERT_EQUAL_INT(0, ret);

    TEST_ASSERT_TRUE(response.has_id);
    /* 字符串 id "req-abc" 通过 strtol 转换，结果为 0 */
    TEST_ASSERT_EQUAL_INT(0, response.id);

    jsonrpc_free_response(&response);
}

/* 测试 Response 解析：无效 JSON */
void test_jsonrpc_parse_response_invalid_json(void) {
    const char *json_str = "invalid json";

    jsonrpc_response_t response;
    int ret = jsonrpc_parse_response(json_str, &response);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* 测试 Response 解析：NULL 参数 */
void test_jsonrpc_parse_response_null_args(void) {
    TEST_ASSERT_EQUAL_INT(-1, jsonrpc_parse_response("{}", NULL));
    TEST_ASSERT_EQUAL_INT(-1, jsonrpc_parse_response(NULL, NULL));
}

/* 测试 Response 解析：带 error.data 字段 */
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

/* ====== jsonrpc_parse_event 测试 ====== */

/* 测试 Event 解析：基本事件 */
void test_jsonrpc_parse_event_basic(void) {
    const char *json_str =
        "{\"id\":\"evt-001\",\"method\":\"agent.exec\",\"params\":{\"command\":\"ls\"}}";

    jsonrpc_event_t event;
    int ret = jsonrpc_parse_event(json_str, &event);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* 验证 id */
    TEST_ASSERT_NOT_NULL(event.id);
    TEST_ASSERT_EQUAL_STRING("evt-001", event.id);

    /* 验证 method */
    TEST_ASSERT_NOT_NULL(event.method);
    TEST_ASSERT_EQUAL_STRING("agent.exec", event.method);

    /* 验证 params */
    TEST_ASSERT_NOT_NULL(event.params);
    cJSON *cmd = cJSON_GetObjectItem(event.params, "command");
    TEST_ASSERT_EQUAL_STRING("ls", cmd->valuestring);

    /* 验证可选字段为 NULL */
    TEST_ASSERT_NULL(event.created_at);
    TEST_ASSERT_NULL(event.expires_at);

    jsonrpc_free_event(&event);
}

/* 测试 Event 解析：带 created_at 和 expires_at */
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

/* 测试 Event 解析：缺少可选字段 */
void test_jsonrpc_parse_event_minimal(void) {
    const char *json_str = "{\"method\":\"agent.message\"}";

    jsonrpc_event_t event;
    int ret = jsonrpc_parse_event(json_str, &event);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* id 为 NULL（未提供） */
    TEST_ASSERT_NULL(event.id);

    /* method 应存在 */
    TEST_ASSERT_NOT_NULL(event.method);
    TEST_ASSERT_EQUAL_STRING("agent.message", event.method);

    /* params 为 NULL */
    TEST_ASSERT_NULL(event.params);

    jsonrpc_free_event(&event);
}

/* 测试 Event 解析：无效 JSON */
void test_jsonrpc_parse_event_invalid_json(void) {
    const char *json_str = "not valid json";

    jsonrpc_event_t event;
    int ret = jsonrpc_parse_event(json_str, &event);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

/* 测试 Event 解析：NULL 参数 */
void test_jsonrpc_parse_event_null_args(void) {
    TEST_ASSERT_EQUAL_INT(-1, jsonrpc_parse_event("{}", NULL));
    TEST_ASSERT_EQUAL_INT(-1, jsonrpc_parse_event(NULL, NULL));
}

/* ====== jsonrpc_free_* 测试 ====== */

/* 测试 jsonrpc_free_response 传入 NULL */
void test_jsonrpc_free_response_null(void) {
    jsonrpc_free_response(NULL);
    TEST_PASS();
}

/* 测试 jsonrpc_free_event 传入 NULL */
void test_jsonrpc_free_event_null(void) {
    jsonrpc_free_event(NULL);
    TEST_PASS();
}

/* 测试 jsonrpc_free_request 传入 NULL */
void test_jsonrpc_free_request_null(void) {
    jsonrpc_free_request(NULL);
    TEST_PASS();
}

int main(void) {
    UNITY_BEGIN();

    /* 通知构造测试 */
    RUN_TEST(test_jsonrpc_new_notification_basic);
    RUN_TEST(test_jsonrpc_new_notification_null_params);
    RUN_TEST(test_jsonrpc_new_notification_null_method);

    /* 请求构造测试 */
    RUN_TEST(test_jsonrpc_new_request_basic);
    RUN_TEST(test_jsonrpc_new_request_zero_id);
    RUN_TEST(test_jsonrpc_new_request_null_method);

    /* BuildReportPayload 测试 */
    RUN_TEST(test_jsonrpc_build_report_payload);
    RUN_TEST(test_jsonrpc_build_report_payload_null_data);

    /* BuildBasicInfoPayload 测试 */
    RUN_TEST(test_jsonrpc_build_basic_info_payload);
    RUN_TEST(test_jsonrpc_build_basic_info_payload_null_data);

    /* BuildPingResultPayload 测试 */
    RUN_TEST(test_jsonrpc_build_ping_result_payload);

    /* BuildReportRequest 测试 */
    RUN_TEST(test_jsonrpc_build_report_request);
    RUN_TEST(test_jsonrpc_build_report_request_no_acks);

    /* Response 解析测试 */
    RUN_TEST(test_jsonrpc_parse_response_success);
    RUN_TEST(test_jsonrpc_parse_response_error);
    RUN_TEST(test_jsonrpc_parse_response_string_id);
    RUN_TEST(test_jsonrpc_parse_response_invalid_json);
    RUN_TEST(test_jsonrpc_parse_response_null_args);
    RUN_TEST(test_jsonrpc_parse_response_error_with_data);

    /* Event 解析测试 */
    RUN_TEST(test_jsonrpc_parse_event_basic);
    RUN_TEST(test_jsonrpc_parse_event_with_timestamps);
    RUN_TEST(test_jsonrpc_parse_event_minimal);
    RUN_TEST(test_jsonrpc_parse_event_invalid_json);
    RUN_TEST(test_jsonrpc_parse_event_null_args);

    /* 释放函数 NULL 测试 */
    RUN_TEST(test_jsonrpc_free_response_null);
    RUN_TEST(test_jsonrpc_free_event_null);
    RUN_TEST(test_jsonrpc_free_request_null);

    return UNITY_END();
}
