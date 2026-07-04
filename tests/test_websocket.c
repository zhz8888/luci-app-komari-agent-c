/*
 * test_websocket.c - WebSocket client unit tests
 *
 * Test coverage:
 *   - ws_client_create / ws_client_destroy lifecycle management
 *   - ws_client_set_handler / ws_client_set_raw_handler / ws_client_set_user_data setters
 *   - ws_message_t JSON message parsing (verify field extraction logic)
 *   - WebSocket handshake validation (Sec-WebSocket-Accept computation, based on RFC 6455)
 *   - Mask computation validation
 *   - RFC 6455 §5.4 fragmented message accumulation logic (ws_fragment_accumulate)
 *   - v2 JSON-RPC event handling (ws_handle_v2_event): deduplication, method dispatch, ACK accumulation
 *
 * Note: Frame parsing (ws_recv_frame), handshake (ws_handshake) and other internal functions
 * are static and cannot be unit tested directly. Real network connections are out of scope.
 * JSON message parsing logic is implemented in ws_recv_thread; here we reproduce the parsing
 * logic to verify correctness.
 * Fragment accumulation logic (ws_fragment_accumulate) is exposed via the header file and
 * can be tested directly.
 * v2 event handling logic (ws_handle_v2_event) is also exposed via the header file and can
 * be tested directly.
 */

#include "unity.h"
#include "websocket.h"
#include "cJSON.h"
#include "v2.h"
#include "jsonrpc.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <openssl/sha.h>

/* Base64 encoding table */
static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Reproduce the base64_encode implementation from websocket.c for testing */
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
 * Reproduce the compute_accept_key implementation from websocket.c
 * Concatenate Sec-WebSocket-Key with the magic string, compute SHA1, then Base64 encode
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
 * Apply WebSocket mask (reproduce the mask logic from ws_recv_frame)
 * Mask rule: data[i] ^= mask[i % 4]
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

/* ====== ws_client_create / ws_client_destroy tests ====== */

/* Test ws_client_create: create client with valid config */
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

    /* Verify config is correctly copied */
    TEST_ASSERT_EQUAL_STRING("ws://example.com/api", client->config.endpoint);
    TEST_ASSERT_EQUAL_STRING("test_token", client->config.token);
    TEST_ASSERT_FALSE(client->config.ignore_cert);
    TEST_ASSERT_EQUAL_INT(5, client->config.max_retries);
    TEST_ASSERT_EQUAL_INT(10, client->config.reconnect_interval);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, client->config.report_interval);

    /* Verify initial state */
    TEST_ASSERT_FALSE(client->connected);
    TEST_ASSERT_FALSE(client->should_stop);
    TEST_ASSERT_FALSE(client->use_tls);
    TEST_ASSERT_EQUAL_INT(-1, client->fd);

    ws_client_destroy(client);

    free(config.endpoint);
    free(config.token);
}

/* Test ws_client_create: create client with NULL config */
void test_ws_client_create_null_config(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    /* Verify initial state */
    TEST_ASSERT_FALSE(client->connected);
    TEST_ASSERT_FALSE(client->should_stop);
    TEST_ASSERT_EQUAL_INT(-1, client->fd);

    ws_client_destroy(client);
}

/* Test ws_client_destroy with NULL */
void test_ws_client_destroy_null(void) {
    /* Passing means no crash */
    ws_client_destroy(NULL);
    TEST_PASS();
}

/* ====== Setter tests ====== */

/* Test ws_client_set_handler */
void test_ws_client_set_handler(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    /* Initial handler should be NULL */
    TEST_ASSERT_NULL(client->handler);

    /* Set handler (using a non-NULL function pointer) */
    ws_client_set_handler(client, (ws_message_handler_t)0x1234);
    TEST_ASSERT_EQUAL_PTR((void *)0x1234, (void *)client->handler);

    /* Set to NULL */
    ws_client_set_handler(client, NULL);
    TEST_ASSERT_NULL(client->handler);

    ws_client_destroy(client);
}

/* Test ws_client_set_raw_handler */
void test_ws_client_set_raw_handler(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    /* Initial raw_handler should be NULL */
    TEST_ASSERT_NULL(client->raw_handler);

    /* Set raw_handler */
    ws_client_set_raw_handler(client, (ws_raw_handler_t)0x5678);
    TEST_ASSERT_EQUAL_PTR((void *)0x5678, (void *)client->raw_handler);

    /* Set to NULL */
    ws_client_set_raw_handler(client, NULL);
    TEST_ASSERT_NULL(client->raw_handler);

    ws_client_destroy(client);
}

/* Test ws_client_set_user_data */
void test_ws_client_set_user_data(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    /* Initial user_data should be NULL */
    TEST_ASSERT_NULL(client->user_data);

    /* Set user_data */
    int user_data = 42;
    ws_client_set_user_data(client, &user_data);
    TEST_ASSERT_EQUAL_PTR(&user_data, client->user_data);

    /* Set to NULL */
    ws_client_set_user_data(client, NULL);
    TEST_ASSERT_NULL(client->user_data);

    ws_client_destroy(client);
}

