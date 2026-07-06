/*
 * websocket_internal.h - Internal helpers of websocket.c exposed for testing.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 *
 * The functions declared here are implementation details of websocket.c and
 * must not be called by application code. They are exposed solely so that
 * unit tests in tests/test_websocket.c can verify the real handshake, mask
 * and JSON parsing logic instead of re-implementing it.
 */

#ifndef KOMARI_AGENT_C_WEBSOCKET_INTERNAL_H
#define KOMARI_AGENT_C_WEBSOCKET_INTERNAL_H

#include <stddef.h>

#include "cJSON.h"
#include "websocket.h"

/* Base64-encode `len` bytes from `data` into `out`. The output buffer must
 * have room for at least ((len + 2) / 3) * 4 + 1 bytes (including NUL).
 * The output is always NUL-terminated. */
void ws_base64_encode(const unsigned char *data, size_t len, char *out);

/* Compute the Sec-WebSocket-Accept value from a client key per RFC 6455
 * §4.2.2: Base64(SHA1(key || "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")).
 *
 * @param key             NUL-terminated client key.
 * @param accept_key      Output buffer for the 28-char Base64 result + NUL.
 * @param accept_key_size Size of accept_key; must be >= 29.
 * @return 0 on success, -1 if the output buffer is too small or snprintf
 *         truncation occurs. */
int ws_compute_accept_key(const char *key, char *accept_key, size_t accept_key_size);

/* Apply a 4-byte WebSocket mask to `data` in place per RFC 6455 §5.3:
 * data[i] ^= mask[i % 4]. Used by both the send path (client frames must be
 * masked) and the receive path (server frames may be masked). Exposed so
 * tests can verify mask reversibility without duplicating the loop. */
void ws_apply_mask(unsigned char *data, size_t len, const unsigned char mask[4]);

/* Extract v1 message fields from a parsed cJSON tree into a ws_message_t.
 *
 * Implements the same field-extraction logic the recv thread uses for v1
 * messages: reads "message", "terminal_id" (fallback "request_id"),
 * "exec_command" (fallback "command"), "exec_task_id" (fallback "task_id"),
 * "ping_type", "ping_target", "ping_task_id". All string fields are copied
 * with strncpy(.., sizeof(field) - 1) and explicitly NUL-terminated so that
 * overlong inputs are truncated rather than overflowed.
 *
 * The caller is responsible for zero-initializing `msg` before the call
 * (e.g., `ws_message_t msg = {0};`) so that absent fields remain empty
 * strings rather than uninitialized memory.
 *
 * @param root Parsed cJSON tree. May be NULL; the function returns -1 and
 *             leaves `msg` untouched.
 * @param msg  Output message struct. Must not be NULL.
 * @return 0 on success, -1 if root or msg is NULL. */
int ws_message_parse_from_json(const cJSON *root, ws_message_t *msg);

#endif /* KOMARI_AGENT_C_WEBSOCKET_INTERNAL_H */
