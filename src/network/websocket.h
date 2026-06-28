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

#include "config.h"
#include "monitoring.h"
#include "v2.h"

#define WS_MAX_MESSAGE_SIZE (64 * 1024)
#define WS_PING_INTERVAL 30
#define WS_KEY_LEN 24

typedef struct {
    char *endpoint;
    char *token;
    char *extra_query;  /* Additional query parameters, e.g., "&id=xxx", appended after token */
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
 * Run the client's main loop (connect with retries and maintain the connection).
 *
 * @param client Pointer to the client.
 * @return 0 on success, -1 on failure.
 */
int ws_client_run(ws_client_t *client);

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

#endif