/* Test ws_client_set_handler with NULL client */
void test_ws_client_set_handler_null_client(void) {
    /* Passing means no crash */
    ws_client_set_handler(NULL, (ws_message_handler_t)0x1234);
    ws_client_set_raw_handler(NULL, (ws_raw_handler_t)0x5678);
    ws_client_set_user_data(NULL, (void *)0x9ABC);
    TEST_PASS();
}

/* ====== ws_client_stop / ws_client_disconnect tests ====== */

/* Test ws_client_stop: sets the should_stop flag */
void test_ws_client_stop(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    TEST_ASSERT_FALSE(client->should_stop);

    ws_client_stop(client);
    TEST_ASSERT_TRUE(client->should_stop);

    ws_client_destroy(client);
}

/* Test ws_client_stop with NULL */
void test_ws_client_stop_null(void) {
    ws_client_stop(NULL);
    TEST_PASS();
}

/* Test ws_client_disconnect: disconnect when not connected */
void test_ws_client_disconnect_not_connected(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    /* Disconnect when not connected should not crash */
    ws_client_disconnect(client);
    TEST_ASSERT_FALSE(client->connected);

    ws_client_destroy(client);
}

/* ====== ws_client_send tests ====== */

/* Test ws_client_send_text returns error when not connected */
void test_ws_client_send_text_not_connected(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    const char *msg = "hello";
    int ret = ws_client_send_text(client, msg, strlen(msg));
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ws_client_destroy(client);
}

/* Test ws_client_send_text with NULL arguments */
void test_ws_client_send_text_null_args(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL_INT(-1, ws_client_send_text(NULL, "test", 4));
    TEST_ASSERT_EQUAL_INT(-1, ws_client_send_text(client, NULL, 4));

    ws_client_destroy(client);
}

/* Test ws_client_send_ping returns error when not connected */
void test_ws_client_send_ping_not_connected(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    int ret = ws_client_send_ping(client);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ws_client_destroy(client);
}

/* ====== WebSocket handshake tests ====== */

/*
 * Test Sec-WebSocket-Accept computation
 * Uses the example from RFC 6455 Section 4.1:
 *   Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==
 *   Expected Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=
 */
void test_ws_handshake_accept_key_rfc6455(void) {
    const char *key = "dGhlIHNhbXBsZSBub25jZQ==";
    char accept_key[64];

    test_compute_accept_key(key, accept_key);

    /* Expected value per RFC 6455 */
    TEST_ASSERT_EQUAL_STRING("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", accept_key);
}

/* Test Sec-WebSocket-Accept computation with a different Key */
void test_ws_handshake_accept_key_custom(void) {
    const char *key = "YWJjZGVmZ2hpamtsbW5vcA==";
    char accept_key[64];

    test_compute_accept_key(key, accept_key);

    /* Verify output is non-empty and has reasonable length (Base64-encoded SHA1 = 28 chars) */
    TEST_ASSERT_TRUE(strlen(accept_key) == 28);
}

/* ====== WebSocket frame mask tests ====== */

/* Test WebSocket client mask application: verify mask reversibility */
void test_ws_frame_mask_reversible(void) {
    unsigned char data[] = "Hello, WebSocket!";
    size_t data_len = strlen((char *)data);
    unsigned char original[32];
    memcpy(original, data, data_len);

    unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};

    /* Apply mask */
    test_apply_mask(data, data_len, mask);

    /* Masked data should differ from original data */
    TEST_ASSERT_NOT_EQUAL(0, memcmp(data, original, data_len));

    /* Applying the same mask again should restore original data */
    test_apply_mask(data, data_len, mask);
    TEST_ASSERT_EQUAL(0, memcmp(data, original, data_len));
}

/* Test WebSocket frame mask: empty data */
void test_ws_frame_mask_empty(void) {
    unsigned char data[] = "";
    unsigned char mask[4] = {0xFF, 0xFF, 0xFF, 0xFF};

    /* Masking empty data should not crash */
    test_apply_mask(data, 0, mask);
    TEST_PASS();
}

