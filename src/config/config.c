/*
 * Configuration management: defaults, environment variables, JSON file
 * and OpenWrt UCI loading for the Komari agent.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>

#include "config.h"
#include "utils.h"
#include "logger.h"
#include "cJSON.h"

/* Default values used when configuration validation fails. These mirror the
 * defaults applied by config_init() so that a corrupted or partially loaded
 * configuration can be repaired in place without restarting the agent. */
#define CONFIG_DEFAULT_INTERVAL          1.0
#define CONFIG_DEFAULT_MAX_RETRIES       5
#define CONFIG_DEFAULT_RECONNECT_INTERVAL 5
#define CONFIG_DEFAULT_INFO_REPORT_INTERVAL 30
#define CONFIG_DEFAULT_PROTOCOL_VERSION  PROTOCOL_VERSION_V2

/* Length of a canonical UUID string (8-4-4-4-12 hex digits with hyphens). */
#define UUID_LEN 36

/* Check whether a string contains CR or LF characters.
 * Used to prevent HTTP header injection through user-controlled fields
 * such as endpoint URLs. Mirrors the helper in autodiscovery.c. */
static int contains_crlf(const char *s) {
    if (!s) return 0;
    for (; *s; s++) {
        if (*s == '\r' || *s == '\n') return 1;
    }
    return 0;
}

/* Check whether a string looks like a canonical UUID (8-4-4-4-12 hex digits
 * separated by hyphens). Returns 1 on match, 0 otherwise. Only the format
 * is validated; the version/variant bits are not checked. */
static int looks_like_uuid(const char *s) {
    if (!s || strlen(s) != UUID_LEN) return 0;
    for (int i = 0; i < UUID_LEN; i++) {
        char c = s[i];
        /* Hyphens at positions 8, 13, 18, 23 */
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return 0;
        } else if (!isxdigit((unsigned char)c)) {
            return 0;
        }
    }
    return 1;
}

/* Extract the port portion from a URL string of the form
 * scheme://host[:port][/path]. Returns the port as a positive integer on
 * success, or -1 when no explicit port is present or parsing fails. */
static int parse_endpoint_port(const char *url) {
    if (!url) return -1;

    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
    } else {
        return -1;
    }

    /* Skip userinfo and host (everything up to ':', '/', '?' or end). */
    const char *port_start = NULL;
    for (; *p && *p != '/' && *p != '?'; p++) {
        if (*p == '@') {
            /* Reset: port can only appear after the host, not userinfo */
            port_start = NULL;
            continue;
        }
        if (*p == ':') {
            port_start = p + 1;
        }
    }
    if (!port_start) return -1;

    /* Parse numeric port up to the next '/', '?' or end of string. */
    int port = 0;
    size_t digits = 0;
    while (port_start[digits] && port_start[digits] != '/' && port_start[digits] != '?') {
        if (!isdigit((unsigned char)port_start[digits])) return -1;
        port = port * 10 + (port_start[digits] - '0');
        if (port > 65535) return -1;
        digits++;
    }
    if (digits == 0) return -1;
    return port;
}

void config_init(agent_config_t *config) {
    if (!config) return;
    
    memset(config, 0, sizeof(agent_config_t));
    
    config->interval = 1.0;
    config->max_retries = 5;
    config->reconnect_interval = 5;
    config->info_report_interval = 30;
    config->month_rotate = 0;
    config->protocol_version = PROTOCOL_VERSION_V2;

    config->disable_auto_update = false;
    config->disable_web_ssh = false;
    config->ignore_unsafe_cert = false;
    config->memory_include_cache = false;
    config->enable_gpu = false;
    config->get_ip_addr_from_nic = false;
    config->show_warning = false;
    config->disable_compression = false;
}

/* Look up an environment variable and copy its value into `buf`.
 * Unlike a plain getenv wrapper, when the variable is unset the existing
 * contents of `buf` are preserved so a previously loaded value (e.g. from
 * UCI or JSON) is not silently clobbered with an empty string. This
 * implements the configuration priority rule where env vars override only
 * the fields they explicitly set. Returns `buf` on both paths. */
static char *get_env_or_empty(const char *name, char *buf, size_t buf_len) {
    char *val = getenv(name);
    if (val) {
        utils_set_string(buf, buf_len, val);
        return buf;
    }
    /* When environment variable is not set, keep existing value without override (supports configuration priority: environment variables only override set fields) */
    return buf;
}

