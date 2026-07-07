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
#include <pthread.h>
#include <openssl/ssl.h>

#include "cJSON.h"
#include "websocket.h"

/* Full definition of the ws_client handle declared as opaque in websocket.h.
 * Exposed here so that websocket.c and the white-box unit tests in
 * tests/test_websocket.c can read or assert on internal state. Application
 * code must include only websocket.h and treat ws_client_t as opaque. */
struct ws_client {
    int fd;
    bool connected;
    bool should_stop;
    bool use_tls;
    SSL *ssl;
    SSL_CTX *ssl_ctx;
    pthread_t recv_thread;
    pthread_mutex_t send_mutex;
    pthread_mutex_t state_mutex;
    ws_client_config_t config;
    ws_message_handler_t handler;
    ws_raw_handler_t raw_handler;
    void *user_data;
    v2_state_t v2_state;   /* v2 protocol runtime state, used for protocol fallback mechanism */
    int protocol_version;  /* Currently active protocol version (protocol_version_t) */
    /* Fragmented message accumulation state (RFC 6455 §5.4).
     * fragment_buf accumulates continuation frame payloads until a final
     * frame (FIN=1) is received. Only the recv thread reads/writes these
     * fields, so no extra locking is required. */
    char *fragment_buf;
    size_t fragment_len;
    size_t fragment_capacity;
    int fragment_opcode;

    /* Bytes read past the HTTP handshake response. The server may start
     * sending WebSocket frames immediately after the 101 Switching Protocols
     * response; ws_handshake reads them into the same buffer as the headers
     * and stores any overflow here so the recv thread can consume them via
     * read_full before issuing recv()/SSL_read(). pending_off tracks the
     * next byte to consume; pending_len is the total bytes stored. Only the
     * recv thread touches these after ws_handshake. */
    unsigned char pending_buf[WS_PENDING_BUF_SIZE];
    size_t pending_len;
    size_t pending_off;
};

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

/* ====== Fragment accumulation (RFC 6455 §5.4) ======
 * The following helper is an internal function exposed for unit testing.
 * Application code should not call it directly; it is invoked by the
 * recv thread to assemble fragmented WebSocket messages. */

/**
 * Accumulate a WebSocket fragment into the client's fragment buffer.
 *
 * Handles RFC 6455 §5.4 message fragmentation. For unfragmented messages
 * (FIN=1 with a non-continuation opcode), returns the input buffer directly.
 * For fragmented messages, accumulates payload into the client's fragment_buf
 * until the final fragment is received.
 *
 * @param client     Pointer to the client.
 * @param opcode     Frame opcode (0x00 continuation, 0x01 text, 0x02 binary).
 * @param fin        FIN bit (1 = final frame, 0 = more fragments to follow).
 * @param data       Frame payload buffer (mutable; may be returned via `out`
 *                   for unfragmented messages).
 * @param len        Frame payload length.
 * @param out        On return value 1, points to the complete message
 *                   (either `data` for unfragmented messages or
 *                   `client->fragment_buf` for fragmented ones). The caller
 *                   may safely write a NUL terminator at `out[*out_len]`.
 * @param out_len    On return value 1, length of the complete message.
 * @param out_opcode On return value 1, opcode of the complete message
 *                   (0x01 text or 0x02 binary).
 * @return 0 = fragment accumulated, waiting for more data;
 *         1 = message complete (`out`/`out_len`/`out_opcode` set);
 *        -1 = error (oversize, allocation failure, or protocol error).
 */
int ws_fragment_accumulate(ws_client_t *client, int opcode, int fin,
                           char *data, size_t len,
                           char **out, size_t *out_len, int *out_opcode);

/* ====== v2 JSON-RPC event handling ======
 * The following helper is an internal function exposed for unit testing.
 * Application code should not call it directly; it is invoked by the
 * recv thread to dispatch v2 JSON-RPC events received from the server. */

/**
 * Handle a v2 JSON-RPC event message.
 *
 * Parses the JSON-RPC 2.0 event, deduplicates by event ID (using the
 * client's v2 state), dispatches to the registered message handler based
 * on the method field (agent.exec / agent.ping / agent.terminal.request /
 * agent.message / agent.event), and accumulates the event ID into the
 * pending ACK list for the next report cycle.
 *
 * Deduplication: if the event ID (string form) has been seen before, the
 * event is not dispatched to the handler but is still ACKed (so the server
 * stops retransmitting). Events without an ID are always dispatched.
 *
 * ACK accumulation: the event ID is converted to an int (via strtol for
 * string IDs, or directly for numeric IDs). If the ID is missing, empty,
 * or non-numeric, ACK accumulation is skipped (the C v2 interface uses
 * int ACK IDs, matching v2_add_ack_event).
 *
 * @param client Pointer to the WebSocket client.
 * @param root   Parsed cJSON tree of the JSON-RPC event. Ownership is
 *               transferred to this function, which frees it via cJSON_Delete
 *               before returning. The caller must not free or reuse root
 *               after this call. Passing NULL is invalid and returns -1.
 * @return 0 on success (event processed or duplicate skipped),
 *         -1 on parse failure or invalid arguments.
 */
int ws_handle_v2_event(ws_client_t *client, cJSON *root);

#endif /* KOMARI_AGENT_C_WEBSOCKET_INTERNAL_H */
