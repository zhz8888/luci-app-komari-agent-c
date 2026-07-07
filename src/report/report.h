/*
 * Status report generation and upload interface declarations.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_REPORT_H
#define KOMARI_AGENT_C_REPORT_H

#include <stdint.h>
#include <stddef.h>

#include "monitoring.h"
#include "config.h"

typedef struct {
    double cpu_usage;
    uint64_t mem_total;
    uint64_t mem_used;
    uint64_t swap_total;
    uint64_t swap_used;
    double load1;
    double load5;
    double load15;
    uint64_t disk_total;
    uint64_t disk_used;
    uint64_t net_up;
    uint64_t net_down;
    uint64_t net_total_up;
    uint64_t net_total_down;
    int tcp_count;
    int udp_count;
    uint64_t uptime;
    int process_count;
    char message[256];
} report_data_t;

/**
 * Generate the periodic status report JSON payload.
 *
 * @param config    Agent configuration
 * @param net_state Caller-owned network rate calculation state. Pass NULL to
 *                  skip speed calculation (rx_speed/tx_speed will be 0).
 * @param buf       Output buffer for the JSON payload
 * @param buf_len   Size of buf
 * @return Number of bytes written on success, -1 on failure
 */
int report_generate(const agent_config_t *config, monitoring_net_state_t *net_state,
                    char *buf, size_t buf_len);

/**
 * Generate the periodic status report payload wrapped as a v2 JSON-RPC 2.0
 * notification (method = "agent.report", params.report = original report).
 *
 * Internally calls report_generate to produce the v1 JSON, then embeds it
 * verbatim under params.report via direct string concatenation. This avoids
 * the cJSON_Parse → cJSON_Print round-trip on the 1 Hz report path.
 *
 * @param config    Agent configuration
 * @param net_state Caller-owned network rate calculation state (see report_generate)
 * @param buf       Output buffer for the JSON-RPC payload
 * @param buf_len   Size of buf
 * @return Number of bytes written on success, -1 on failure
 */
int report_generate_v2(const agent_config_t *config, monitoring_net_state_t *net_state,
                       char *buf, size_t buf_len);

/**
 * Generate the periodic status report payload wrapped as a v2 JSON-RPC 2.0
 * request (method = "agent.report") carrying pending ACK event IDs.
 *
 * Unlike report_generate_v2 (which builds a notification), this builds a
 * JSON-RPC request with an integer id and an ack_event_ids array under
 * params, so the server can correlate the report with previously delivered
 * events and stop retransmitting them. The request id is derived from the
 * current time so each report cycle uses a distinct id.
 *
 * @param config    Agent configuration
 * @param net_state Caller-owned network rate calculation state (see report_generate)
 * @param buf       Output buffer for the JSON-RPC payload
 * @param buf_len   Size of buf
 * @param ack_ids   Array of pending ACK event IDs to include in the report.
 *                  May be NULL when ack_count is 0.
 * @param ack_count Number of items in ack_ids
 * @return Number of bytes written on success, -1 on failure
 */
int report_generate_v2_with_acks(const agent_config_t *config,
                                 monitoring_net_state_t *net_state,
                                 char *buf, size_t buf_len, const int *ack_ids,
                                 int ack_count);

/**
 * Generate the basic (one-time) system info JSON payload.
 *
 * @param config Agent configuration
 * @param buf Output buffer for the JSON payload
 * @param buf_len Size of buf
 * @return Number of bytes written on success, -1 on failure
 */
int report_generate_basic_info(const agent_config_t *config, char *buf, size_t buf_len);

/**
 * Generate the basic system info payload wrapped as a v2 JSON-RPC 2.0
 * notification (method = "agent.basicInfo", params.info = original basic info).
 *
 * @param config Agent configuration
 * @param buf Output buffer for the JSON-RPC payload
 * @param buf_len Size of buf
 * @return Number of bytes written on success, -1 on failure
 */
int report_generate_basic_info_v2(const agent_config_t *config, char *buf, size_t buf_len);

/**
 * Upload a task execution result to the panel server.
 *
 * @param config Agent configuration
 * @param task_id Task identifier
 * @param result Task result text (may be NULL)
 * @param exit_code Task exit code
 * @param finished_at Completion timestamp (unix seconds)
 * @return 0 on success, -1 on failure
 */
int report_upload_task_result(const agent_config_t *config,
                               const char *task_id,
                               const char *result,
                               int exit_code,
                               uint64_t finished_at);

/**
 * Upload a ping task result to the panel server.
 *
 * @param config Agent configuration
 * @param task_id Ping task identifier
 * @param ping_type Ping type string (icmp/tcp/http)
 * @param value Measured latency value
 * @param finished_at Completion timestamp (unix seconds)
 * @return 0 on success, -1 on failure
 */
int report_upload_ping_result(const agent_config_t *config,
                               uint32_t task_id,
                               const char *ping_type,
                               int value,
                               uint64_t finished_at);

#endif