/* Parse an environment variable as a boolean. Accepts case-insensitive
 * "true"/"1"/"yes"/"on" and "false"/"0"/"no"/"off"; any other value (or an
 * unset variable) yields `default_val` so the caller's prior configuration
 * is preserved. */
static int get_env_bool(const char *name, bool default_val) {
    char *val = getenv(name);
    if (!val) return default_val;

    if (strcasecmp(val, "true") == 0 ||
        strcasecmp(val, "1") == 0 ||
        strcasecmp(val, "yes") == 0 ||
        strcasecmp(val, "on") == 0) {
        return true;
    }

    if (strcasecmp(val, "false") == 0 ||
        strcasecmp(val, "0") == 0 ||
        strcasecmp(val, "no") == 0 ||
        strcasecmp(val, "off") == 0) {
        return false;
    }

    /* Unknown value falls back to default to preserve caller's intent */
    return default_val;
}

/* Parse an environment variable as a double; returns `default_val` when the
 * variable is unset. atof's behavior on malformed input (0.0) is accepted
 * since config_validate will sanity-check the result later. */
static double get_env_double(const char *name, double default_val) {
    char *val = getenv(name);
    if (!val) return default_val;
    return atof(val);
}

/* Parse an environment variable as an int; returns `default_val` when the
 * variable is unset. atoi's behavior on malformed input (0) is accepted
 * since config_validate will sanity-check the result later. */
static int get_env_int(const char *name, int default_val) {
    char *val = getenv(name);
    if (!val) return default_val;
    return atoi(val);
}

int config_load_from_env(agent_config_t *config) {
    if (!config) return -1;
    
    get_env_or_empty("AGENT_TOKEN", config->token, sizeof(config->token));
    get_env_or_empty("AGENT_ENDPOINT", config->endpoint, sizeof(config->endpoint));
    get_env_or_empty("AGENT_CUSTOM_DNS", config->custom_dns, sizeof(config->custom_dns));
    get_env_or_empty("AGENT_INCLUDE_NICS", config->include_nics, sizeof(config->include_nics));
    get_env_or_empty("AGENT_EXCLUDE_NICS", config->exclude_nics, sizeof(config->exclude_nics));
    get_env_or_empty("AGENT_INCLUDE_MOUNTPOINTS", config->include_mountpoints, sizeof(config->include_mountpoints));
    get_env_or_empty("AGENT_CF_ACCESS_CLIENT_ID", config->cf_access_client_id, sizeof(config->cf_access_client_id));
    get_env_or_empty("AGENT_CF_ACCESS_CLIENT_SECRET", config->cf_access_client_secret, sizeof(config->cf_access_client_secret));
    get_env_or_empty("AGENT_CUSTOM_IPV4", config->custom_ipv4, sizeof(config->custom_ipv4));
    get_env_or_empty("AGENT_CUSTOM_IPV6", config->custom_ipv6, sizeof(config->custom_ipv6));
    get_env_or_empty("AGENT_AUTO_DISCOVERY_KEY", config->auto_discovery_key, sizeof(config->auto_discovery_key));
    get_env_or_empty("AGENT_CONFIG_FILE", config->config_file, sizeof(config->config_file));
    
    config->interval = get_env_double("AGENT_INTERVAL", config->interval);
    config->max_retries = get_env_int("AGENT_MAX_RETRIES", config->max_retries);
    config->reconnect_interval = get_env_int("AGENT_RECONNECT_INTERVAL", config->reconnect_interval);
    config->info_report_interval = get_env_int("AGENT_INFO_REPORT_INTERVAL", config->info_report_interval);
    config->month_rotate = get_env_int("AGENT_MONTH_ROTATE", config->month_rotate);
    config->protocol_version = get_env_int("AGENT_PROTOCOL_VERSION", config->protocol_version);
    
    config->disable_auto_update = get_env_bool("AGENT_DISABLE_AUTO_UPDATE", config->disable_auto_update);
    config->disable_web_ssh = get_env_bool("AGENT_DISABLE_WEB_SSH", config->disable_web_ssh);
    config->ignore_unsafe_cert = get_env_bool("AGENT_IGNORE_UNSAFE_CERT", config->ignore_unsafe_cert);
    config->memory_include_cache = get_env_bool("AGENT_MEMORY_INCLUDE_CACHE", config->memory_include_cache);
    config->enable_gpu = get_env_bool("AGENT_ENABLE_GPU", config->enable_gpu);
    config->get_ip_addr_from_nic = get_env_bool("AGENT_GET_IP_ADDR_FROM_NIC", config->get_ip_addr_from_nic);
    config->show_warning = get_env_bool("AGENT_SHOW_WARNING", config->show_warning);
    config->disable_compression = get_env_bool("AGENT_DISABLE_COMPRESSION", config->disable_compression);
    
    return 0;
}



