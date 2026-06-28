/*
 * v1 protocol payload construction implementation.
 * The v1 protocol uses raw JSON for reporting without any wrapping.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include "v1.h"

#include <stdlib.h>

int v1_build_report_payload(cJSON *report_data, char **output)
{
    if (!output) {
        return -1;
    }

    *output = NULL;

    if (!report_data) {
        return -1;
    }

    /* Generate a compact JSON string */
    char *json_str = cJSON_PrintUnformatted(report_data);
    if (!json_str) {
        return -1;
    }

    *output = json_str;
    return 0;
}

int v1_build_basic_info_payload(cJSON *info_data, char **output)
{
    if (!output) {
        return -1;
    }

    *output = NULL;

    if (!info_data) {
        return -1;
    }

    /* Generate a compact JSON string */
    char *json_str = cJSON_PrintUnformatted(info_data);
    if (!json_str) {
        return -1;
    }

    *output = json_str;
    return 0;
}
