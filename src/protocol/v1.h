/*
 * v1 protocol definitions and payload construction.
 * The v1 protocol sends raw JSON (no wrapping) to the Komari server.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_V1_H
#define KOMARI_AGENT_C_V1_H

#include "cJSON.h"

/* v1 protocol endpoint constants */
#define V1_REPORT_ENDPOINT       "/api/clients/report"
#define V1_BASIC_INFO_ENDPOINT   "/api/clients/uploadBasicInfo"
#define V1_TASK_RESULT_ENDPOINT  "/api/clients/task/result"

/**
 * Build the v1 report payload as a compact JSON string.
 * The v1 protocol uses raw JSON directly without any wrapping.
 *
 * @param report_data Monitoring data cJSON object. The function copies it
 *                    internally; the caller remains responsible for freeing it.
 * @param output      Outputs a compact JSON string. The caller must free it.
 * @return 0 on success, -1 on failure.
 */
int v1_build_report_payload(cJSON *report_data, char **output);

/**
 * Build the v1 basic info payload as a compact JSON string.
 *
 * @param info_data Basic info cJSON object. The function copies it internally;
 *                  the caller remains responsible for freeing it.
 * @param output    Outputs a compact JSON string. The caller must free it.
 * @return 0 on success, -1 on failure.
 */
int v1_build_basic_info_payload(cJSON *info_data, char **output);

#endif /* KOMARI_AGENT_C_V1_H */
