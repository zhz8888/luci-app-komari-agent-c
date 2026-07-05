/*
 * WebSocket client (ws/wss) with TLS support and v1/v2 protocol fallback.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_WEBSOCKET_H
#define KOMARI_AGENT_C_WEBSOCKET_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <openssl/ssl.h>

#include "cJSON.h"
#include "config.h"
#include "monitoring.h"
#include "v2.h"

#define WS_MAX_MESSAGE_SIZE (64 * 1024)
#define WS_PING_INTERVAL 30
#define WS_KEY_LEN 24

/* Maximum accumulated size for a fragmented WebSocket message (RFC 6455 §5.4).
 * When the total payload of a fragmented message exceeds this limit, the
 * connection is closed to prevent unbounded memory growth. */
#define WS_FRAGMENT_MAX_SIZE (1024 * 1024)

/* Size of the buffer used to hold bytes read past the HTTP handshake response
 * (MAJ-24). The server may send the first WebSocket frame immediately after
 * the 101 Switching Protocols response; those bytes are read into the same
 * buffer as the headers during ws_handshake and must be preserved for the
 * recv thread to consume via read_full. Matches the 2048-byte handshake
 * response buffer so no data is ever lost. */
#define WS_PENDING_BUF_SIZE 2048

typedef struct {
    const char *endpoint;
    const char *token;
    const char *extra_query;  /* Additional query parameters, e.g., "&id=xxx", appended after token */
    bool ignore_cert;
    int max_retries;
    int reconnect_interval;
    double report_interval;
} ws_client_config_t;

typedef struct {
    int type;
    char message[32];
    char terminal_id[64];
    char exec_command[1024];
    char exec_task_id[64];
    uint32_t ping_task_id;
    char ping_type[16];
    char ping_target[256];
} ws_message_t;

typedef struct ws_client ws_client_t;

typedef void (*ws_message_handler_t)(ws_client_t *client, const ws_message_t *msg);
/* Raw data callback: used for terminal and similar scenarios, passes frame data directly without JSON parsing */
typedef void (*ws_raw_handler_t)(ws_client_t *client, const char *data, size_t len);

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
    int protocol_version;  /* Currently used protocol version (1 or 2) */
    /* Fragmented message accumulation state (RFC 6455 §5.4).
     * fragment_buf accumulates continuation frame payloads until a final
     * frame (FIN=1) is received. Only the recv thread reads/writes these
     * fields, so no extra locking is required. */
    char *fragment_buf;
    size_t fragment_len;
    size_t fragment_capacity;
    int fragment_opcode;

    /* Bytes read past the HTTP handshake response (MAJ-24). The server may
     * start sending WebSocket frames immediately after the 101 Switching
     * Protocols response; ws_handshake reads them into the same buffer as
     * the headers and stores any overflow here so the recv thread can
     * consume them via read_full before issuing recv()/SSL_read().
     * pending_off tracks the next byte to consume; pending_len is the total
     * bytes stored. Only the recv thread touches these after ws_handshake. */
    unsigned char pending_buf[WS_PENDING_BUF_SIZE];
    size_t pending_len;
    size_t pending_off;
};

/**
 * Create a new WebSocket client from the given configuration.
 *
 * @param config Configuration for the client (endpoint, token, TLS options, etc.).
 *               May be NULL to use defaults.
 * @return Newly allocated client on success, NULL on failure. The caller must
 *         release it with ws_client_destroy.
 */
ws_client_t *ws_client_create(const ws_client_config_t *config);

/**
 * Destroy a WebSocket client and release all associated resources.
 *
 * @param client Pointer to the client to destroy. May be NULL.
 */
void ws_client_destroy(ws_client_t *client);

/**
 * Connect the client to the configured endpoint.
 *
 * @param client Pointer to the client.
 * @return 0 on success, -1 on failure.
 */
int ws_client_connect(ws_client_t *client);

/**
 * Disconnect the client and tear down the underlying socket/TLS state.
 *
 * @param client Pointer to the client. May be NULL.
 */
void ws_client_disconnect(ws_client_t *client);

/**
 * Send a text frame to the server.
 *
 * @param client Pointer to the client.
 * @param data   Payload data to send.
 * @param len    Length of the payload (in bytes).
 * @return 0 on success, -1 on failure.
 */
int ws_client_send_text(ws_client_t *client, const char *data, size_t len);

/**
 * Send a WebSocket ping frame to the server.
 *
 * @param client Pointer to the client.
 * @return 0 on success, -1 on failure.
 */
int ws_client_send_ping(ws_client_t *client);

/**
 * Set the JSON message handler invoked for incoming text frames parsed as JSON.
 *
 * @param client  Pointer to the client.
 * @param handler Callback function. May be NULL to clear.
 */
void ws_client_set_handler(ws_client_t *client, ws_message_handler_t handler);

/**
 * Set the raw data handler invoked for incoming text frames without JSON parsing
 * (used by terminal sessions and similar scenarios).
 *
 * @param client  Pointer to the client.
 * @param handler Callback function. May be NULL to clear.
 */
void ws_client_set_raw_handler(ws_client_t *client, ws_raw_handler_t handler);

/**
 * Set the user data pointer stored on the client.
 *
 * @param client Pointer to the client.
 * @param data   Opaque user data pointer.
 */
void ws_client_set_user_data(ws_client_t *client, void *data);

/**
 * Request the client to stop its main loop.
 *
 * @param client Pointer to the client.
 */
void ws_client_stop(ws_client_t *client);

/* Protocol fallback mechanism related interfaces */

/**
 * Get the currently used protocol version.
 *
 * @param client Pointer to the client.
 * @return 1 for the v1 protocol, 2 for the v2 protocol.
 */
int ws_client_get_protocol_version(ws_client_t *client);

/**
 * Check whether the v2 protocol should be used.
 *
 * @param client Pointer to the client.
 * @return true when consecutive v2 failures have not reached the threshold (3 times).
 */
bool ws_client_should_use_v2(ws_client_t *client);

/**
 * Record protocol attempt result (used for the protocol fallback mechanism).
 *
 * @param client  Pointer to the client.
 * @param success true indicates this connection/communication succeeded,
 *                false indicates failure.
 */
void ws_client_note_protocol_result(ws_client_t *client, bool success);

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

#endif
