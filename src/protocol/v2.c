/*
 * v2 protocol payload construction and runtime state management.
 * Wraps monitoring data in JSON-RPC 2.0 messages and tracks the v2 state
 * used by the protocol fallback mechanism.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include "v2.h"

#include <stdlib.h>
#include <string.h>

#include "jsonrpc.h"

/* ----------------------------------------------------------------------- */
/* Payload construction functions                                          */
/* ----------------------------------------------------------------------- */

int v2_build_report_payload(cJSON *report_data, char **output)
{
    if (!output) {
        return -1;
    }
    *output = NULL;

    /* jsonrpc_build_report_payload takes ownership of report_data */
    cJSON *root = jsonrpc_build_report_payload(report_data);
    if (!root) {
        return -1;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root); /* also frees report_data */

    if (!json_str) {
        return -1;
    }

    *output = json_str;
    return 0;
}

int v2_build_basic_info_payload(cJSON *info_data, char **output)
{
    if (!output) {
        return -1;
    }
    *output = NULL;

    cJSON *root = jsonrpc_build_basic_info_payload(info_data);
    if (!root) {
        return -1;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        return -1;
    }

    *output = json_str;
    return 0;
}

int v2_build_ping_result_payload(int id, cJSON *ping_result, char **output)
{
    if (!output) {
        return -1;
    }
    *output = NULL;

    cJSON *root = jsonrpc_build_ping_result_payload(id, ping_result);
    if (!root) {
        return -1;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        return -1;
    }

    *output = json_str;
    return 0;
}

int v2_build_task_result_payload(int id, cJSON *task_result, char **output)
{
    if (!output) {
        return -1;
    }
    *output = NULL;

    /* jsonrpc_new_request takes ownership of task_result on success. On
     * failure it does not free task_result, so free it here for consistent
     * ownership semantics (MIN-43). */
    cJSON *root = jsonrpc_new_request(id, AGENT_TASK_RESULT, task_result);
    if (!root) {
        cJSON_Delete(task_result);
        return -1;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        return -1;
    }

    *output = json_str;
    return 0;
}

/* ----------------------------------------------------------------------- */
/* State management                                                        */
/* ----------------------------------------------------------------------- */

/* Initial capacity */
#define V2_SEEN_EVENTS_INIT_CAPACITY 64
#define V2_ACK_IDS_INIT_CAPACITY 16

int v2_state_init(v2_state_t *state)
{
    if (!state) {
        return -1;
    }

    memset(state, 0, sizeof(*state));

    if (pthread_mutex_init(&state->mutex, NULL) != 0) {
        return -1;
    }

    state->fail_count = 0;
    state->seen_events = NULL;
    state->seen_count = 0;
    state->seen_capacity = 0;
    state->ack_ids = NULL;
    state->ack_count = 0;
    state->ack_capacity = 0;

    return 0;
}

void v2_state_cleanup(v2_state_t *state)
{
    if (!state) {
        return;
    }

    pthread_mutex_lock(&state->mutex);

    /* Free the seen events array */
    if (state->seen_events) {
        int i;
        for (i = 0; i < state->seen_count; i++) {
            free(state->seen_events[i]);
        }
        free(state->seen_events);
        state->seen_events = NULL;
        state->seen_count = 0;
        state->seen_capacity = 0;
    }

    /* Free the pending ACK array */
    if (state->ack_ids) {
        free(state->ack_ids);
        state->ack_ids = NULL;
        state->ack_count = 0;
        state->ack_capacity = 0;
    }

    state->fail_count = 0;

    pthread_mutex_unlock(&state->mutex);
    pthread_mutex_destroy(&state->mutex);
}

int v2_note_attempt_result(v2_state_t *state, int success)
{
    if (!state) {
        return 0;
    }

    pthread_mutex_lock(&state->mutex);

    if (success) {
        state->fail_count = 0;
    } else {
        state->fail_count++;
    }

    int count = state->fail_count;

    pthread_mutex_unlock(&state->mutex);
    return count;
}

bool v2_should_fallback_to_v1(v2_state_t *state)
{
    if (!state) {
        return false;
    }

    pthread_mutex_lock(&state->mutex);
    bool fallback = (state->fail_count >= V2_FALLBACK_THRESHOLD);
    pthread_mutex_unlock(&state->mutex);

    return fallback;
}

/* Internal function: unlocked seen-event check. The caller must hold state->mutex. */
static bool v2_is_event_seen_locked(v2_state_t *state, const char *event_id)
{
    int i;
    for (i = 0; i < state->seen_count; i++) {
        if (state->seen_events[i] && strcmp(state->seen_events[i], event_id) == 0) {
            return true;
        }
    }
    return false;
}

