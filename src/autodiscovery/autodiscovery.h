/*
 * Auto-discovery module: register the agent to the Komari panel and
 * persist/reuse client credentials (uuid + token).
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_AUTODISCOVERY_H
#define KOMARI_AGENT_C_AUTODISCOVERY_H

#include <stddef.h>

/* Auto-discovery configuration file path (temporary file, may be lost after restart) */
#define AUTODISCOVERY_FILE_PATH "/tmp/komari-auto-discovery.json"

/* Auto-discovery configuration data structure */
typedef struct {
    char uuid[64];    /* Client UUID */
    char token[128];  /* Client communication token */
} autodiscovery_config_t;

/**
 * Get the auto-discovery configuration file path
 *
 * @param path     Output buffer for storing the path
 * @param path_len Buffer length
 * @return 0 on success, -1 on failure
 */
int autodiscovery_get_file_path(char *path, size_t path_len);

/**
 * Load existing auto-discovery configuration
 *
 * @param config Output configuration structure
 * @return 0 on success, -1 on failure (file not found or parsing failed)
 */
int autodiscovery_load_config(autodiscovery_config_t *config);

/**
 * Save auto-discovery configuration to file
 *
 * @param config Configuration to save
 * @return 0 on success, -1 on failure
 */
int autodiscovery_save_config(const autodiscovery_config_t *config);

/**
 * Register to the Komari panel
 *
 * @param endpoint            Panel address (e.g. https://komari.example.com)
 * @param auto_discovery_key  Auto-discovery key
 * @param hostname            Hostname (used as client name)
 * @param config              Output uuid and token obtained from registration
 * @return 0 on success, -1 on failure
 */
int autodiscovery_register(const char *endpoint,
                            const char *auto_discovery_key,
                            const char *hostname,
                            autodiscovery_config_t *config);

/**
 * Auto-discovery main handler
 *
 * Reuses saved configuration first; if it does not exist or token is empty, initiates registration.
 *
 * @param endpoint            Panel address
 * @param auto_discovery_key  Auto-discovery key
 * @param token               Output available token
 * @param token_len           Token buffer length
 * @return 0 on success, -1 on failure
 */
int autodiscovery_handle(const char *endpoint,
                          const char *auto_discovery_key,
                          char *token,
                          size_t token_len);

#endif
