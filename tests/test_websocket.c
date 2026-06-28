/*
 * test_websocket.c - WebSocket 客户端单元测试
 *
 * 测试内容：
 *   - ws_client_create / ws_client_destroy 生命周期管理
 *   - ws_client_set_handler / ws_client_set_raw_handler / ws_client_set_user_data 设置器
 *   - ws_message_t JSON 消息解析（验证字段提取逻辑）
 *   - WebSocket 握手验证（Sec-WebSocket-Accept 计算，基于 RFC 6455）
 *   - 掩码计算验证
 *
 * 注意：帧解析（ws_recv_frame）、握手（ws_handshake）等内部函数为 static，
 * 无法直接单元测试。实际网络连接不在测试范围内。
 * JSON 消息解析逻辑在 ws_recv_thread 中实现，此处通过复现解析逻辑验证正确性。
 */

#include "unity.h"
#include "websocket.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <openssl/sha.h>

/* Base64 编码表 */
static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* 复现 websocket.c 中的 base64_encode 实现，用于测试 */
static void test_base64_encode(const unsigned char *data, size_t len, char *out) {
    size_t i, j;
    for (i = 0, j = 0; i < len; i += 3, j += 4) {
        unsigned int n = ((unsigned int)data[i]) << 16;
        if (i + 1 < len) n |= ((unsigned int)data[i + 1]) << 8;
        if (i + 2 < len) n |= data[i + 2];

        out[j] = base64_table[(n >> 18) & 0x3F];
        out[j + 1] = base64_table[(n >> 12) & 0x3F];
        out[j + 2] = (i + 1 < len) ? base64_table[(n >> 6) & 0x3F] : '=';
        out[j + 3] = (i + 2 < len) ? base64_table[n & 0x3F] : '=';
    }
    out[j] = '\0';
}

/*
 * 复现 websocket.c 中的 compute_accept_key 实现
 * 将 Sec-WebSocket-Key 与魔术字符串拼接，计算 SHA1，再 Base64 编码
 */
static void test_compute_accept_key(const char *key, char *accept_key) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    char combined[256];
    const char *magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    snprintf(combined, sizeof(combined), "%s%s", key, magic);
    SHA1((unsigned char *)combined, strlen(combined), hash);
    test_base64_encode(hash, SHA_DIGEST_LENGTH, accept_key);
}

/*
 * 应用 WebSocket 掩码（复现 ws_recv_frame 中的掩码逻辑）
 * 掩码规则：data[i] ^= mask[i % 4]
 */
static void test_apply_mask(unsigned char *data, size_t len, const unsigned char *mask) {
    for (size_t i = 0; i < len; i++) {
        data[i] ^= mask[i % 4];
    }
}

void setUp(void) {
}

void tearDown(void) {
}

/* ====== ws_client_create / ws_client_destroy 测试 ====== */

/* 测试 ws_client_create：使用有效配置创建客户端 */
void test_ws_client_create_with_config(void) {
    ws_client_config_t config;
    memset(&config, 0, sizeof(config));
    config.endpoint = strdup("ws://example.com/api");
    config.token = strdup("test_token");
    config.ignore_cert = false;
    config.max_retries = 5;
    config.reconnect_interval = 10;
    config.report_interval = 1.0;

    ws_client_t *client = ws_client_create(&config);
    TEST_ASSERT_NOT_NULL(client);

    /* 验证配置被正确复制 */
    TEST_ASSERT_EQUAL_STRING("ws://example.com/api", client->config.endpoint);
    TEST_ASSERT_EQUAL_STRING("test_token", client->config.token);
    TEST_ASSERT_FALSE(client->config.ignore_cert);
    TEST_ASSERT_EQUAL_INT(5, client->config.max_retries);
    TEST_ASSERT_EQUAL_INT(10, client->config.reconnect_interval);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, client->config.report_interval);

    /* 验证初始状态 */
    TEST_ASSERT_FALSE(client->connected);
    TEST_ASSERT_FALSE(client->should_stop);
    TEST_ASSERT_FALSE(client->use_tls);
    TEST_ASSERT_EQUAL_INT(-1, client->fd);

    ws_client_destroy(client);

    free(config.endpoint);
    free(config.token);
}