/* Test WebSocket frame mask: data length not a multiple of 4 */
void test_ws_frame_mask_non_aligned(void) {
    unsigned char data[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    size_t data_len = 7;
    unsigned char original[7];
    memcpy(original, data, data_len);

    unsigned char mask[4] = {0xAA, 0xBB, 0xCC, 0xDD};

    /* Apply mask */
    test_apply_mask(data, data_len, mask);

    /* Verify each byte is correctly masked */
    for (size_t i = 0; i < data_len; i++) {
        TEST_ASSERT_EQUAL_UINT8(original[i] ^ mask[i % 4], data[i]);
    }
}

/* ====== ws_message_t JSON message parsing tests ====== */

/*
 * Test WebSocket message JSON parsing: construct a message with the message field
 * This test reproduces the JSON parsing logic in ws_recv_thread
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

/* Test WebSocket message JSON parsing: terminal_id field (compatible with request_id) */
void test_ws_message_parse_terminal_id(void) {
    /* Test terminal_id field */
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

    /* Test request_id field (compatibility) */
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

/* Test WebSocket message JSON parsing: exec_command field (compatible with command) */
void test_ws_message_parse_exec_command(void) {
    /* Test exec_command field */
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

    /* Test command field (compatibility) */
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

/* Test WebSocket message JSON parsing: exec_task_id field (compatible with task_id) */
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

/* Test WebSocket message JSON parsing: ping-related fields */
void test_ws_message_parse_ping_fields(void) {
    const char *json_str =
        "{\"ping_type\": \"icmp\", \"ping_target\": \"8.8.8.8\", \"ping_task_id\": 42}";
    cJSON *root = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(root);

    ws_message_t msg;
    memset(&msg, 0, sizeof(msg));

    cJSON *item;

    /* Parse ping_type */
    if ((item = cJSON_GetObjectItem(root, "ping_type")) && cJSON_IsString(item)) {
        strncpy(msg.ping_type, item->valuestring, sizeof(msg.ping_type) - 1);
    }

    /* Parse ping_target */
    if ((item = cJSON_GetObjectItem(root, "ping_target")) && cJSON_IsString(item)) {
        strncpy(msg.ping_target, item->valuestring, sizeof(msg.ping_target) - 1);
    }

    /* Parse ping_task_id */
    if ((item = cJSON_GetObjectItem(root, "ping_task_id")) && cJSON_IsNumber(item)) {
        msg.ping_task_id = (uint32_t)item->valuedouble;
    }

    TEST_ASSERT_EQUAL_STRING("icmp", msg.ping_type);
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", msg.ping_target);
    TEST_ASSERT_EQUAL_UINT32(42, msg.ping_task_id);

    cJSON_Delete(root);
}

/* Test WebSocket message JSON parsing: full message with all fields */
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

/* Test WebSocket message JSON parsing: invalid JSON */
void test_ws_message_parse_invalid_json(void) {
    const char *json_str = "{ invalid json }";
    cJSON *root = cJSON_Parse(json_str);
    TEST_ASSERT_NULL(root);
}

/* Test ws_message_t struct size */
void test_ws_message_struct_size(void) {
    TEST_ASSERT_TRUE(sizeof(ws_message_t) > 0);
    /* message[32] + terminal_id[64] + exec_command[1024] + exec_task_id[64] +
       ping_task_id(4) + ping_type[16] + ping_target[256] */
    TEST_ASSERT_TRUE(sizeof(ws_message_t) >= 32 + 64 + 1024 + 64 + 4 + 16 + 256);
}

/* ====== RFC 6455 §5.4 fragmented message accumulation logic tests ====== */

/* Test ws_fragment_accumulate: single-frame (unfragmented) text message returns directly */
void test_ws_fragment_unfragmented_text(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    char data[] = "{\"message\":\"hello\"}";
    size_t len = strlen(data);
    char *out = NULL;
    size_t out_len = 0;
    int out_opcode = 0;

    /* FIN=1, opcode=0x01 (text): unfragmented message returns immediately */
    int r = ws_fragment_accumulate(client, 0x01, 1, data, len, &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(1, r);
    TEST_ASSERT_EQUAL_PTR(data, out);
    TEST_ASSERT_EQUAL_size_t(len, out_len);
    TEST_ASSERT_EQUAL_INT(0x01, out_opcode);

    /* No fragment buffer should have been allocated for unfragmented messages */
    TEST_ASSERT_NULL(client->fragment_buf);
    TEST_ASSERT_EQUAL_size_t(0, client->fragment_len);

    ws_client_destroy(client);
}

/* Test ws_fragment_accumulate: single-frame (unfragmented) binary message returns directly */
void test_ws_fragment_unfragmented_binary(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    char data[] = {0x01, 0x02, 0x03, 0x04};
    size_t len = sizeof(data);
    char *out = NULL;
    size_t out_len = 0;
    int out_opcode = 0;

    /* FIN=1, opcode=0x02 (binary): unfragmented message returns immediately */
    int r = ws_fragment_accumulate(client, 0x02, 1, data, len, &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(1, r);
    TEST_ASSERT_EQUAL_PTR(data, out);
    TEST_ASSERT_EQUAL_size_t(len, out_len);
    TEST_ASSERT_EQUAL_INT(0x02, out_opcode);

    ws_client_destroy(client);
}

/* Test ws_fragment_accumulate: multi-fragment text message accumulation
 * Simulate the server sending 3 fragments: first (FIN=0, opcode=0x01),
 * middle (FIN=0, opcode=0x00), last (FIN=1, opcode=0x00)
 */
void test_ws_fragment_multi_text(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    char part1[] = "{\"message\":\"";
    char part2[] = "hello ";
    char part3[] = "world\"}";
    char expected[64];
    snprintf(expected, sizeof(expected), "%s%s%s", part1, part2, part3);

    char *out = NULL;
    size_t out_len = 0;
    int out_opcode = 0;

    /* First fragment: FIN=0, opcode=0x01 (text) */
    int r = ws_fragment_accumulate(client, 0x01, 0, part1, strlen(part1),
                                   &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_NOT_NULL(client->fragment_buf);
    TEST_ASSERT_EQUAL_size_t(strlen(part1), client->fragment_len);
    TEST_ASSERT_EQUAL_INT(0x01, client->fragment_opcode);

    /* Middle fragment: FIN=0, opcode=0x00 (continuation) */
    r = ws_fragment_accumulate(client, 0x00, 0, part2, strlen(part2),
                               &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_size_t(strlen(part1) + strlen(part2), client->fragment_len);

    /* Final fragment: FIN=1, opcode=0x00 (continuation) */
    r = ws_fragment_accumulate(client, 0x00, 1, part3, strlen(part3),
                               &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(1, r);
    TEST_ASSERT_EQUAL_PTR(client->fragment_buf, out);
    TEST_ASSERT_EQUAL_size_t(strlen(expected), out_len);
    TEST_ASSERT_EQUAL_INT(0x01, out_opcode);
    /* Verify accumulated content matches the complete message */
    TEST_ASSERT_EQUAL_MEMORY(expected, out, out_len);

    /* After completion, fragment_len should be reset to 0 (buffer reused) */
    TEST_ASSERT_EQUAL_size_t(0, client->fragment_len);

    ws_client_destroy(client);
}

/* Test ws_fragment_accumulate: multi-fragment binary message accumulation */
void test_ws_fragment_multi_binary(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    unsigned char part1[] = {0x00, 0x01, 0x02};
    unsigned char part2[] = {0x03, 0x04};
    unsigned char part3[] = {0x05};
    unsigned char expected[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};

    char *out = NULL;
    size_t out_len = 0;
    int out_opcode = 0;

    /* First fragment: FIN=0, opcode=0x02 (binary) */
    int r = ws_fragment_accumulate(client, 0x02, 0, (char *)part1, sizeof(part1),
                                   &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_INT(0x02, client->fragment_opcode);

    /* Middle fragment */
    r = ws_fragment_accumulate(client, 0x00, 0, (char *)part2, sizeof(part2),
                               &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(0, r);

    /* Final fragment */
    r = ws_fragment_accumulate(client, 0x00, 1, (char *)part3, sizeof(part3),
                               &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(1, r);
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), out_len);
    TEST_ASSERT_EQUAL_INT(0x02, out_opcode);
    TEST_ASSERT_EQUAL_MEMORY(expected, out, out_len);

    ws_client_destroy(client);
}

/* Test ws_fragment_accumulate: returns error when accumulation exceeds WS_FRAGMENT_MAX_SIZE */
void test_ws_fragment_oversize(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    /* Use a 64KB chunk (matches WS_MAX_MESSAGE_SIZE, the largest single frame
     * the recv thread can deliver). Reuse the same buffer for every fragment. */
    size_t chunk = 65536;
    char *data = (char *)malloc(chunk);
    TEST_ASSERT_NOT_NULL(data);
    memset(data, 'A', chunk);

    char *out = NULL;
    size_t out_len = 0;
    int out_opcode = 0;

    /* Start a new fragment with FIN=0, opcode=0x01 */
    int r = ws_fragment_accumulate(client, 0x01, 0, data, chunk, &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(0, r);

    /* Keep appending continuation frames until we exceed WS_FRAGMENT_MAX_SIZE.
     * WS_FRAGMENT_MAX_SIZE (1MB) / 64KB = 16 chunks; the 17th chunk pushes the
     * total to 17*64KB = 1.0625 MB > 1 MB, which must be rejected. */
    int saw_error = 0;
    for (int i = 0; i < 20; i++) {
        r = ws_fragment_accumulate(client, 0x00, 0, data, chunk,
                                   &out, &out_len, &out_opcode);
        if (r < 0) {
            saw_error = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE(saw_error);

    free(data);
    ws_client_destroy(client);
}

/* Test ws_fragment_accumulate: returns error when continuation frame received without a start fragment */
void test_ws_fragment_continuation_without_start(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    char data[] = "orphan";
    char *out = NULL;
    size_t out_len = 0;
    int out_opcode = 0;

    /* Continuation frame without a preceding first fragment: protocol error */
    int r = ws_fragment_accumulate(client, 0x00, 1, data, strlen(data),
                                   &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(-1, r);

    ws_client_destroy(client);
}

/* Test ws_fragment_accumulate: returns error when a new non-continuation frame arrives during fragmentation */
void test_ws_fragment_new_opcode_during_fragment(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    char part1[] = "start ";
    char part2[] = "interrupt";
    char *out = NULL;
    size_t out_len = 0;
    int out_opcode = 0;

    /* Start a fragment */
    int r = ws_fragment_accumulate(client, 0x01, 0, part1, strlen(part1),
                                   &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(0, r);

    /* Receive a new text frame (FIN=1, opcode=0x01) while fragment in progress:
     * RFC 6455 §5.4 forbids this; the accumulator must report a protocol error. */
    r = ws_fragment_accumulate(client, 0x01, 1, part2, strlen(part2),
                               &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(-1, r);

    ws_client_destroy(client);
}

/* Test ws_fragment_accumulate: a new fragmented message can start immediately after completing one (buffer reuse) */
void test_ws_fragment_reset_after_complete(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    char *out = NULL;
    size_t out_len = 0;
    int out_opcode = 0;

    /* First message: 2-fragment text */
    char part1[] = "hello ";
    char part2[] = "world";
    int r = ws_fragment_accumulate(client, 0x01, 0, part1, strlen(part1),
                                   &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(0, r);
    r = ws_fragment_accumulate(client, 0x00, 1, part2, strlen(part2),
                               &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(1, r);
    TEST_ASSERT_EQUAL_size_t(11, out_len);
    TEST_ASSERT_EQUAL_INT(0x01, out_opcode);

    /* Second message: unfragmented text after a completed fragmented message */
    char msg2[] = "{\"ok\":true}";
    r = ws_fragment_accumulate(client, 0x01, 1, msg2, strlen(msg2),
                               &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(1, r);
    TEST_ASSERT_EQUAL_PTR(msg2, out);
    TEST_ASSERT_EQUAL_size_t(strlen(msg2), out_len);

    /* Third message: another fragmented text, verifying buffer reuse */
    char part3[] = "second ";
    char part4[] = "message";
    r = ws_fragment_accumulate(client, 0x01, 0, part3, strlen(part3),
                               &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(0, r);
    r = ws_fragment_accumulate(client, 0x00, 1, part4, strlen(part4),
                               &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(1, r);
    TEST_ASSERT_EQUAL_size_t(strlen(part3) + strlen(part4), out_len);
    TEST_ASSERT_EQUAL_INT(0x01, out_opcode);

    char expected[] = "second message";
    TEST_ASSERT_EQUAL_MEMORY(expected, out, out_len);

    ws_client_destroy(client);
}

/* Test ws_fragment_accumulate: empty fragments (zero-length payload) accumulate correctly */
void test_ws_fragment_empty_payloads(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    char *out = NULL;
    size_t out_len = 0;
    int out_opcode = 0;
    char empty[] = "";
    char tail[] = "X";

    /* First fragment with empty payload */
    int r = ws_fragment_accumulate(client, 0x01, 0, empty, 0, &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_size_t(0, client->fragment_len);

    /* Middle fragment with empty payload */
    r = ws_fragment_accumulate(client, 0x00, 0, empty, 0, &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(0, r);
    TEST_ASSERT_EQUAL_size_t(0, client->fragment_len);

    /* Final fragment with one byte */
    r = ws_fragment_accumulate(client, 0x00, 1, tail, 1, &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(1, r);
    TEST_ASSERT_EQUAL_size_t(1, out_len);
    TEST_ASSERT_EQUAL_INT(0x01, out_opcode);
    TEST_ASSERT_EQUAL_UINT8('X', (unsigned char)out[0]);

    ws_client_destroy(client);
}

/* Test ws_fragment_accumulate: NULL arguments return error */
void test_ws_fragment_null_args(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    char data[] = "x";
    char *out = NULL;
    size_t out_len = 0;
    int out_opcode = 0;

    TEST_ASSERT_EQUAL_INT(-1, ws_fragment_accumulate(NULL, 0x01, 1, data, 1, &out, &out_len, &out_opcode));
    TEST_ASSERT_EQUAL_INT(-1, ws_fragment_accumulate(client, 0x01, 1, NULL, 1, &out, &out_len, &out_opcode));
    TEST_ASSERT_EQUAL_INT(-1, ws_fragment_accumulate(client, 0x01, 1, data, 1, NULL, &out_len, &out_opcode));
    TEST_ASSERT_EQUAL_INT(-1, ws_fragment_accumulate(client, 0x01, 1, data, 1, &out, NULL, &out_opcode));
    TEST_ASSERT_EQUAL_INT(-1, ws_fragment_accumulate(client, 0x01, 1, data, 1, &out, &out_len, NULL));

    ws_client_destroy(client);
}

/* Test ws_fragment_accumulate: single-fragment binary message (first fragment is also last, FIN=0+1 impossible).
 * This test verifies that FIN=0 + opcode=0x00 (continuation but fin=0) returns an error without a prior fragment */
void test_ws_fragment_invalid_continuation_fin0(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    char data[] = "x";
    char *out = NULL;
    size_t out_len = 0;
    int out_opcode = 0;

    /* FIN=0, opcode=0x00 without prior start: protocol error */
    int r = ws_fragment_accumulate(client, 0x00, 0, data, 1, &out, &out_len, &out_opcode);
    TEST_ASSERT_EQUAL_INT(-1, r);

    ws_client_destroy(client);
}

/* ====== v2 JSON-RPC event handling tests (ws_handle_v2_event) ====== */

/* Test handler: records the last received message and a call counter so tests
 * can verify that ws_handle_v2_event dispatched to the handler with the right
 * fields. The handler intentionally does not depend on global state beyond
 * these static variables; each test resets them before use. */
static ws_message_t g_v2_last_msg;
static int g_v2_handler_calls = 0;

static void test_v2_message_handler(ws_client_t *client, const ws_message_t *msg) {
    (void)client;
    g_v2_last_msg = *msg;
    g_v2_handler_calls++;
}

static void reset_v2_handler_state(void) {
    memset(&g_v2_last_msg, 0, sizeof(g_v2_last_msg));
    g_v2_handler_calls = 0;
}

/* Test ws_handle_v2_event with NULL arguments returns -1. */
void test_ws_handle_v2_event_null_args(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL_INT(-1, ws_handle_v2_event(NULL, "{}"));
    TEST_ASSERT_EQUAL_INT(-1, ws_handle_v2_event(client, NULL));

    ws_client_destroy(client);
}

/* Test ws_handle_v2_event with invalid JSON returns -1. */
void test_ws_handle_v2_event_invalid_json(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);

    TEST_ASSERT_EQUAL_INT(-1, ws_handle_v2_event(client, "not json"));

    ws_client_destroy(client);
}

/* Test agent.exec dispatch: handler receives msg.message="exec" with
 * exec_command and exec_task_id extracted from params. */
void test_ws_handle_v2_event_dispatch_exec(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    ws_client_set_handler(client, test_v2_message_handler);
    reset_v2_handler_state();

    const char *event_json =
        "{\"jsonrpc\":\"2.0\",\"id\":\"42\","
        "\"method\":\"agent.exec\","
        "\"params\":{\"task_id\":\"task-001\",\"command\":\"uname -a\"}}";

    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));

    /* Handler must have been called once with exec fields populated */
    TEST_ASSERT_EQUAL_INT(1, g_v2_handler_calls);
    TEST_ASSERT_EQUAL_STRING("exec", g_v2_last_msg.message);
    TEST_ASSERT_EQUAL_STRING("task-001", g_v2_last_msg.exec_task_id);
    TEST_ASSERT_EQUAL_STRING("uname -a", g_v2_last_msg.exec_command);

    /* ACK must be accumulated (id="42" -> ack_id=42) */
    TEST_ASSERT_EQUAL_INT(1, client->v2_state.ack_count);
    TEST_ASSERT_EQUAL_INT(42, client->v2_state.ack_ids[0]);

    ws_client_destroy(client);
}

/* Test agent.ping dispatch: handler receives msg.message="ping" with ping
 * fields extracted from params. */
void test_ws_handle_v2_event_dispatch_ping(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    ws_client_set_handler(client, test_v2_message_handler);
    reset_v2_handler_state();

    const char *event_json =
        "{\"jsonrpc\":\"2.0\",\"id\":\"100\","
        "\"method\":\"agent.ping\","
        "\"params\":{\"ping_task_id\":99,\"ping_type\":\"icmp\","
        "\"ping_target\":\"8.8.8.8\"}}";

    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));

    TEST_ASSERT_EQUAL_INT(1, g_v2_handler_calls);
    TEST_ASSERT_EQUAL_STRING("ping", g_v2_last_msg.message);
    TEST_ASSERT_EQUAL_UINT32(99, g_v2_last_msg.ping_task_id);
    TEST_ASSERT_EQUAL_STRING("icmp", g_v2_last_msg.ping_type);
    TEST_ASSERT_EQUAL_STRING("8.8.8.8", g_v2_last_msg.ping_target);

    /* ACK accumulated with id=100 */
    TEST_ASSERT_EQUAL_INT(1, client->v2_state.ack_count);
    TEST_ASSERT_EQUAL_INT(100, client->v2_state.ack_ids[0]);

    ws_client_destroy(client);
}

/* Test agent.terminal.request dispatch: handler receives msg with terminal_id
 * populated from params.request_id. */
void test_ws_handle_v2_event_dispatch_terminal(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    ws_client_set_handler(client, test_v2_message_handler);
    reset_v2_handler_state();

    const char *event_json =
        "{\"jsonrpc\":\"2.0\",\"id\":\"7\","
        "\"method\":\"agent.terminal.request\","
        "\"params\":{\"request_id\":\"term-abc-123\"}}";

    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));

    TEST_ASSERT_EQUAL_INT(1, g_v2_handler_calls);
    TEST_ASSERT_EQUAL_STRING("term-abc-123", g_v2_last_msg.terminal_id);

    /* ACK accumulated with id=7 */
    TEST_ASSERT_EQUAL_INT(1, client->v2_state.ack_count);
    TEST_ASSERT_EQUAL_INT(7, client->v2_state.ack_ids[0]);

    ws_client_destroy(client);
}

/* Test agent.message and agent.event: handler is NOT called (informational
 * only) but ACK is still accumulated. */
void test_ws_handle_v2_event_dispatch_message_event(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    ws_client_set_handler(client, test_v2_message_handler);
    reset_v2_handler_state();

    /* agent.message */
    const char *msg_json =
        "{\"jsonrpc\":\"2.0\",\"id\":\"55\","
        "\"method\":\"agent.message\","
        "\"params\":{\"text\":\"hello\"}}";
    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, msg_json));
    TEST_ASSERT_EQUAL_INT(0, g_v2_handler_calls);
    TEST_ASSERT_EQUAL_INT(1, client->v2_state.ack_count);
    TEST_ASSERT_EQUAL_INT(55, client->v2_state.ack_ids[0]);

    /* agent.event */
    const char *evt_json =
        "{\"jsonrpc\":\"2.0\",\"id\":\"56\","
        "\"method\":\"agent.event\","
        "\"params\":{\"type\":\"custom\"}}";
    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, evt_json));
    TEST_ASSERT_EQUAL_INT(0, g_v2_handler_calls);
    TEST_ASSERT_EQUAL_INT(2, client->v2_state.ack_count);
    TEST_ASSERT_EQUAL_INT(56, client->v2_state.ack_ids[1]);

    ws_client_destroy(client);
}

/* Test unknown method: handler is NOT called and no ACK is accumulated. */
void test_ws_handle_v2_event_unknown_method(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    ws_client_set_handler(client, test_v2_message_handler);
    reset_v2_handler_state();

    const char *event_json =
        "{\"jsonrpc\":\"2.0\",\"id\":\"99\","
        "\"method\":\"agent.unknown\","
        "\"params\":{}}";

    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));
    TEST_ASSERT_EQUAL_INT(0, g_v2_handler_calls);
    /* Unknown method: not processed, no ACK */
    TEST_ASSERT_EQUAL_INT(0, client->v2_state.ack_count);

    ws_client_destroy(client);
}

/* Test event deduplication: the same event ID is processed only once.
 * The second call must skip handler dispatch but still accumulate ACK. */
void test_ws_handle_v2_event_dedup(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    ws_client_set_handler(client, test_v2_message_handler);
    reset_v2_handler_state();

    const char *event_json =
        "{\"jsonrpc\":\"2.0\",\"id\":\"200\","
        "\"method\":\"agent.exec\","
        "\"params\":{\"task_id\":\"t1\",\"command\":\"echo hi\"}}";

    /* First call: dispatched + ACKed */
    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));
    TEST_ASSERT_EQUAL_INT(1, g_v2_handler_calls);
    TEST_ASSERT_EQUAL_INT(1, client->v2_state.ack_count);
    TEST_ASSERT_EQUAL_INT(200, client->v2_state.ack_ids[0]);

    /* Second call with the same id: dedup skips dispatch but still ACKs */
    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));
    TEST_ASSERT_EQUAL_INT(1, g_v2_handler_calls);  /* still 1, not 2 */
    TEST_ASSERT_EQUAL_INT(2, client->v2_state.ack_count);
    TEST_ASSERT_EQUAL_INT(200, client->v2_state.ack_ids[1]);

    ws_client_destroy(client);
}

/* Test ACK accumulation with numeric event ID (JSON number, not string).
 * jsonrpc_parse_event only captures string IDs, so ws_handle_v2_event falls
 * back to re-parsing the JSON for numeric IDs. */
void test_ws_handle_v2_event_numeric_id_ack(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    ws_client_set_handler(client, test_v2_message_handler);
    reset_v2_handler_state();

    /* id is a JSON number (333), not a string */
    const char *event_json =
        "{\"jsonrpc\":\"2.0\",\"id\":333,"
        "\"method\":\"agent.exec\","
        "\"params\":{\"task_id\":\"t2\",\"command\":\"echo num\"}}";

    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));
    TEST_ASSERT_EQUAL_INT(1, g_v2_handler_calls);
    TEST_ASSERT_EQUAL_STRING("t2", g_v2_last_msg.exec_task_id);

    /* ACK accumulated with numeric id=333 */
    TEST_ASSERT_EQUAL_INT(1, client->v2_state.ack_count);
    TEST_ASSERT_EQUAL_INT(333, client->v2_state.ack_ids[0]);

    /* Dedup also works for numeric IDs (string form "333") */
    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));
    TEST_ASSERT_EQUAL_INT(1, g_v2_handler_calls);  /* deduped */
    TEST_ASSERT_EQUAL_INT(2, client->v2_state.ack_count);

    ws_client_destroy(client);
}

/* Test that non-numeric string IDs are deduplicated but not ACKed (the C v2
 * interface uses int ACK IDs, so non-numeric strings cannot be ACKed). */
void test_ws_handle_v2_event_non_numeric_id_no_ack(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    ws_client_set_handler(client, test_v2_message_handler);
    reset_v2_handler_state();

    const char *event_json =
        "{\"jsonrpc\":\"2.0\",\"id\":\"uuid-abc-123\","
        "\"method\":\"agent.message\","
        "\"params\":{}}";

    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));
    /* agent.message: processed=true (logged), but ID is non-numeric -> no ACK */
    TEST_ASSERT_EQUAL_INT(0, client->v2_state.ack_count);

    /* Dedup still works for non-numeric IDs */
    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));
    TEST_ASSERT_EQUAL_INT(0, client->v2_state.ack_count);

    ws_client_destroy(client);
}

