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
#include <stddef.h>

#include "v2.h"
#include "protocol.h"

#define WS_MAX_MESSAGE_SIZE (64 * 1024)
#define WS_PING_INTERVAL 30
#define WS_KEY_LEN 24

/* Maximum accumulated size for a fragmented WebSocket message (RFC 6455 §5.4).
 * When the total payload of a fragmented message exceeds this limit, the
 * connection is closed to prevent unbounded memory growth. */
#define WS_FRAGMENT_MAX_SIZE (1024 * 1024)

/* Size of the buffer used to hold bytes read past the HTTP handshake
 * response. The server may send the first WebSocket frame immediately after
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

/* Opaque handle to a WebSocket client. The internal layout is defined in
 * websocket_internal.h so that production code (main.c) cannot reach into
 * private fields, while unit tests retain white-box access for state
 * assertions. */
typedef struct ws_client ws_client_t;

typedef void (*ws_message_handler_t)(ws_client_t *client, const ws_message_t *msg);
/* Raw data callback: used for terminal and similar scenarios, passes frame data directly without JSON parsing */
typedef void (*ws_raw_handler_t)(ws_client_t *client, const char *data, size_t len);

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
 * Threading: the handler is invoked on the client's receive thread. Handlers
 * must not block, must not call ws_client_destroy, and must not acquire locks
 * that could be held by the caller's thread (see main.c terminal teardown
 * order for an example of the lock-ordering constraints).
 *
 * @param client  Pointer to the client.
 * @param handler Callback function. May be NULL to clear.
 */
void ws_client_set_handler(ws_client_t *client, ws_message_handler_t handler);

/**
 * Set the raw data handler invoked for incoming text frames without JSON parsing
 * (used by terminal sessions and similar scenarios).
 *
 * Threading: the handler is invoked on the client's receive thread. Handlers
 * must not block, must not call ws_client_destroy, and must not acquire locks
 * that could be held by the caller's thread (see main.c terminal teardown
 * order for an example of the lock-ordering constraints).
 *
 * @param client  Pointer to the client.
 * @param handler Callback function. May be NULL to clear.
 */
void ws_client_set_raw_handler(ws_client_t *client, ws_raw_handler_t handler);

/**
 * Set the user data pointer stored on the client.
 *
 * Ownership: caller retains ownership of `data`; the client only stores the
 * pointer and never frees it. The caller must ensure `data` remains valid for
 * the lifetime of the client (or until replaced with another pointer).
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
 * @return PROTOCOL_VERSION_V1 or PROTOCOL_VERSION_V2; returns
 *         PROTOCOL_VERSION_V1 when client is NULL.
 */
protocol_version_t ws_client_get_protocol_version(ws_client_t *client);

/**
 * Check whether the currently active protocol should be used for the next
 * report cycle.
 *
 * The client tracks consecutive v2 failures and falls back to v1 once the
 * threshold (V2_FALLBACK_THRESHOLD) is reached. This accessor centralizes
 * that decision so callers (main.c report loop, ws_client_connect path
 * selection) do not need to inspect protocol_version or v2_state directly.
 *
 * @param client Pointer to the client.
 * @return true when the active protocol should be used (i.e. the fallback
 *         threshold has not been reached); false otherwise.
 */
bool ws_client_should_use_current_protocol(ws_client_t *client);

/**
 * Record protocol attempt result (used for the protocol fallback mechanism).
 *
 * @param client  Pointer to the client.
 * @param success true indicates this connection/communication succeeded,
 *                false indicates failure.
 */
void ws_client_note_protocol_result(ws_client_t *client, bool success);

/**
 * Check whether the client is currently connected to the server.
 *
 * Reads the connected flag under the client's state mutex so callers do not
 * need to take the lock themselves. Equivalent to the previous pattern:
 *
 *     pthread_mutex_lock(&client->state_mutex);
 *     bool c = client->connected;
 *     pthread_mutex_unlock(&client->state_mutex);
 *
 * @param client Pointer to the client. May be NULL (returns false).
 * @return true when the client is connected, false otherwise.
 */
bool ws_client_is_connected(ws_client_t *client);

/**
 * Get the v2 protocol runtime state owned by the client.
 *
 * Returned pointer is valid for the lifetime of the client. Callers may pass
 * it to v2_snapshot_ack_ids / v2_clear_acks and other v2_state_t accessors,
 * which take the state's internal mutex. The pointer must not be freed by
 * the caller.
 *
 * @param client Pointer to the client. May be NULL (returns NULL).
 * @return Pointer to the client's v2_state_t, or NULL if client is NULL.
 */
v2_state_t *ws_client_get_v2_state(ws_client_t *client);

/**
 * Get the user data pointer previously stored via ws_client_set_user_data.
 *
 * @param client Pointer to the client. May be NULL (returns NULL).
 * @return The stored user data pointer, or NULL if none has been set.
 */
void *ws_client_get_user_data(ws_client_t *client);

#endif
