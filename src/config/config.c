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

#include "config.h"
#include "utils.h"
#include "cJSON.h"

void config_init(agent_config_t *config) {
    if (!config) return;
    
    memset(config, 0, sizeof(agent_config_t));
    
    config->interval = 1.0;
    config->max_retries = 5;
    config->reconnect_interval = 5;
    config->info_report_interval = 30;
    config->month_rotate = 0;
    config->protocol_version = 2;   /* Default to v2 protocol */

    config->disable_auto_update = false;
    config->disable_web_ssh = false;
    config->ignore_unsafe_cert = false;
    config->memory_include_cache = false;
    config->memory_report_raw_used = false;
    config->enable_gpu = false;
    config->get_ip_addr_from_nic = false;
    config->show_warning = false;
    config->disable_compression = false;
    
    strncpy(config->language, "auto", sizeof(config->language) - 1);
}

static char *get_env_or_empty(const char *name, char *buf, size_t buf_len) {
    char *val = getenv(name);
    if (val) {
        strncpy(buf, val, buf_len - 1);
        buf[buf_len - 1] = '\0';
        return buf;
    }
    /* When environment variable is not set, keep existing value without override (supports configuration priority: environment variables only override set fields) */
    return buf;
}

static int get_env_bool(const char *name, bool default_val) {
    char *val = getenv(name);
    if (!val) return default_val;
    
    if (strcasecmp(val, "true") == 0 ||
        strcasecmp(val, "1") == 0 ||
        strcasecmp(val, "yes") == 0) {
        return true;
    }
    return false;
}

static double get_env_double(const char *name, double default_val) {
    char *val = getenv(name);
    if (!val) return default_val;
    return atof(val);
}

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
    get_env_or_empty("AGENT_LANGUAGE", config->language, sizeof(config->language));
    
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
    config->memory_report_raw_used = get_env_bool("AGENT_MEMORY_REPORT_RAW_USED", config->memory_report_raw_used);
    config->enable_gpu = get_env_bool("AGENT_ENABLE_GPU", config->enable_gpu);
    config->get_ip_addr_from_nic = get_env_bool("AGENT_GET_IP_ADDR_FROM_NIC", config->get_ip_addr_from_nic);
    config->show_warning = get_env_bool("AGENT_SHOW_WARNING", config->show_warning);
    config->disable_compression = get_env_bool("AGENT_DISABLE_COMPRESSION", config->disable_compression);
    
    return 0;
}