/* 测试 ws_client_create：使用 NULL 配置创建客户端 */
void test_ws_client_create_null_config(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    /* 验证初始状态 */
    TEST_ASSERT_FALSE(client->connected);
    TEST_ASSERT_FALSE(client->should_stop);
    TEST_ASSERT_EQUAL_INT(-1, client->fd);

    ws_client_destroy(client);
}

/* 测试 ws_client_destroy 传入 NULL */
void test_ws_client_destroy_null(void) {
    /* 不崩溃即通过 */
    ws_client_destroy(NULL);
    TEST_PASS();
}

/* ====== 设置器测试 ====== */

/* 测试 ws_client_set_handler */
void test_ws_client_set_handler(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    /* 初始 handler 应为 NULL */
    TEST_ASSERT_NULL(client->handler);

    /* 设置 handler（使用一个非 NULL 函数指针） */
    ws_client_set_handler(client, (ws_message_handler_t)0x1234);
    TEST_ASSERT_EQUAL_PTR((void *)0x1234, (void *)client->handler);

    /* 设置为 NULL */
    ws_client_set_handler(client, NULL);
    TEST_ASSERT_NULL(client->handler);

    ws_client_destroy(client);
}

/* 测试 ws_client_set_raw_handler */
void test_ws_client_set_raw_handler(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    /* 初始 raw_handler 应为 NULL */
    TEST_ASSERT_NULL(client->raw_handler);

    /* 设置 raw_handler */
    ws_client_set_raw_handler(client, (ws_raw_handler_t)0x5678);
    TEST_ASSERT_EQUAL_PTR((void *)0x5678, (void *)client->raw_handler);

    /* 设置为 NULL */
    ws_client_set_raw_handler(client, NULL);
    TEST_ASSERT_NULL(client->raw_handler);

    ws_client_destroy(client);
}

/* 测试 ws_client_set_user_data */
void test_ws_client_set_user_data(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    /* 初始 user_data 应为 NULL */
    TEST_ASSERT_NULL(client->user_data);

    /* 设置 user_data */
    int user_data = 42;
    ws_client_set_user_data(client, &user_data);
    TEST_ASSERT_EQUAL_PTR(&user_data, client->user_data);

    /* 设置为 NULL */
    ws_client_set_user_data(client, NULL);
    TEST_ASSERT_NULL(client->user_data);

    ws_client_destroy(client);
}

/* 测试 ws_client_set_handler 传入 NULL 客户端 */
void test_ws_client_set_handler_null_client(void) {
    /* 不崩溃即通过 */
    ws_client_set_handler(NULL, (ws_message_handler_t)0x1234);
    ws_client_set_raw_handler(NULL, (ws_raw_handler_t)0x5678);
    ws_client_set_user_data(NULL, (void *)0x9ABC);
    TEST_PASS();
}

/* ====== ws_client_stop / ws_client_disconnect 测试 ====== */

/* 测试 ws_client_stop：设置 should_stop 标志 */
void test_ws_client_stop(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_FALSE(client->should_stop);

    ws_client_stop(client);
    TEST_ASSERT_TRUE(client->should_stop);

    ws_client_destroy(client);
}

/* 测试 ws_client_stop 传入 NULL */
void test_ws_client_stop_null(void) {
    ws_client_stop(NULL);
    TEST_PASS();
}

/* 测试 ws_client_disconnect：未连接状态下断开 */
void test_ws_client_disconnect_not_connected(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    /* 未连接状态下断开，不应崩溃 */
    ws_client_disconnect(client);
    TEST_ASSERT_FALSE(client->connected);

    ws_client_destroy(client);
}

