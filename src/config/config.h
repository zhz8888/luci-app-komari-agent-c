/*
 * Configuration management: agent_config_t structure and loaders for
 * defaults, environment variables, JSON file and OpenWrt UCI.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_CONFIG_H
#define KOMARI_AGENT_C_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#include "komari_errno.h"
#include "protocol.h"

#define MAX_TOKEN_LEN 256
#define MAX_ENDPOINT_LEN 512
#define MAX_DNS_LEN 128
#define MAX_NICS_LEN 512
#define MAX_MOUNTPOINTS_LEN 512
#define MAX_CF_ID_LEN 128
#define MAX_CF_SECRET_LEN 256
#define MAX_IPV4_LEN 64
#define MAX_IPV6_LEN 128
#define MAX_DISCOVERY_KEY_LEN 256
#define MAX_CONFIG_FILE_LEN 512

typedef struct {
    char token[MAX_TOKEN_LEN];
    char endpoint[MAX_ENDPOINT_LEN];
    char custom_dns[MAX_DNS_LEN];
    char include_nics[MAX_NICS_LEN];
    char exclude_nics[MAX_NICS_LEN];
    char include_mountpoints[MAX_MOUNTPOINTS_LEN];
    char cf_access_client_id[MAX_CF_ID_LEN];
    char cf_access_client_secret[MAX_CF_SECRET_LEN];
    char custom_ipv4[MAX_IPV4_LEN];
    char custom_ipv6[MAX_IPV6_LEN];
    char auto_discovery_key[MAX_DISCOVERY_KEY_LEN];
    char config_file[MAX_CONFIG_FILE_LEN];
    
    double interval;
    int max_retries;
    int reconnect_interval;
    int info_report_interval;
    int month_rotate;
    int protocol_version;   /* Active protocol version (protocol_version_t); default PROTOCOL_VERSION_V2 */

    bool disable_auto_update;
    bool disable_web_ssh;
    bool ignore_unsafe_cert;
    bool memory_include_cache;
    bool enable_gpu;
    bool get_ip_addr_from_nic;
    bool show_warning;
    bool disable_compression;   /* Whether to disable gzip compression for v2 protocol */
} agent_config_t;

/**
 * Initialize the agent configuration structure with default values.
 *
 * @param config Configuration structure to initialize
 */
void config_init(agent_config_t *config);

/**
 * Load configuration values from environment variables.
 *
 * Only overrides fields whose environment variable is set; leaves the
 * rest of the configuration untouched.
 *
 * @param config Configuration structure to populate
 * @return 0 on success, -1 on invalid argument
 */
int config_load_from_env(agent_config_t *config);

/**
 * Load configuration values from a JSON configuration file.
 *
 * @param config Configuration structure to populate
 * @param path   Path to the JSON configuration file
 * @return KOMARI_OK on success; KOMARI_ERR_INVALID_ARG if config/path is NULL
 *         or the file is group/world accessible; KOMARI_ERR_NOT_FOUND if the
 *         file cannot be opened; KOMARI_ERR_PARSE if the JSON is invalid;
 *         KOMARI_ERR_NOMEM on allocation failure; KOMARI_ERR_GENERIC on
 *         other I/O failures.
 */
int config_load_from_file(agent_config_t *config, const char *path);

/**
 * Load configuration values from OpenWrt UCI (Unified Configuration
 * Interface) via the `uci show komari-agent-c` command.
 *
 * @param config Configuration structure to populate
 * @return 0 on success, -1 on invalid argument
 */
int config_load_from_uci(agent_config_t *config);

/**
 * Print the current configuration to standard output.
 *
 * @param config Configuration structure to print
 */
void config_print(const agent_config_t *config);

/**
 * Validate the configuration and apply fallback defaults for any field
 * that fails sanity checks (empty required strings, out-of-range numeric
 * values, URL format issues, CR/LF injection in endpoint, etc.).
 *
 * Validation failures are logged at KOMARI_LOG_ERROR level but do not
 * abort the program; the offending field is reset to a safe default
 * (or emptied when no default makes sense, e.g. token/endpoint) so the
 * caller can decide how to react.
 *
 * @param config Configuration structure to validate (modified in place)
 * @return 0 on success (including when fallbacks were applied), -1 on
 *         invalid argument
 */
int config_validate(agent_config_t *config);

#endif
