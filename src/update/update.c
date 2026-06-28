/*
 * Package update checking implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

#include "update.h"
#include "utils.h"
#include "logger.h"

/* komari-agent-c package name */
#define KOMARI_PACKAGE_NAME "komari-agent-c"

/* State directories for opkg and apk */
#define OPKG_STATE_DIR "/usr/lib/opkg"
#define APK_STATE_DIR  "/usr/lib/apk"

/* Background check interval: 6 hours = 21600 seconds */
#define UPDATE_CHECK_INTERVAL_SECONDS 21600

/* Command output buffer size */
#define CMD_OUTPUT_BUF_SIZE 4096

/**
 * Check whether the specified directory exists
 *
 * @param path Directory path
 * @return 1 if exists, 0 if not
 */
static int dir_exists(const char *path) {
    return utils_file_exists(path);
}

/**
 * Parse the version number from a string (strip the package name prefix and separators)
 *
 * Examples:
 *   "komari-agent-c - 1.0.0-1"  -> "1.0.0-1"
 *   "komari-agent-1.0.0-1"     -> "1.0.0-1"
 *   "  1.2.3  "                -> "1.2.3"
 *
 * @param line    Input line
 * @param version Output version number
 * @param len     Version buffer length
 * @return 0 on success, -1 on failure
 */
static int parse_version_from_line(const char *line,
                                    char *version,
                                    size_t len) {
    if (!line || !version || len == 0) return -1;

    version[0] = '\0';

    /* Skip leading whitespace */
    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p == '\0') return -1;

    /* Skip the package name prefix (if the line starts with "komari-agent-c") */
    if (strncmp(p, KOMARI_PACKAGE_NAME, strlen(KOMARI_PACKAGE_NAME)) == 0) {
        p += strlen(KOMARI_PACKAGE_NAME);
        /* Skip separators: space, '-', '=', etc. */
        while (*p && (*p == ' ' || *p == '-' || *p == '=' || *p == ':')) {
            p++;
        }
    }

    if (*p == '\0') return -1;

    /* Copy the version number until end of line or whitespace */
    size_t i = 0;
    while (*p && !isspace((unsigned char)*p) && i < len - 1) {
        version[i++] = *p++;
    }
    version[i] = '\0';

    return (i > 0) ? 0 : -1;
}

int update_check_opkg(package_version_t *current) {
    if (!current) return -1;

    memset(current, 0, sizeof(*current));

    char output[CMD_OUTPUT_BUF_SIZE];
    int exit_code = 0;

    /* Execute opkg list-installed komari-agent-c */
    if (utils_exec_command("opkg list-installed " KOMARI_PACKAGE_NAME,
                            output, sizeof(output), &exit_code) != 0) {
        KOMARI_LOG_ERROR("Update check: failed to execute opkg list-installed");
        return -1;
    }

    if (exit_code != 0) {
        KOMARI_LOG_WARN("Update check: opkg list-installed returned non-zero exit code exit_code=%d", exit_code);
        return -1;
    }

    /* Parse the first line of output */
    char *line = output;
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';

    char version[32];
    if (parse_version_from_line(line, version, sizeof(version)) != 0) {
        KOMARI_LOG_WARN("Update check: failed to parse opkg output output=%s", output);
        return -1;
    }

    strncpy(current->name, KOMARI_PACKAGE_NAME, sizeof(current->name) - 1);
    current->name[sizeof(current->name) - 1] = '\0';

    strncpy(current->version, version, sizeof(current->version) - 1);
    current->version[sizeof(current->version) - 1] = '\0';

    KOMARI_LOG_DEBUG("Update check: opkg detected version %s-%s", current->name, current->version);
    return 0;
}

int update_check_apk(package_version_t *current) {
    if (!current) return -1;

    memset(current, 0, sizeof(*current));

    char output[CMD_OUTPUT_BUF_SIZE];
    int exit_code = 0;

    /* Execute apk info komari-agent-c */
    if (utils_exec_command("apk info " KOMARI_PACKAGE_NAME,
                            output, sizeof(output), &exit_code) != 0) {
        KOMARI_LOG_ERROR("Update check: failed to execute apk info");
        return -1;
    }

    if (exit_code != 0) {
        KOMARI_LOG_WARN("Update check: apk info returned non-zero exit code exit_code=%d", exit_code);
        return -1;
    }

    /* apk info output may contain multiple lines; find the line containing the package name */
    char *line = output;
    char *nl = NULL;
    char version[32] = "";

    while (line && *line) {
        nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (strstr(line, KOMARI_PACKAGE_NAME) != NULL) {
            if (parse_version_from_line(line, version, sizeof(version)) == 0) {
                break;
            }
        }
        version[0] = '\0';

        if (nl) {
            line = nl + 1;
        } else {
            line = NULL;
        }
    }

    if (version[0] == '\0') {
        KOMARI_LOG_WARN("Update check: failed to parse apk output output=%s", output);
        return -1;
    }

    strncpy(current->name, KOMARI_PACKAGE_NAME, sizeof(current->name) - 1);
    current->name[sizeof(current->name) - 1] = '\0';

    strncpy(current->version, version, sizeof(current->version) - 1);
    current->version[sizeof(current->version) - 1] = '\0';

    KOMARI_LOG_DEBUG("Update check: apk detected version %s-%s", current->name, current->version);
    return 0;
}