/* ====== ws_client_send 测试 ====== */

/* 测试 ws_client_send_text 未连接时返回错误 */
void test_ws_client_send_text_not_connected(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    const char *msg = "hello";
    int ret = ws_client_send_text(client, msg, strlen(msg));
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ws_client_destroy(client);
}

/* 测试 ws_client_send_text 传入 NULL 参数 */
void test_ws_client_send_text_null_args(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL_INT(-1, ws_client_send_text(NULL, "test", 4));
    TEST_ASSERT_EQUAL_INT(-1, ws_client_send_text(client, NULL, 4));

    ws_client_destroy(client);
}

/* 测试 ws_client_send_ping 未连接时返回错误 */
void test_ws_client_send_ping_not_connected(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    int ret = ws_client_send_ping(client);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ws_client_destroy(client);
}

/* ====== WebSocket 握手测试 ====== */

/*
 * 测试 Sec-WebSocket-Accept 计算
 * 使用 RFC 6455 Section 4.1 中的示例：
 *   Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
 *   预期 Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
 */
void test_ws_handshake_accept_key_rfc6455(void) {
    const char *key = "dGhlIHNhbXBsZSBub25jZQ==";
    char accept_key[64];

    test_compute_accept_key(key, accept_key);

    /* RFC 6455 规定的预期值 */
    TEST_ASSERT_EQUAL_STRING("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", accept_key);
}

/* 测试 Sec-WebSocket-Accept 计算使用不同的 Key */
void test_ws_handshake_accept_key_custom(void) {
    const char *key = "YWJjZGVmZ2hpamtsbW5vcA==";
    char accept_key[64];

    test_compute_accept_key(key, accept_key);

    /* 验证输出非空且长度合理（Base64 编码 SHA1 = 28 字符） */
    TEST_ASSERT_TRUE(strlen(accept_key) == 28);
}

/* ====== WebSocket 帧掩码测试 ====== */

/* 测试 WebSocket 客户端掩码应用：验证掩码可逆性 */
void test_ws_frame_mask_reversible(void) {
    unsigned char data[] = "Hello, WebSocket!";
    size_t data_len = strlen((char *)data);
    unsigned char original[32];
    memcpy(original, data, data_len);

    unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};

    /* 应用掩码 */
    test_apply_mask(data, data_len, mask);

    /* 掩码后数据应与原始数据不同 */
    TEST_ASSERT_NOT_EQUAL(0, memcmp(data, original, data_len));

    /* 再次应用相同掩码应恢复原始数据 */
    test_apply_mask(data, data_len, mask);
    TEST_ASSERT_EQUAL(0, memcmp(data, original, data_len));
}

/* 测试 WebSocket 帧掩码：空数据 */
void test_ws_frame_mask_empty(void) {
    unsigned char data[] = "";
    unsigned char mask[4] = {0xFF, 0xFF, 0xFF, 0xFF};

    /* 空数据掩码不应崩溃 */
    test_apply_mask(data, 0, mask);
    TEST_PASS();
}

/* 测试 WebSocket 帧掩码：数据长度非 4 的倍数 */
void test_ws_frame_mask_non_aligned(void) {
    unsigned char data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    size_t data_len = 7;
    unsigned char original[7];
    memcpy(original, data, data_len);

    unsigned char mask[4] = {0xAA, 0xBB, 0xCC, 0xDD};

    /* 应用掩码 */
    test_apply_mask(data, data_len, mask);

    /* 验证每个字节被正确掩码 */
    for (size_t i = 0; i < data_len; i++) {
        TEST_ASSERT_EQUAL_UINT8(original[i] ^ mask[i % 4], data[i]);
    }
}

/* ====== ws_message_t JSON 消息解析测试 ====== */

/*
 * 测试 WebSocket 消息 JSON 解析：构造包含 message 字段的消息
 * 此测试复现 ws_recv_thread 中的 JSON 解析逻辑
 */