int config_load_from_file(agent_config_t *config, const char *path) {
    if (!config || !path) return KOMARI_ERR_INVALID_ARG;

    FILE *f = fopen(path, "r");
    if (!f) return KOMARI_ERR_NOT_FOUND;

    /* Reject world/group-accessible config files to prevent token and
     * auto_discovery_key leakage to local users. Only the owner should be
     * able to read the file. */
    struct stat st;
    if (fstat(fileno(f), &st) == 0 && (st.st_mode & 077) != 0) {
        KOMARI_LOG_WARN("Config file %s is group/world accessible (mode %03o); refusing to load. Use 'chmod 600 %s' to fix.",
                        path, st.st_mode & 0777, path);
        fclose(f);
        return KOMARI_ERR_INVALID_ARG;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return KOMARI_ERR_GENERIC;
    }
    fseek(f, 0, SEEK_SET);

    char *json = malloc(size + 1);
    if (!json) {
        fclose(f);
        return KOMARI_ERR_NOMEM;
    }

    size_t bytes_read = fread(json, 1, size, f);
    if (bytes_read != (size_t)size) {
        /* Partial read usually indicates an underlying I/O problem (file
         * truncated after ftell, NFS interruption, etc.). Treat it as an
         * error instead of parsing potentially uninitialized memory between
         * bytes_read and size. */
        fprintf(stderr, "Warning: Config file read incomplete: %zu/%ld bytes\n", bytes_read, size);
        free(json);
        fclose(f);
        return KOMARI_ERR_GENERIC;
    }
    json[bytes_read] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(json);
    if (!root) {
        fprintf(stderr, "Failed to parse JSON config from %s\n", path);
        free(json);
        return KOMARI_ERR_PARSE;
    }
    
    cJSON *item;
    
    if ((item = cJSON_GetObjectItem(root, "token")) && item->valuestring) {
        utils_set_string(config->token, sizeof(config->token), item->valuestring);
    }

    if ((item = cJSON_GetObjectItem(root, "endpoint")) && item->valuestring) {
        utils_set_string(config->endpoint, sizeof(config->endpoint), item->valuestring);
    }

    if ((item = cJSON_GetObjectItem(root, "custom_dns")) && item->valuestring) {
        utils_set_string(config->custom_dns, sizeof(config->custom_dns), item->valuestring);
    }

    if ((item = cJSON_GetObjectItem(root, "include_nics")) && item->valuestring) {
        utils_set_string(config->include_nics, sizeof(config->include_nics), item->valuestring);
    }

    if ((item = cJSON_GetObjectItem(root, "exclude_nics")) && item->valuestring) {
        utils_set_string(config->exclude_nics, sizeof(config->exclude_nics), item->valuestring);
    }

    if ((item = cJSON_GetObjectItem(root, "include_mountpoints")) && item->valuestring) {
        utils_set_string(config->include_mountpoints, sizeof(config->include_mountpoints), item->valuestring);
    }

    if ((item = cJSON_GetObjectItem(root, "cf_access_client_id")) && item->valuestring) {
        utils_set_string(config->cf_access_client_id, sizeof(config->cf_access_client_id), item->valuestring);
    }

    if ((item = cJSON_GetObjectItem(root, "cf_access_client_secret")) && item->valuestring) {
        utils_set_string(config->cf_access_client_secret, sizeof(config->cf_access_client_secret), item->valuestring);
    }

    if ((item = cJSON_GetObjectItem(root, "custom_ipv4")) && item->valuestring) {
        utils_set_string(config->custom_ipv4, sizeof(config->custom_ipv4), item->valuestring);
    }

    if ((item = cJSON_GetObjectItem(root, "custom_ipv6")) && item->valuestring) {
        utils_set_string(config->custom_ipv6, sizeof(config->custom_ipv6), item->valuestring);
    }

    if ((item = cJSON_GetObjectItem(root, "auto_discovery_key")) && item->valuestring) {
        utils_set_string(config->auto_discovery_key, sizeof(config->auto_discovery_key), item->valuestring);
    }

    if ((item = cJSON_GetObjectItem(root, "interval")) && item->valuedouble > 0) {
        config->interval = item->valuedouble;
    }
    
    if ((item = cJSON_GetObjectItem(root, "max_retries")) && item->valueint > 0) {
        config->max_retries = item->valueint;
    }
    
    if ((item = cJSON_GetObjectItem(root, "reconnect_interval")) && item->valueint > 0) {
        config->reconnect_interval = item->valueint;
    }
    
    if ((item = cJSON_GetObjectItem(root, "info_report_interval")) && item->valueint > 0) {
        config->info_report_interval = item->valueint;
    }
    
    if ((item = cJSON_GetObjectItem(root, "month_rotate"))) {
        config->month_rotate = item->valueint;
    }

    if ((item = cJSON_GetObjectItem(root, "protocol_version")) && item->valueint > 0) {
        config->protocol_version = item->valueint;
    }
    
    if ((item = cJSON_GetObjectItem(root, "disable_auto_update"))) {
        if (cJSON_IsBool(item)) {
            config->disable_auto_update = cJSON_IsTrue(item);
        } else if (cJSON_IsString(item)) {
            config->disable_auto_update = (strcasecmp(item->valuestring, "true") == 0);
        }
    }
    
    if ((item = cJSON_GetObjectItem(root, "disable_web_ssh"))) {
        if (cJSON_IsBool(item)) {
            config->disable_web_ssh = cJSON_IsTrue(item);
        } else if (cJSON_IsString(item)) {
            config->disable_web_ssh = (strcasecmp(item->valuestring, "true") == 0);
        }
    }
    
    if ((item = cJSON_GetObjectItem(root, "ignore_unsafe_cert"))) {
        if (cJSON_IsBool(item)) {
            config->ignore_unsafe_cert = cJSON_IsTrue(item);
        } else if (cJSON_IsString(item)) {
            config->ignore_unsafe_cert = (strcasecmp(item->valuestring, "true") == 0);
        }
    }
    
    if ((item = cJSON_GetObjectItem(root, "memory_include_cache"))) {
        if (cJSON_IsBool(item)) {
            config->memory_include_cache = cJSON_IsTrue(item);
        } else if (cJSON_IsString(item)) {
            config->memory_include_cache = (strcasecmp(item->valuestring, "true") == 0);
        }
    }

    if ((item = cJSON_GetObjectItem(root, "enable_gpu"))) {
        if (cJSON_IsBool(item)) {
            config->enable_gpu = cJSON_IsTrue(item);
        } else if (cJSON_IsString(item)) {
            config->enable_gpu = (strcasecmp(item->valuestring, "true") == 0);
        }
    }
    
    if ((item = cJSON_GetObjectItem(root, "get_ip_addr_from_nic"))) {
        if (cJSON_IsBool(item)) {
            config->get_ip_addr_from_nic = cJSON_IsTrue(item);
        } else if (cJSON_IsString(item)) {
            config->get_ip_addr_from_nic = (strcasecmp(item->valuestring, "true") == 0);
        }
    }

    if ((item = cJSON_GetObjectItem(root, "disable_compression"))) {
        if (cJSON_IsBool(item)) {
            config->disable_compression = cJSON_IsTrue(item);
        } else if (cJSON_IsString(item)) {
            config->disable_compression = (strcasecmp(item->valuestring, "true") == 0);
        }
    }
    
    cJSON_Delete(root);
    free(json);
    return 0;
}

