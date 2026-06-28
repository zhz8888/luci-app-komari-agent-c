/*
 * JSON-RPC 2.0 message construction and parsing implementation.
 * Builds notifications/requests and parses responses/events from the server.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include "jsonrpc.h"

#include <stdlib.h>
#include <string.h>

/* Internal helper: safely duplicate a string. Returns NULL if src is NULL. */
static char *jsonrpc_strdup_safe(const char *src)
{
    if (!src) {
        return NULL;
    }
    char *dst = (char *)malloc(strlen(src) + 1);
    if (dst) {
        strcpy(dst, src);
    }
    return dst;
}

cJSON *jsonrpc_new_notification(const char *method, cJSON *params)
{
    if (!method) {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "jsonrpc", JSONRPC_VERSION);
    cJSON_AddStringToObject(root, "method", method);

    if (params) {
        cJSON_AddItemToObject(root, "params", params);
    }

    return root;
}

cJSON *jsonrpc_new_request(int id, const char *method, cJSON *params)
{
    if (!method) {
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "jsonrpc", JSONRPC_VERSION);
    cJSON_AddStringToObject(root, "method", method);
    cJSON_AddNumberToObject(root, "id", id);

    if (params) {
        cJSON_AddItemToObject(root, "params", params);
    }

    return root;
}

cJSON *jsonrpc_build_report_payload(cJSON *report_data)
{
    cJSON *params = cJSON_CreateObject();
    if (!params) {
        return NULL;
    }

    if (report_data) {
        cJSON_AddItemToObject(params, "report", report_data);
    }

    cJSON *root = jsonrpc_new_notification(AGENT_REPORT, params);
    if (!root) {
        /* On construction failure, free params along with the contained report_data */
        cJSON_Delete(params);
        return NULL;
    }

    return root;
}

cJSON *jsonrpc_build_basic_info_payload(cJSON *info_data)
{
    cJSON *params = cJSON_CreateObject();
    if (!params) {
        return NULL;
    }

    if (info_data) {
        cJSON_AddItemToObject(params, "info", info_data);
    }

    cJSON *root = jsonrpc_new_notification(AGENT_BASIC_INFO, params);
    if (!root) {
        cJSON_Delete(params);
        return NULL;
    }

    return root;
}

cJSON *jsonrpc_build_ping_result_payload(int id, cJSON *ping_result)
{
    /* ping_result is passed directly as params; ownership is transferred to the request object */
    return jsonrpc_new_request(id, AGENT_PING_RESULT, ping_result);
}

cJSON *jsonrpc_build_report_request(int id, cJSON *report_data,
                                     const int *ack_ids, int ack_count)
{
    cJSON *params = cJSON_CreateObject();
    if (!params) {
        return NULL;
    }

    if (report_data) {
        cJSON_AddItemToObject(params, "report", report_data);
    }

    /* Build the ack_event_ids array */
    cJSON *ids_array = cJSON_CreateArray();
    if (ids_array) {
        if (ack_ids && ack_count > 0) {
            int i;
            for (i = 0; i < ack_count; i++) {
                cJSON *item = cJSON_CreateNumber((double)ack_ids[i]);
                if (item) {
                    cJSON_AddItemToArray(ids_array, item);
                }
            }
        }
        cJSON_AddItemToObject(params, "ack_event_ids", ids_array);
    }

    cJSON *root = jsonrpc_new_request(id, AGENT_REPORT, params);
    if (!root) {
        cJSON_Delete(params);
        return NULL;
    }

    return root;
}

int jsonrpc_parse_response(const char *json_str, jsonrpc_response_t *response)
{
    if (!json_str || !response) {
        return -1;
    }

    memset(response, 0, sizeof(*response));

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        return -1;
    }

    /* Parse the jsonrpc version */
    cJSON *jsonrpc = cJSON_GetObjectItem(root, "jsonrpc");
    if (jsonrpc && cJSON_IsString(jsonrpc)) {
        response->jsonrpc = jsonrpc_strdup_safe(jsonrpc->valuestring);
    }

    /* Parse the id */
    cJSON *id = cJSON_GetObjectItem(root, "id");
    if (id) {
        if (cJSON_IsNumber(id)) {
            response->id = id->valueint;
            response->has_id = true;
        } else if (cJSON_IsString(id)) {
            /* Convert a string ID to a number; use 0 if conversion fails */
            response->id = (int)strtol(id->valuestring, NULL, 10);
            response->has_id = true;
        }
    }

    /* Parse the result */
    cJSON *result = cJSON_GetObjectItem(root, "result");
    if (result) {
        response->result = cJSON_Duplicate(result, 1);
    }

    /* Parse the error */
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error && cJSON_IsObject(error)) {
        jsonrpc_error_t *err = (jsonrpc_error_t *)calloc(1, sizeof(jsonrpc_error_t));
        if (err) {
            cJSON *code = cJSON_GetObjectItem(error, "code");
            cJSON *message = cJSON_GetObjectItem(error, "message");
            cJSON *data = cJSON_GetObjectItem(error, "data");

            if (code && cJSON_IsNumber(code)) {
                err->code = code->valueint;
            }
            if (message && cJSON_IsString(message)) {
                err->message = jsonrpc_strdup_safe(message->valuestring);
            }
            if (data) {
                err->data = cJSON_Duplicate(data, 1);
            }
            response->error = err;
        }
    }

    cJSON_Delete(root);
    return 0;
}

int jsonrpc_parse_event(const char *json_str, jsonrpc_event_t *event)
{
    if (!json_str || !event) {
        return -1;
    }

    memset(event, 0, sizeof(*event));

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        return -1;
    }

    cJSON *id = cJSON_GetObjectItem(root, "id");
    if (id && cJSON_IsString(id)) {
        event->id = jsonrpc_strdup_safe(id->valuestring);
    }

    cJSON *method = cJSON_GetObjectItem(root, "method");
    if (method && cJSON_IsString(method)) {
        event->method = jsonrpc_strdup_safe(method->valuestring);
    }

    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (params) {
        event->params = cJSON_Duplicate(params, 1);
    }

    cJSON *created_at = cJSON_GetObjectItem(root, "created_at");
    if (created_at && cJSON_IsString(created_at)) {
        event->created_at = jsonrpc_strdup_safe(created_at->valuestring);
    }

    cJSON *expires_at = cJSON_GetObjectItem(root, "expires_at");
    if (expires_at && cJSON_IsString(expires_at)) {
        event->expires_at = jsonrpc_strdup_safe(expires_at->valuestring);
    }

    cJSON_Delete(root);
    return 0;
}

void jsonrpc_free_request(jsonrpc_request_t *req)
{
    if (!req) {
        return;
    }
    free(req->jsonrpc);
    free(req->method);
    if (req->params) {
        cJSON_Delete(req->params);
    }
    /* id is an integer; no need to free */
}

void jsonrpc_free_response(jsonrpc_response_t *resp)
{
    if (!resp) {
        return;
    }
    free(resp->jsonrpc);
    if (resp->result) {
        cJSON_Delete(resp->result);
    }
    if (resp->error) {
        free(resp->error->message);
        if (resp->error->data) {
            cJSON_Delete(resp->error->data);
        }
        free(resp->error);
    }
}

void jsonrpc_free_event(jsonrpc_event_t *event)
{
    if (!event) {
        return;
    }
    free(event->id);
    free(event->method);
    if (event->params) {
        cJSON_Delete(event->params);
    }
    free(event->created_at);
    free(event->expires_at);
}