void test_ws_message_parse_message_field(void) {
    const char *json_str = "{\"message\": \"hello world\"}";
    cJSON *root = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(root);

    ws_message_t msg;
    memset(&msg, 0, sizeof(msg));

    cJSON *item = cJSON_GetObjectItem(root, "message");
    if (item && cJSON_IsString(item)) {
        strncpy(msg.message, item->valuestring, sizeof(msg.message) - 1);
    }

    TEST_ASSERT_EQUAL_STRING("hello world", msg.message);

    cJSON_Delete(root);
}

/* 测试 WebSocket 消息 JSON 解析：terminal_id 字段（兼容 request_id） */
void test_ws_message_parse_terminal_id(void) {
    /* 测试 terminal_id 字段 */
    const char *json_str1 = "{\"terminal_id\": \"term-123\"}";
    cJSON *root1 = cJSON_Parse(json_str1);
    TEST_ASSERT_NOT_NULL(root1);

    ws_message_t msg;
    memset(&msg, 0, sizeof(msg));

    cJSON *item = cJSON_GetObjectItem(root1, "terminal_id");
    if (!item) item = cJSON_GetObjectItem(root1, "request_id");
    if (item && cJSON_IsString(item)) {
        strncpy(msg.terminal_id, item->valuestring, sizeof(msg.terminal_id) - 1);
    }

    TEST_ASSERT_EQUAL_STRING("term-123", msg.terminal_id);
    cJSON_Delete(root1);

    /* 测试 request_id 字段（兼容） */
    const char *json_str2 = "{\"request_id\": \"req-456\"}";
    cJSON *root2 = cJSON_Parse(json_str2);
    TEST_ASSERT_NOT_NULL(root2);

    memset(&msg, 0, sizeof(msg));
    item = cJSON_GetObjectItem(root2, "terminal_id");
    if (!item) item = cJSON_GetObjectItem(root2, "request_id");
    if (item && cJSON_IsString(item)) {
        strncpy(msg.terminal_id, item->valuestring, sizeof(msg.terminal_id) - 1);
    }

    TEST_ASSERT_EQUAL_STRING("req-456", msg.terminal_id);
    cJSON_Delete(root2);
}

/* 测试 WebSocket 消息 JSON 解析：exec_command 字段（兼容 command） */
void test_ws_message_parse_exec_command(void) {
    /* 测试 exec_command 字段 */
    const char *json_str1 = "{\"exec_command\": \"ls -la\"}";
    cJSON *root1 = cJSON_Parse(json_str1);
    TEST_ASSERT_NOT_NULL(root1);

    ws_message_t msg;
    memset(&msg, 0, sizeof(msg));

    cJSON *item = cJSON_GetObjectItem(root1, "exec_command");
    if (!item) item = cJSON_GetObjectItem(root1, "command");
    if (item && cJSON_IsString(item)) {
        strncpy(msg.exec_command, item->valuestring, sizeof(msg.exec_command) - 1);
    }

    TEST_ASSERT_EQUAL_STRING("ls -la", msg.exec_command);
    cJSON_Delete(root1);

    /* 测试 command 字段（兼容） */
    const char *json_str2 = "{\"command\": \"uname -a\"}";
    cJSON *root2 = cJSON_Parse(json_str2);
    TEST_ASSERT_NOT_NULL(root2);

    memset(&msg, 0, sizeof(msg));
    item = cJSON_GetObjectItem(root2, "exec_command");
    if (!item) item = cJSON_GetObjectItem(root2, "command");
    if (item && cJSON_IsString(item)) {
        strncpy(msg.exec_command, item->valuestring, sizeof(msg.exec_command) - 1);
    }

    TEST_ASSERT_EQUAL_STRING("uname -a", msg.exec_command);
    cJSON_Delete(root2);
}