int config_load_from_file(agent_config_t *config, const char *path) {
    if (!config || !path) return -1;
    
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *json = malloc(size + 1);
    if (!json) {
        fclose(f);
        return -1;
    }
    
    size_t bytes_read = fread(json, 1, size, f);
    if (bytes_read != (size_t)size) {
        fprintf(stderr, "Warning: Config file read incomplete: %zu/%ld bytes\n", bytes_read, size);
    }
    json[size] = '\0';
    fclose(f);
    
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        fprintf(stderr, "Failed to parse JSON config from %s\n", path);
        free(json);
        return -1;
    }
    
    cJSON *item;
    
    if ((item = cJSON_GetObjectItem(root, "token")) && item->valuestring) {
        strncpy(config->token, item->valuestring, sizeof(config->token) - 1);
        config->token[sizeof(config->token) - 1] = '\0';
    }
    
    if ((item = cJSON_GetObjectItem(root, "endpoint")) && item->valuestring) {
        strncpy(config->endpoint, item->valuestring, sizeof(config->endpoint) - 1);
        config->endpoint[sizeof(config->endpoint) - 1] = '\0';
    }
    
    if ((item = cJSON_GetObjectItem(root, "custom_dns")) && item->valuestring) {
        strncpy(config->custom_dns, item->valuestring, sizeof(config->custom_dns) - 1);
        config->custom_dns[sizeof(config->custom_dns) - 1] = '\0';
    }
    
    if ((item = cJSON_GetObjectItem(root, "include_nics")) && item->valuestring) {
        strncpy(config->include_nics, item->valuestring, sizeof(config->include_nics) - 1);
        config->include_nics[sizeof(config->include_nics) - 1] = '\0';
    }
    
    if ((item = cJSON_GetObjectItem(root, "exclude_nics")) && item->valuestring) {
        strncpy(config->exclude_nics, item->valuestring, sizeof(config->exclude_nics) - 1);
        config->exclude_nics[sizeof(config->exclude_nics) - 1] = '\0';
    }
    
    if ((item = cJSON_GetObjectItem(root, "include_mountpoints")) && item->valuestring) {
        strncpy(config->include_mountpoints, item->valuestring, sizeof(config->include_mountpoints) - 1);
        config->include_mountpoints[sizeof(config->include_mountpoints) - 1] = '\0';
    }
    
    if ((item = cJSON_GetObjectItem(root, "cf_access_client_id")) && item->valuestring) {
        strncpy(config->cf_access_client_id, item->valuestring, sizeof(config->cf_access_client_id) - 1);
        config->cf_access_client_id[sizeof(config->cf_access_client_id) - 1] = '\0';
    }
    
    if ((item = cJSON_GetObjectItem(root, "cf_access_client_secret")) && item->valuestring) {
        strncpy(config->cf_access_client_secret, item->valuestring, sizeof(config->cf_access_client_secret) - 1);
        config->cf_access_client_secret[sizeof(config->cf_access_client_secret) - 1] = '\0';
    }
    
    if ((item = cJSON_GetObjectItem(root, "custom_ipv4")) && item->valuestring) {
        strncpy(config->custom_ipv4, item->valuestring, sizeof(config->custom_ipv4) - 1);
        config->custom_ipv4[sizeof(config->custom_ipv4) - 1] = '\0';
    }
    
    if ((item = cJSON_GetObjectItem(root, "custom_ipv6")) && item->valuestring) {
        strncpy(config->custom_ipv6, item->valuestring, sizeof(config->custom_ipv6) - 1);
        config->custom_ipv6[sizeof(config->custom_ipv6) - 1] = '\0';
    }
    
    if ((item = cJSON_GetObjectItem(root, "auto_discovery_key")) && item->valuestring) {
        strncpy(config->auto_discovery_key, item->valuestring, sizeof(config->auto_discovery_key) - 1);
        config->auto_discovery_key[sizeof(config->auto_discovery_key) - 1] = '\0';
    }

    if ((item = cJSON_GetObjectItem(root, "language")) && item->valuestring) {
        strncpy(config->language, item->valuestring, sizeof(config->language) - 1);
        config->language[sizeof(config->language) - 1] = '\0';
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
        config->disable_auto_update = (item->type == cJSON_True);
    }
    
    if ((item = cJSON_GetObjectItem(root, "disable_web_ssh"))) {
        config->disable_web_ssh = (item->type == cJSON_True);
    }
    
    if ((item = cJSON_GetObjectItem(root, "ignore_unsafe_cert"))) {
        config->ignore_unsafe_cert = (item->type == cJSON_True);
    }
    
    if ((item = cJSON_GetObjectItem(root, "memory_include_cache"))) {
        config->memory_include_cache = (item->type == cJSON_True);
    }
    
    if ((item = cJSON_GetObjectItem(root, "memory_report_raw_used"))) {
        config->memory_report_raw_used = (item->type == cJSON_True);
    }
    
    if ((item = cJSON_GetObjectItem(root, "enable_gpu"))) {
        config->enable_gpu = (item->type == cJSON_True);
    }
    
    if ((item = cJSON_GetObjectItem(root, "get_ip_addr_from_nic"))) {
        config->get_ip_addr_from_nic = (item->type == cJSON_True);
    }

    if ((item = cJSON_GetObjectItem(root, "disable_compression"))) {
        config->disable_compression = (item->type == cJSON_True);
    }
    
    cJSON_Delete(root);
    free(json);
    return 0;
}

int config_load_from_uci(agent_config_t *config) {
    if (!config) return -1;
    
    FILE *fp;
    char line[512];
    char key[64], value[256];
    
    fp = popen("uci show komari-agent-c 2>/dev/null", "r");
    if (!fp) return -1;
    
    while (fgets(line, sizeof(line), fp)) {
        char *dot = strchr(line, '.');
        if (!dot) continue;
        
        char *eq = strchr(dot, '=');
        if (!eq) continue;
        
        *eq = '\0';
        strncpy(key, dot + 1, sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';
        
        char *val = eq + 1;
        while (*val == ' ' || *val == '\'') val++;
        char *end = val + strlen(val) - 1;
        while (end > val && (*end == '\n' || *end == '\'' || *end == ' ')) {
            *end = '\0';
            end--;
        }
        
        strncpy(value, val, sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';
        
        if (strcmp(key, "token") == 0) {
            strncpy(config->token, value, sizeof(config->token) - 1);
        } else if (strcmp(key, "endpoint") == 0) {
            strncpy(config->endpoint, value, sizeof(config->endpoint) - 1);
        } else if (strcmp(key, "interval") == 0) {
            config->interval = atof(value);
        } else if (strcmp(key, "disable_web_ssh") == 0) {
            config->disable_web_ssh = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
        } else if (strcmp(key, "ignore_unsafe_cert") == 0) {
            config->ignore_unsafe_cert = (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
        } else if (strcmp(key, "custom_dns") == 0) {
            strncpy(config->custom_dns, value, sizeof(config->custom_dns) - 1);
        } else if (strcmp(key, "language") == 0) {
            strncpy(config->language, value, sizeof(config->language) - 1);
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
            strncpy(config->auto_discovery_key, value, sizeof(config->auto_discovery_key) - 1);
        }
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
    printf("Language: %s\n", config->language);
    printf("=================================\n");
}
