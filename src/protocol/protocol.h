/*
 * Protocol version identifiers shared by the WebSocket client, the report
 * builder and the configuration loader.
 *
 * The numeric values (1, 2) are stable: they are stored in agent_config_t and
 * ws_client_t as plain int so that cJSON-based config loading keeps working
 * unchanged, and they match the values used by the Komari server.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_PROTOCOL_H
#define KOMARI_AGENT_C_PROTOCOL_H

typedef enum {
    PROTOCOL_VERSION_V1 = 1,  /* Raw JSON to /api/clients/report */
    PROTOCOL_VERSION_V2 = 2,  /* JSON-RPC 2.0 envelope to /api/clients/v2/rpc */
} protocol_version_t;

#endif /* KOMARI_AGENT_C_PROTOCOL_H */