/* 测试 WebSocket 消息 JSON 解析：exec_task_id 字段（兼容 task_id） */
void test_ws_message_parse_exec_task_id(void) {
    const char *json_str = "{\"exec_task_id\": \"task-789\"}";
    cJSON *root = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(root);

    ws_message_t msg;
    memset(&msg, 0, sizeof(msg));

    cJSON *item = cJSON_GetObjectItem(root, "exec_task_id");
    if (!item) item = cJSON_GetObjectItem(root, "task_id");
    if (item && cJSON_IsString(item)) {
        strncpy(msg.exec_task_id, item->valuestring, sizeof(msg.exec_task_id) - 1);
    }

    TEST_ASSERT_EQUAL_STRING("task-789", msg.exec_task_id);
    cJSON_Delete(root);
}

/* 测试 WebSocket 消息 JSON 解析：ping 相关字段 */
void test_ws_message_parse_ping_fields(void) {
    const char *json_str =
        "{\"ping_type\": \"icmp\", \"ping_target\": \"8.8.8.8\", \"ping_task_id\": 42}";
    cJSON *root = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(root);

    ws_message_t msg;
    memset(&msg, 0, sizeof(msg));

    cJSON *item;

    /* 解析 ping_type */
    if ((item = cJSON_GetObjectItem(root, "ping_type")) && cJSON_IsString(item)) {
        strncpy(msg.ping_type, item->valuestring, sizeof(msg.ping_type) - 1);
    }

    /* 解析 ping_target */
    if ((item = cJSON_GetObjectItem(root, "ping_target")) && cJSON_IsString(item)) {
        strncpy(msg.ping_target, item->valuestring, sizeof(msg.ping_target) - 1);
    }

    /* 解析 ping_task_id */
    if ((item = cJSON_GetObjectItem(root, "ping_task_id")) && cJSON_IsNumber(item)) {
        msg.ping_task_id = (uint32_t)item->valuedouble;
    }

    TEST_ASSERT_EQUAL_STRING("icmp", msg.ping_type);
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", msg.ping_target);
    TEST_ASSERT_EQUAL_UINT32(42, msg.ping_task_id);

    cJSON_Delete(root);
}

/* 测试 WebSocket 消息 JSON 解析：包含所有字段的完整消息 */
void test_ws_message_parse_full_message(void) {
    const char *json_str =
        "{"
        "\"message\": \"exec\","
        "\"terminal_id\": \"term-001\","
        "\"exec_command\": \"uptime\","
        "\"exec_task_id\": \"task-001\","
        "\"ping_type\": \"tcp\","
        "\"ping_target\": \"example.com\","
        "\"ping_task_id\": 100"
        "}";
    cJSON *root = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(root);

    ws_message_t msg;
    memset(&msg, 0, sizeof(msg));

    cJSON *item;

    if ((item = cJSON_GetObjectItem(root, "message")) && cJSON_IsString(item)) {
        strncpy(msg.message, item->valuestring, sizeof(msg.message) - 1);
    }

    item = cJSON_GetObjectItem(root, "terminal_id");
    if (!item) item = cJSON_GetObjectItem(root, "request_id");
    if (item && cJSON_IsString(item)) {
        strncpy(msg.terminal_id, item->valuestring, sizeof(msg.terminal_id) - 1);
    }

    item = cJSON_GetObjectItem(root, "exec_command");
    if (!item) item = cJSON_GetObjectItem(root, "command");
    if (item && cJSON_IsString(item)) {
        strncpy(msg.exec_command, item->valuestring, sizeof(msg.exec_command) - 1);
    }

    item = cJSON_GetObjectItem(root, "exec_task_id");
    if (!item) item = cJSON_GetObjectItem(root, "task_id");
    if (item && cJSON_IsString(item)) {
        strncpy(msg.exec_task_id, item->valuestring, sizeof(msg.exec_task_id) - 1);
    }

    if ((item = cJSON_GetObjectItem(root, "ping_type")) && cJSON_IsString(item)) {
        strncpy(msg.ping_type, item->valuestring, sizeof(msg.ping_type) - 1);
    }

    if ((item = cJSON_GetObjectItem(root, "ping_target")) && cJSON_IsString(item)) {
        strncpy(msg.ping_target, item->valuestring, sizeof(msg.ping_target) - 1);
    }

    if ((item = cJSON_GetObjectItem(root, "ping_task_id")) && cJSON_IsNumber(item)) {
        msg.ping_task_id = (uint32_t)item->valuedouble;
    }

    TEST_ASSERT_EQUAL_STRING("exec", msg.message);
    TEST_ASSERT_EQUAL_STRING("term-001", msg.terminal_id);
    TEST_ASSERT_EQUAL_STRING("uptime", msg.exec_command);
    TEST_ASSERT_EQUAL_STRING("task-001", msg.exec_task_id);
    TEST_ASSERT_EQUAL_STRING("tcp", msg.ping_type);
    TEST_ASSERT_EQUAL_STRING("example.com", msg.ping_target);
    TEST_ASSERT_EQUAL_UINT32(100, msg.ping_task_id);

    cJSON_Delete(root);
}