/* Parse a single UCI output line and apply it to the config.
 *
 * UCI emits lines in the form: package.section.option='value'
 * (the value may also be unquoted). The option name is the substring
 * after the last '.' that precedes '='.
 *
 * Extracted as a standalone, non-static helper so it can be unit-tested
 * without invoking `popen("uci show ...")`.
 *
 * Returns 0 on success, -1 on a malformed line or invalid argument.
 */
int config_parse_uci_line(agent_config_t *config, const char *raw_line) {
    if (!config || !raw_line) return -1;

    char line[512];
    utils_set_string(line, sizeof(line), raw_line);

    /* Locate '=' first, then find the last '.' before it. Finding the last
     * dot (rather than the first) correctly handles option names whose
     * package or section name also contains dots, e.g. the line
     *   komari-agent-c.komari-agent-c.token='xxx'
     * yields key "token" instead of the buggy "komari-agent-c.token". */
    char *eq = strchr(line, '=');
    if (!eq) return -1;

    *eq = '\0';
    char *dot = strrchr(line, '.');
    if (!dot) return -1;

    char key[64];
    utils_set_string(key, sizeof(key), dot + 1);

    /* Value: skip leading spaces/quotes, then trim trailing newline,
     * quote and space characters. */
    char *val = eq + 1;
    while (*val == ' ' || *val == '\'') val++;

    char value[256];
    utils_set_string(value, sizeof(value), val);

    size_t vlen = strlen(value);
    while (vlen > 0 && (value[vlen - 1] == '\n' ||
                        value[vlen - 1] == '\'' ||
                        value[vlen - 1] == ' ')) {
        value[--vlen] = '\0';
    }

    if (strcmp(key, "token") == 0) {
        utils_set_string(config->token, sizeof(config->token), value);
    } else if (strcmp(key, "endpoint") == 0) {
        utils_set_string(config->endpoint, sizeof(config->endpoint), value);
    } else if (strcmp(key, "interval") == 0) {
        config->interval = atof(value);
    } else if (strcmp(key, "disable_web_ssh") == 0) {
        config->disable_web_ssh = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    } else if (strcmp(key, "ignore_unsafe_cert") == 0) {
        config->ignore_unsafe_cert = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    } else if (strcmp(key, "custom_dns") == 0) {
        utils_set_string(config->custom_dns, sizeof(config->custom_dns), value);
    } else if (strcmp(key, "max_retries") == 0) {
        config->max_retries = atoi(value);
    } else if (strcmp(key, "reconnect_interval") == 0) {
        config->reconnect_interval = atoi(value);
    } else if (strcmp(key, "info_report_interval") == 0) {
        config->info_report_interval = atoi(value);
    } else if (strcmp(key, "month_rotate") == 0) {
        config->month_rotate = atoi(value);
    } else if (strcmp(key, "protocol_version") == 0) {
        config->protocol_version = atoi(value);
    } else if (strcmp(key, "enable_gpu") == 0) {
        config->enable_gpu = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    } else if (strcmp(key, "disable_compression") == 0) {
        config->disable_compression = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    } else if (strcmp(key, "disable_auto_update") == 0) {
        config->disable_auto_update = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    } else if (strcmp(key, "auto_discovery_key") == 0) {
        utils_set_string(config->auto_discovery_key, sizeof(config->auto_discovery_key), value);
    }

    return 0;
}