/* Test event without an ID: always dispatched, never deduped, never ACKed. */
void test_ws_handle_v2_event_no_id(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    ws_client_set_handler(client, test_v2_message_handler);
    reset_v2_handler_state();

    const char *event_json =
        "{\"jsonrpc\":\"2.0\","
        "\"method\":\"agent.exec\","
        "\"params\":{\"task_id\":\"t3\",\"command\":\"echo noid\"}}";

    /* First call: dispatched, no ACK (no id) */
    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));
    TEST_ASSERT_EQUAL_INT(1, g_v2_handler_calls);
    TEST_ASSERT_EQUAL_INT(0, client->v2_state.ack_count);

    /* Second call: also dispatched (no dedup without id), still no ACK */
    TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, event_json));
    TEST_ASSERT_EQUAL_INT(2, g_v2_handler_calls);
    TEST_ASSERT_EQUAL_INT(0, client->v2_state.ack_count);

    ws_client_destroy(client);
}

/* Test ACK snapshot + clear integration with ws_handle_v2_event:
 * process several events, snapshot the ACK IDs, then clear and verify. */
void test_ws_handle_v2_event_ack_snapshot_clear(void) {
    ws_client_t *client = ws_client_create(NULL);
    TEST_ASSERT_NOT_NULL(client);
    ws_client_set_handler(client, test_v2_message_handler);
    reset_v2_handler_state();

    /* Process three events with IDs 10, 20, 30 */
    const char *events[] = {
        "{\"jsonrpc\":\"2.0\",\"id\":\"10\",\"method\":\"agent.message\",\"params\":{}}",
        "{\"jsonrpc\":\"2.0\",\"id\":\"20\",\"method\":\"agent.message\",\"params\":{}}",
        "{\"jsonrpc\":\"2.0\",\"id\":\"30\",\"method\":\"agent.message\",\"params\":{}}"
    };

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_INT(0, ws_handle_v2_event(client, events[i]));
    }
    TEST_ASSERT_EQUAL_INT(3, client->v2_state.ack_count);

    /* Snapshot the ACK IDs into a local buffer */
    int snap[16];
    int snap_count = 0;
    TEST_ASSERT_EQUAL_INT(0, v2_snapshot_ack_ids(&client->v2_state,
                                                  snap, 16, &snap_count));
    TEST_ASSERT_EQUAL_INT(3, snap_count);
    TEST_ASSERT_EQUAL_INT(10, snap[0]);
    TEST_ASSERT_EQUAL_INT(20, snap[1]);
    TEST_ASSERT_EQUAL_INT(30, snap[2]);

    /* Internal state still holds all 3 ACKs */
    TEST_ASSERT_EQUAL_INT(3, client->v2_state.ack_count);

    /* Clear all ACKs */
    v2_clear_acks(&client->v2_state);
    TEST_ASSERT_EQUAL_INT(0, client->v2_state.ack_count);

    /* Snapshot after clear returns 0 items */
    snap_count = -1;
    TEST_ASSERT_EQUAL_INT(0, v2_snapshot_ack_ids(&client->v2_state,
                                                  snap, 16, &snap_count));
    TEST_ASSERT_EQUAL_INT(0, snap_count);

    ws_client_destroy(client);
}