/* 测试 WebSocket 消息 JSON 解析：无效 JSON */
void test_ws_message_parse_invalid_json(void) {
    const char *json_str = "{ invalid json }";
    cJSON *root = cJSON_Parse(json_str);
    TEST_ASSERT_NULL(root);
}

/* 测试 ws_message_t 结构体大小 */
void test_ws_message_struct_size(void) {
    TEST_ASSERT_TRUE(sizeof(ws_message_t) > 0);
    /* message[32] + terminal_id[64] + exec_command[1024] + exec_task_id[64] +
       ping_task_id(4) + ping_type[16] + ping_target[256] */
    TEST_ASSERT_TRUE(sizeof(ws_message_t) >= 32 + 64 + 1024 + 64 + 4 + 16 + 256);
}

int main(void) {
    UNITY_BEGIN();

    /* 客户端创建/销毁测试 */
    RUN_TEST(test_ws_client_create_with_config);
    RUN_TEST(test_ws_client_create_null_config);
    RUN_TEST(test_ws_client_destroy_null);

    /* 设置器测试 */
    RUN_TEST(test_ws_client_set_handler);
    RUN_TEST(test_ws_client_set_raw_handler);
    RUN_TEST(test_ws_client_set_user_data);
    RUN_TEST(test_ws_client_set_handler_null_client);

    /* 停止/断开测试 */
    RUN_TEST(test_ws_client_stop);
    RUN_TEST(test_ws_client_stop_null);
    RUN_TEST(test_ws_client_disconnect_not_connected);

    /* 发送测试 */
    RUN_TEST(test_ws_client_send_text_not_connected);
    RUN_TEST(test_ws_client_send_text_null_args);
    RUN_TEST(test_ws_client_send_ping_not_connected);

    /* 握手测试 */
    RUN_TEST(test_ws_handshake_accept_key_rfc6455);
    RUN_TEST(test_ws_handshake_accept_key_custom);

    /* 掩码测试 */
    RUN_TEST(test_ws_frame_mask_reversible);
    RUN_TEST(test_ws_frame_mask_empty);
    RUN_TEST(test_ws_frame_mask_non_aligned);

    /* JSON 消息解析测试 */
    RUN_TEST(test_ws_message_parse_message_field);
    RUN_TEST(test_ws_message_parse_terminal_id);
    RUN_TEST(test_ws_message_parse_exec_command);
    RUN_TEST(test_ws_message_parse_exec_task_id);
    RUN_TEST(test_ws_message_parse_ping_fields);
    RUN_TEST(test_ws_message_parse_full_message);
    RUN_TEST(test_ws_message_parse_invalid_json);
    RUN_TEST(test_ws_message_struct_size);

    return UNITY_END();
}
