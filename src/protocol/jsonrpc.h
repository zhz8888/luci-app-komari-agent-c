/*
 * JSON-RPC 2.0 message construction and parsing for the v2 protocol.
 * Provides notification/request builders and response/event parsers.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_JSONRPC_H
#define KOMARI_AGENT_C_JSONRPC_H

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"

/* JSON-RPC 2.0 version */
#define JSONRPC_VERSION "2.0"

/* Reporting methods */
#define AGENT_REPORT           "agent.report"
#define AGENT_BASIC_INFO       "agent.basicInfo"
#define AGENT_PING_RESULT      "agent.pingResult"
#define AGENT_TASK_RESULT      "agent.taskResult"

/* Event methods */
#define AGENT_EXEC             "agent.exec"
#define AGENT_PING             "agent.ping"
#define AGENT_MESSAGE          "agent.message"
#define AGENT_EVENT            "agent.event"
#define AGENT_TERMINAL_REQUEST "agent.terminal.request"
#define AGENT_PULL             "agent.pull"

/* JSON-RPC error object */
typedef struct {
    int code;            /* Error code */
    char *message;       /* Error message */
    cJSON *data;         /* Additional data, may be NULL */
} jsonrpc_error_t;

/* JSON-RPC request/notification */
typedef struct {
    char *jsonrpc;       /* Version string, typically "2.0" */
    char *method;        /* Method name */
    cJSON *params;       /* Parameter object, may be NULL */
    int id;              /* Request ID; when has_id is false it represents a notification */
    bool has_id;         /* Whether an ID is present */
} jsonrpc_request_t;

/* JSON-RPC response */
typedef struct {
    char *jsonrpc;       /* Version string */
    int id;              /* ID of the corresponding request */
    bool has_id;         /* Whether an ID is present */
    cJSON *result;       /* Result data, may be NULL */
    jsonrpc_error_t *error; /* Error object, may be NULL */
} jsonrpc_response_t;

/* JSON-RPC event (delivered by the server) */
typedef struct {
    char *id;            /* Event ID */
    char *method;        /* Method name */
    cJSON *params;       /* Parameter object, may be NULL */
    char *created_at;    /* Creation time, may be NULL */
    char *expires_at;    /* Expiration time, may be NULL */
} jsonrpc_event_t;

/**
 * Create a notification (no ID).
 *
 * @param method Method name string.
 * @param params Parameter object. Ownership is transferred to the returned
 *               cJSON object; the caller must not free it.
 * @return Newly constructed cJSON notification object. The caller must free it
 *         using cJSON_Delete. Returns NULL on failure.
 */
cJSON *jsonrpc_new_notification(const char *method, cJSON *params);

/**
 * Create a request (with ID).
 *
 * @param id     Request ID.
 * @param method Method name string.
 * @param params Parameter object. Ownership is transferred to the returned
 *               cJSON object; the caller must not free it.
 * @return Newly constructed cJSON request object. The caller must free it
 *         using cJSON_Delete. Returns NULL on failure.
 */
cJSON *jsonrpc_new_request(int id, const char *method, cJSON *params);

/**
 * Wrap monitoring data as an agent.report notification.
 *
 * @param report_data Monitoring data cJSON object. Ownership is transferred on
 *                     input; the caller must not free it.
 * @return Newly constructed cJSON notification. The caller must free it using
 *         cJSON_Delete. Returns NULL on failure.
 */
cJSON *jsonrpc_build_report_payload(cJSON *report_data);

/**
 * Wrap basic info as an agent.basicInfo notification.
 *
 * @param info_data Basic info cJSON object. Ownership is transferred on input;
 *                  the caller must not free it.
 * @return Newly constructed cJSON notification. The caller must free it using
 *         cJSON_Delete. Returns NULL on failure.
 */
cJSON *jsonrpc_build_basic_info_payload(cJSON *info_data);

/**
 * Wrap a Ping result as an agent.pingResult request.
 *
 * @param id          JSON-RPC request ID.
 * @param ping_result Pre-built result object. Ownership is transferred on input;
 *                     the caller must not free it.
 * @return Newly constructed cJSON request. The caller must free it using
 *         cJSON_Delete. Returns NULL on failure.
 */
cJSON *jsonrpc_build_ping_result_payload(int id, cJSON *ping_result);

/**
 * Build an agent.report request with ACKs.
 *
 * @param id         JSON-RPC request ID.
 * @param report_data Monitoring data cJSON object. Ownership is transferred on
 *                    input; the caller must not free it.
 * @param ack_ids    Array of event IDs pending acknowledgment. May be NULL when
 *                   ack_count is 0.
 * @param ack_count  Number of items in ack_ids.
 * @return Newly constructed cJSON request. The caller must free it using
 *         cJSON_Delete. Returns NULL on failure.
 */
cJSON *jsonrpc_build_report_request(int id, cJSON *report_data,
                                     const int *ack_ids, int ack_count);

/**
 * Parse a JSON-RPC response string.
 *
 * @param json_str Response JSON string to parse.
 * @param response Output parsed response structure. The caller must call
 *                 jsonrpc_free_response to release resources after use.
 * @return 0 on success, -1 on failure.
 */
int jsonrpc_parse_response(const char *json_str, jsonrpc_response_t *response);

/**
 * Parse a JSON-RPC event string.
 *
 * @param json_str Event JSON string to parse.
 * @param event    Output parsed event structure. The caller must call
 *                 jsonrpc_free_event to release resources after use.
 * @return 0 on success, -1 on failure.
 */
int jsonrpc_parse_event(const char *json_str, jsonrpc_event_t *event);

/**
 * Parse a JSON-RPC event from an existing cJSON tree.
 *
 * This variant avoids re-parsing the JSON string when the caller already has
 * a cJSON root (e.g. the WebSocket recv thread that inspects the jsonrpc
 * field to detect v2 messages). The caller retains ownership of root.
 *
 * @param root  Parsed cJSON tree (must not be NULL).
 * @param event Output parsed event structure. The caller must call
 *              jsonrpc_free_event to release resources after use.
 * @return 0 on success, -1 on failure.
 */
int jsonrpc_parse_event_from_json(cJSON *root, jsonrpc_event_t *event);

/**
 * Free the request structure and its internal resources.
 *
 * @param req Pointer to the request structure to free.
 */
void jsonrpc_free_request(jsonrpc_request_t *req);

/**
 * Free the response structure and its internal resources.
 *
 * @param resp Pointer to the response structure to free.
 */
void jsonrpc_free_response(jsonrpc_response_t *resp);

/**
 * Free the event structure and its internal resources.
 *
 * @param event Pointer to the event structure to free.
 */
void jsonrpc_free_event(jsonrpc_event_t *event);

#endif /* KOMARI_AGENT_C_JSONRPC_H */