int main(void) {
    UNITY_BEGIN();

    /* Client create/destroy tests */
    RUN_TEST(test_ws_client_create_with_config);
    RUN_TEST(test_ws_client_create_null_config);
    RUN_TEST(test_ws_client_destroy_null);

    /* Setter tests */
    RUN_TEST(test_ws_client_set_handler);
    RUN_TEST(test_ws_client_set_raw_handler);
    RUN_TEST(test_ws_client_set_user_data);
    RUN_TEST(test_ws_client_set_handler_null_client);

    /* Stop/disconnect tests */
    RUN_TEST(test_ws_client_stop);
    RUN_TEST(test_ws_client_stop_null);
    RUN_TEST(test_ws_client_disconnect_not_connected);

    /* Send tests */
    RUN_TEST(test_ws_client_send_text_not_connected);
    RUN_TEST(test_ws_client_send_text_null_args);
    RUN_TEST(test_ws_client_send_ping_not_connected);

    /* Handshake tests */
    RUN_TEST(test_ws_handshake_accept_key_rfc6455);
    RUN_TEST(test_ws_handshake_accept_key_custom);

    /* Mask tests */
    RUN_TEST(test_ws_frame_mask_reversible);
    RUN_TEST(test_ws_frame_mask_empty);
    RUN_TEST(test_ws_frame_mask_non_aligned);

    /* JSON message parsing tests */
    RUN_TEST(test_ws_message_parse_message_field);
    RUN_TEST(test_ws_message_parse_terminal_id);
    RUN_TEST(test_ws_message_parse_exec_command);
    RUN_TEST(test_ws_message_parse_exec_task_id);
    RUN_TEST(test_ws_message_parse_ping_fields);
    RUN_TEST(test_ws_message_parse_full_message);
    RUN_TEST(test_ws_message_parse_invalid_json);
    RUN_TEST(test_ws_message_struct_size);

    /* RFC 6455 §5.4 fragmented message accumulation tests */
    RUN_TEST(test_ws_fragment_unfragmented_text);
    RUN_TEST(test_ws_fragment_unfragmented_binary);
    RUN_TEST(test_ws_fragment_multi_text);
    RUN_TEST(test_ws_fragment_multi_binary);
    RUN_TEST(test_ws_fragment_oversize);
    RUN_TEST(test_ws_fragment_continuation_without_start);
    RUN_TEST(test_ws_fragment_new_opcode_during_fragment);
    RUN_TEST(test_ws_fragment_reset_after_complete);
    RUN_TEST(test_ws_fragment_empty_payloads);
    RUN_TEST(test_ws_fragment_null_args);
    RUN_TEST(test_ws_fragment_invalid_continuation_fin0);

    /* v2 JSON-RPC event handling tests */
    RUN_TEST(test_ws_handle_v2_event_null_args);
    RUN_TEST(test_ws_handle_v2_event_invalid_json);
    RUN_TEST(test_ws_handle_v2_event_dispatch_exec);
    RUN_TEST(test_ws_handle_v2_event_dispatch_ping);
    RUN_TEST(test_ws_handle_v2_event_dispatch_terminal);
    RUN_TEST(test_ws_handle_v2_event_dispatch_message_event);
    RUN_TEST(test_ws_handle_v2_event_unknown_method);
    RUN_TEST(test_ws_handle_v2_event_dedup);
    RUN_TEST(test_ws_handle_v2_event_numeric_id_ack);
    RUN_TEST(test_ws_handle_v2_event_non_numeric_id_no_ack);
    RUN_TEST(test_ws_handle_v2_event_no_id);
    RUN_TEST(test_ws_handle_v2_event_ack_snapshot_clear);

    return UNITY_END();
}
