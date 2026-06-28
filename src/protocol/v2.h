/*
 * v2 protocol definitions, runtime state and payload construction.
 * The v2 protocol wraps monitoring data in JSON-RPC 2.0 messages.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_V2_H
#define KOMARI_AGENT_C_V2_H

#include <stdbool.h>
#include <pthread.h>

#include "cJSON.h"

/* v2 protocol endpoint constants */
#define V2_RPC_ENDPOINT "/api/clients/v2/rpc"

/* Threshold of v2 failures after which the agent falls back to v1 */
#define V2_FALLBACK_THRESHOLD 3

/* Maximum number of seen event IDs; the oldest is removed when full */
#define V2_SEEN_EVENTS_MAX 1000

/* v2 protocol runtime state */
typedef struct {
    int fail_count;             /* Number of consecutive v2 failures */

    /* Array of seen event IDs (used for deduplication) */
    char **seen_events;
    int seen_count;
    int seen_capacity;

    /* Array of pending ACK event IDs */
    int *ack_ids;
    int ack_count;
    int ack_capacity;

    pthread_mutex_t mutex;
} v2_state_t;

/**
 * Build the v2 report payload (JSON-RPC wrapped) as a compact JSON string.
 *
 * @param report_data Monitoring data cJSON object. Ownership is transferred on
 *                    input; the caller must not free it.
 * @param output      Outputs a compact JSON string. The caller must free it.
 * @return 0 on success, -1 on failure.
 */
int v2_build_report_payload(cJSON *report_data, char **output);

/**
 * Build the v2 basic info payload (JSON-RPC wrapped).
 *
 * @param info_data Basic info cJSON object. Ownership is transferred on input;
 *                  the caller must not free it.
 * @param output    Outputs a compact JSON string. The caller must free it.
 * @return 0 on success, -1 on failure.
 */
int v2_build_basic_info_payload(cJSON *info_data, char **output);

/**
 * Build the v2 Ping result payload (JSON-RPC wrapped).
 *
 * @param id           JSON-RPC request ID.
 * @param ping_result  Pre-built result object. Ownership is transferred on input;
 *                     the caller must not free it.
 * @param output       Outputs a compact JSON string. The caller must free it.
 * @return 0 on success, -1 on failure.
 */
int v2_build_ping_result_payload(int id, cJSON *ping_result, char **output);

/**
 * Build the v2 task result payload (JSON-RPC wrapped).
 *
 * @param id          JSON-RPC request ID.
 * @param task_result Pre-built task result object. Ownership is transferred on
 *                    input; the caller must not free it.
 * @param output      Outputs a compact JSON string. The caller must free it.
 * @return 0 on success, -1 on failure.
 */
int v2_build_task_result_payload(int id, cJSON *task_result, char **output);

/**
 * Initialize the v2 state.
 *
 * @param state Pointer to the v2_state_t structure to initialize.
 * @return 0 on success, -1 on failure.
 */
int v2_state_init(v2_state_t *state);

/**
 * Clean up the v2 state and its internal resources.
 *
 * @param state Pointer to the v2_state_t structure to clean up.
 */
void v2_state_cleanup(v2_state_t *state);

/**
 * Record the result of a v2 attempt.
 *
 * @param state   Pointer to the v2 state.
 * @param success 1 means success (resets the failure counter);
 *                0 means failure (increments the counter by one).
 * @return The current failure count.
 */
int v2_note_attempt_result(v2_state_t *state, int success);

/**
 * Determine whether to downgrade to v1.
 *
 * @param state Pointer to the v2 state.
 * @return true when the number of consecutive failures reaches the threshold (3).
 */
bool v2_should_fallback_to_v1(v2_state_t *state);

/**
 * Add a seen event ID (used for deduplication).
 *
 * @param state    Pointer to the v2 state.
 * @param event_id Event ID string to record.
 * @return 0 on success, -1 on failure.
 */
int v2_add_seen_event(v2_state_t *state, const char *event_id);

/**
 * Check whether an event has already been processed.
 *
 * @param state    Pointer to the v2 state.
 * @param event_id Event ID string to look up.
 * @return true if seen, false if not seen.
 */
bool v2_is_event_seen(v2_state_t *state, const char *event_id);

/**
 * Add a pending ACK event ID.
 *
 * @param state    Pointer to the v2 state.
 * @param event_id Event ID pending acknowledgment.
 * @return 0 on success, -1 on failure.
 */
int v2_add_ack_event(v2_state_t *state, int event_id);

/**
 * Get the list of pending ACK event IDs.
 *
 * @deprecated This function returns a pointer to the internal buffer; the
 *             pointer becomes dangling if another thread calls
 *             v2_add_ack_event (which may realloc the buffer) or
 *             v2_clear_acks. Use v2_snapshot_ack_ids instead, which copies
 *             the data into a caller-provided buffer under the lock.
 *
 * @param state Pointer to the v2 state.
 * @param ids   Outputs an array pointer pointing to an internal buffer; the
 *              caller must not free it.
 * @param count Outputs the number of items in the array.
 * @return 0 on success, -1 on failure.
 */
int v2_get_ack_ids(v2_state_t *state, int **ids, int *count);

/**
 * Snapshot the pending ACK event IDs into a caller-provided buffer.
 *
 * Copies up to `max` ACK IDs into `buf` under the state mutex, so the
 * caller never holds a pointer into internal state. This is safe to call
 * concurrently with v2_add_ack_event / v2_clear_acks.
 *
 * @param state Pointer to the v2 state.
 * @param buf   Caller-provided buffer that receives the ACK IDs. Must not be
 *              NULL. If the internal state holds more than `max` IDs, only
 *              the first `max` are copied.
 * @param max   Maximum number of IDs that `buf` can hold.
 * @param count Outputs the number of IDs actually copied (0..max). Must not
 *              be NULL.
 * @return 0 on success, -1 on invalid arguments.
 */
int v2_snapshot_ack_ids(v2_state_t *state, int *buf, int max, int *count);

/**
 * Clear the list of pending ACK events.
 *
 * @param state Pointer to the v2 state.
 */
void v2_clear_acks(v2_state_t *state);

#endif /* KOMARI_AGENT_C_V2_H */