int config_load_from_uci(agent_config_t *config) {
    if (!config) return -1;

    FILE *fp;
    char line[512];

    fp = popen("uci show komari-agent-c 2>/dev/null", "r");
    if (!fp) return -1;

    while (fgets(line, sizeof(line), fp)) {
        config_parse_uci_line(config, line);
    }

    pclose(fp);
    return 0;
}

void config_print(const agent_config_t *config) {
    if (!config) return;

    printf("=== Komari Agent Configuration ===\n");
    printf("Token: %s\n", config->token[0] ? "***" : "(not set)");
    printf("Endpoint: %s\n", config->endpoint);
    printf("Interval: %.1f seconds\n", config->interval);
    printf("Custom DNS: %s\n", config->custom_dns[0] ? config->custom_dns : "(default)");
    printf("Disable Web SSH: %s\n", config->disable_web_ssh ? "yes" : "no");
    printf("Ignore Unsafe Cert: %s\n", config->ignore_unsafe_cert ? "yes" : "no");
    printf("Max Retries: %d\n", config->max_retries);
    printf("Reconnect Interval: %d seconds\n", config->reconnect_interval);
    printf("=================================\n");
}

int config_validate(agent_config_t *config) {
    if (!config) return -1;

    /* MIN-08: config_file path validation. When set it must be non-empty and
     * fit comfortably within the buffer (the loader already truncates to
     * sizeof(config_file), but a path that has been truncated is unusable
     * and would silently point to the wrong file). */
    if (config->config_file[0] != '\0') {
        size_t cfg_len = strlen(config->config_file);
        if (cfg_len >= MAX_CONFIG_FILE_LEN) {
            KOMARI_LOG_ERROR("Config validation: config_file path too long (%zu bytes), clearing",
                             cfg_len);
            config->config_file[0] = '\0';
        }
    }

    /* MIN-10: token must not be empty. We do not fabricate a token here
     * (there is no safe default), but the caller (main.c) already aborts
     * when token/endpoint are missing, so we only log the problem. */
    if (config->token[0] == '\0') {
        KOMARI_LOG_ERROR("Config validation: token is empty");
    }

    /* MIN-09: token UUID format validation. The Komari token is typically a
     * UUID; if the configured token matches the UUID shape but is malformed,
     * surface the problem so misconfigurations are easier to diagnose. We do
     * not clear the token because non-UUID tokens are still permitted. */
    if (config->token[0] != '\0' && strlen(config->token) == UUID_LEN &&
        !looks_like_uuid(config->token)) {
        KOMARI_LOG_ERROR("Config validation: token has UUID length (%d chars) "
                         "but malformed format", UUID_LEN);
    }

    /* T19: endpoint CR/LF injection prevention. A CR or LF in the endpoint
     * would allow an attacker to inject HTTP headers (e.g. Authorization or
     * Host) when the endpoint is used to build HTTP requests. Clear the
     * endpoint so subsequent URL-format validation can flag it as missing. */
    if (contains_crlf(config->endpoint)) {
        KOMARI_LOG_ERROR("Config validation: endpoint contains CR/LF characters, clearing");
        config->endpoint[0] = '\0';
    }

    /* MIN-11: endpoint URL format. Must start with http:// or https:// so
     * downstream code (ws_endpoint rewrite, idn conversion, etc.) can rely
     * on a known scheme. */
    if (config->endpoint[0] == '\0') {
        KOMARI_LOG_ERROR("Config validation: endpoint is empty");
    } else if (strncmp(config->endpoint, "http://", 7) != 0 &&
               strncmp(config->endpoint, "https://", 8) != 0) {
        KOMARI_LOG_ERROR("Config validation: endpoint must start with http:// or https://, clearing");
        config->endpoint[0] = '\0';
    }

    /* MIN-12: endpoint port range. Only validate when an explicit port is
     * present; default ports (80 for http, 443 for https) are always valid. */
    if (config->endpoint[0] != '\0') {
        int port = parse_endpoint_port(config->endpoint);
        if (port != -1 && (port < 1 || port > 65535)) {
            KOMARI_LOG_ERROR("Config validation: endpoint port %d out of range [1, 65535], clearing endpoint",
                             port);
            config->endpoint[0] = '\0';
        }
    }

    /* MAJ-22 / T12.3: interval must be strictly positive. A non-positive
     * interval would cause busy loops or divide-by-zero in callers. Fall
     * back to the default rather than aborting. */
    if (config->interval <= 0) {
        KOMARI_LOG_ERROR("Config validation: interval (%.2f) must be > 0, using default %.1f",
                         config->interval, CONFIG_DEFAULT_INTERVAL);
        config->interval = CONFIG_DEFAULT_INTERVAL;
    }

    /* Validate other numeric knobs that must be strictly positive to keep
     * the agent functional (reconnect/info-report loops). */
    if (config->max_retries <= 0) {
        KOMARI_LOG_ERROR("Config validation: max_retries (%d) must be > 0, using default %d",
                         config->max_retries, CONFIG_DEFAULT_MAX_RETRIES);
        config->max_retries = CONFIG_DEFAULT_MAX_RETRIES;
    }

    if (config->reconnect_interval <= 0) {
        KOMARI_LOG_ERROR("Config validation: reconnect_interval (%d) must be > 0, using default %d",
                         config->reconnect_interval, CONFIG_DEFAULT_RECONNECT_INTERVAL);
        config->reconnect_interval = CONFIG_DEFAULT_RECONNECT_INTERVAL;
    }

    if (config->info_report_interval <= 0) {
        KOMARI_LOG_ERROR("Config validation: info_report_interval (%d) must be > 0, using default %d",
                         config->info_report_interval, CONFIG_DEFAULT_INFO_REPORT_INTERVAL);
        config->info_report_interval = CONFIG_DEFAULT_INFO_REPORT_INTERVAL;
    }

    if (config->protocol_version != PROTOCOL_VERSION_V1 &&
        config->protocol_version != PROTOCOL_VERSION_V2) {
        KOMARI_LOG_ERROR("Config validation: protocol_version (%d) is not a known version, using default %d",
                         config->protocol_version, CONFIG_DEFAULT_PROTOCOL_VERSION);
        config->protocol_version = CONFIG_DEFAULT_PROTOCOL_VERSION;
    }

    return 0;
}