int v2_add_seen_event(v2_state_t *state, const char *event_id)
{
    if (!state || !event_id) {
        return -1;
    }

    pthread_mutex_lock(&state->mutex);

    /* Skip if already present */
    if (v2_is_event_seen_locked(state, event_id)) {
        pthread_mutex_unlock(&state->mutex);
        return 0;
    }

    /* When the limit is reached, remove the oldest one first. This avoids a
     * wasted realloc when seen_count == V2_SEEN_EVENTS_MAX == seen_capacity:
     * the old code expanded first, then dropped the oldest entry, leaving
     * the extra capacity forever unused. Checking the limit first keeps
     * seen_count < V2_SEEN_EVENTS_MAX <= seen_capacity, so the expansion
     * branch below is skipped. */
    if (state->seen_count >= V2_SEEN_EVENTS_MAX) {
        free(state->seen_events[0]);
        memmove(&state->seen_events[0], &state->seen_events[1],
                (state->seen_count - 1) * sizeof(char *));
        state->seen_count--;
    }

    /* Expand capacity when insufficient */
    if (state->seen_count >= state->seen_capacity) {
        int new_capacity;
        if (state->seen_capacity == 0) {
            new_capacity = V2_SEEN_EVENTS_INIT_CAPACITY;
        } else {
            new_capacity = state->seen_capacity * 2;
        }

        char **new_events = (char **)realloc(state->seen_events,
                                              new_capacity * sizeof(char *));
        if (!new_events) {
            pthread_mutex_unlock(&state->mutex);
            return -1;
        }
        state->seen_events = new_events;
        state->seen_capacity = new_capacity;
    }

    /* Add the new event */
    char *dup = (char *)malloc(strlen(event_id) + 1);
    if (!dup) {
        pthread_mutex_unlock(&state->mutex);
        return -1;
    }
    strcpy(dup, event_id);

    state->seen_events[state->seen_count++] = dup;

    pthread_mutex_unlock(&state->mutex);
    return 0;
}

bool v2_is_event_seen(v2_state_t *state, const char *event_id)
{
    if (!state || !event_id) {
        return false;
    }

    pthread_mutex_lock(&state->mutex);
    bool seen = v2_is_event_seen_locked(state, event_id);
    pthread_mutex_unlock(&state->mutex);

    return seen;
}

int v2_add_ack_event(v2_state_t *state, int event_id)
{
    if (!state) {
        return -1;
    }

    pthread_mutex_lock(&state->mutex);

    /* When the limit is reached, drop the oldest ACK ID first. This caps
     * memory usage when the server floods events faster than the agent
     * can flush them, mirroring the V2_SEEN_EVENTS_MAX policy. */
    if (state->ack_count >= V2_ACK_IDS_MAX) {
        memmove(&state->ack_ids[0], &state->ack_ids[1],
                (state->ack_count - 1) * sizeof(int));
        state->ack_count--;
    }

    /* Expand capacity when insufficient (capped at V2_ACK_IDS_MAX so we
     * never over-allocate beyond the limit). */
    if (state->ack_count >= state->ack_capacity &&
        state->ack_capacity < V2_ACK_IDS_MAX) {
        int new_capacity;
        if (state->ack_capacity == 0) {
            new_capacity = V2_ACK_IDS_INIT_CAPACITY;
        } else {
            new_capacity = state->ack_capacity * 2;
            if (new_capacity > V2_ACK_IDS_MAX) {
                new_capacity = V2_ACK_IDS_MAX;
            }
        }

        int *new_ids = (int *)realloc(state->ack_ids,
                                       new_capacity * sizeof(int));
        if (!new_ids) {
            pthread_mutex_unlock(&state->mutex);
            return -1;
        }
        state->ack_ids = new_ids;
        state->ack_capacity = new_capacity;
    }

    state->ack_ids[state->ack_count++] = event_id;

    pthread_mutex_unlock(&state->mutex);
    return 0;
}

int v2_get_ack_ids(v2_state_t *state, int **ids, int *count)
{
    if (!state || !ids || !count) {
        return -1;
    }

    pthread_mutex_lock(&state->mutex);

    *ids = state->ack_ids;
    *count = state->ack_count;

    pthread_mutex_unlock(&state->mutex);
    return 0;
}

int v2_snapshot_ack_ids(v2_state_t *state, int *buf, int max, int *count)
{
    if (!state || !buf || !count || max < 0) {
        return -1;
    }

    pthread_mutex_lock(&state->mutex);

    int to_copy = state->ack_count;
    if (to_copy > max) {
        to_copy = max;
    }
    if (to_copy > 0) {
        memcpy(buf, state->ack_ids, (size_t)to_copy * sizeof(int));
    }
    *count = to_copy;

    pthread_mutex_unlock(&state->mutex);
    return 0;
}

void v2_clear_acks(v2_state_t *state)
{
    if (!state) {
        return;
    }

    pthread_mutex_lock(&state->mutex);
    /* Only reset the count; keep the buffer for reuse next time */
    state->ack_count = 0;
    pthread_mutex_unlock(&state->mutex);
}