int update_check_package(package_version_t *current) {
    if (!current) return -1;

    /* Detect apk first (OpenWrt 24.10+ default), then opkg */
    if (dir_exists(APK_STATE_DIR)) {
        return update_check_apk(current);
    }

    if (dir_exists(OPKG_STATE_DIR)) {
        return update_check_opkg(current);
    }

    KOMARI_LOG_WARN("Update check: neither opkg nor apk package manager detected");
    return -1;
}

int update_compare_versions(const char *v1, const char *v2) {
    if (!v1 || !v2) return 0;

    /* Skip a possible 'v' prefix */
    if (*v1 == 'v') v1++;
    if (*v2 == 'v') v2++;

    while (*v1 && *v2) {
        /* Extract numeric segments for comparison */
        if (isdigit((unsigned char)*v1) && isdigit((unsigned char)*v2)) {
            long n1 = strtol(v1, (char**)&v1, 10);
            long n2 = strtol(v2, (char**)&v2, 10);
            if (n1 < n2) return -1;
            if (n1 > n2) return 1;
        } else {
            /* Compare non-digit characters directly by ASCII value */
            if (*v1 < *v2) return -1;
            if (*v1 > *v2) return 1;
            v1++;
            v2++;
        }
    }

    /* Handle remaining characters: the longer version is considered greater */
    if (*v1 && !*v2) {
        /* v1 has remaining characters; check whether it is a separator (treat as an extension of the equal prefix) */
        if (*v1 == '.' || *v1 == '-') {
            /* Check whether there are more digits afterwards */
            const char *p = v1 + 1;
            while (*p && !isdigit((unsigned char)*p)) p++;
            if (*p) return 1;
        }
        return 1;
    }

    if (!*v1 && *v2) {
        if (*v2 == '.' || *v2 == '-') {
            const char *p = v2 + 1;
            while (*p && !isdigit((unsigned char)*p)) p++;
            if (*p) return -1;
        }
        return -1;
    }

    return 0;
}

int update_get_current_version(package_version_t *current) {
    return update_check_package(current);
}

/**
 * Check whether opkg has a komari-agent-c upgrade available
 *
 * @return 1 if an update is available, 0 if no update, -1 on check failure
 */
static int check_opkg_upgradable(void) {
    char output[CMD_OUTPUT_BUF_SIZE];
    int exit_code = 0;

    if (utils_exec_command("opkg list-upgradable",
                            output, sizeof(output), &exit_code) != 0) {
        KOMARI_LOG_ERROR("Update check: failed to execute opkg list-upgradable");
        return -1;
    }

    if (exit_code != 0) {
        KOMARI_LOG_WARN("Update check: opkg list-upgradable returned non-zero exit code exit_code=%d", exit_code);
        return -1;
    }

    /* Look for the komari-agent-c line in the output */
    if (strstr(output, KOMARI_PACKAGE_NAME) != NULL) {
        return 1;
    }

    return 0;
}

/**
 * Check whether apk has a komari-agent-c upgrade available
 *
 * @return 1 if an update is available, 0 if no update, -1 on check failure
 */
static int check_apk_upgradable(void) {
    char output[CMD_OUTPUT_BUF_SIZE];
    int exit_code = 0;

    if (utils_exec_command("apk list --upgradable",
                            output, sizeof(output), &exit_code) != 0) {
        KOMARI_LOG_ERROR("Update check: failed to execute apk list --upgradable");
        return -1;
    }

    /* apk list may return a non-zero exit code when there are no updates, but it is enough if the output contains the package name */
    if (strstr(output, KOMARI_PACKAGE_NAME) != NULL) {
        return 1;
    }

    return 0;
}

int update_check_available(const char *current_version) {
    (void)current_version;  /* The current version is for log reference only and is not used in the check */

    int ret = -1;
    const char *manager = NULL;

    if (dir_exists(APK_STATE_DIR)) {
        manager = "apk";
        ret = check_apk_upgradable();
    } else if (dir_exists(OPKG_STATE_DIR)) {
        manager = "opkg";
        ret = check_opkg_upgradable();
    } else {
        KOMARI_LOG_WARN("Update check: neither opkg nor apk package manager detected");
        return -1;
    }

    if (ret == 1) {
        KOMARI_LOG_INFO("Update check: a new version of komari-agent-c is available, please upgrade via %s upgrade komari-agent-c",
                        manager);
    } else if (ret == 0) {
        KOMARI_LOG_DEBUG("Update check: already on the latest version");
    }

    return ret;
}

void *update_do_check_works(void *arg) {
    (void)arg;  /* Unused */

    KOMARI_LOG_INFO("Update check: background check thread started, interval %d seconds", UPDATE_CHECK_INTERVAL_SECONDS);

    while (1) {
        /* Run a check immediately */
        if (update_check_available(NULL) < 0) {
            KOMARI_LOG_DEBUG("Update check: this check failed, will retry in the next cycle");
        }

        /* Sleep for 6 hours before checking again */
        sleep(UPDATE_CHECK_INTERVAL_SECONDS);
    }

    return NULL;
}
