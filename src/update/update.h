/*
 * Package update checking interface declarations.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_UPDATE_H
#define KOMARI_AGENT_C_UPDATE_H

#include <stddef.h>

/* Package version info structure */
typedef struct {
    char name[64];     /* Package name */
    char version[32];  /* Version number */
} package_version_t;

/**
 * Check the installed komari-agent-c package version via opkg
 *
 * @param current Outputs the current package version info
 * @return 0 on success, -1 on failure
 */
int update_check_opkg(package_version_t *current);

/**
 * Check the installed komari-agent-c package version via apk
 *
 * @param current Outputs the current package version info
 * @return 0 on success, -1 on failure
 */
int update_check_apk(package_version_t *current);

/**
 * Auto-detect the system's package manager and check the komari-agent-c version
 *
 * @param current Outputs the current package version info
 * @return 0 on success, -1 on failure
 */
int update_check_package(package_version_t *current);

/**
 * Compare two semantic version numbers
 *
 * @param v1 Version number 1 (e.g. "1.2.3")
 * @param v2 Version number 2 (e.g. "1.2.4")
 * @return -1 if v1 < v2, 0 if equal, 1 if v1 > v2
 */
int update_compare_versions(const char *v1, const char *v2);

/**
 * Get the currently installed komari-agent-c version
 *
 * @param current Outputs the version info
 * @return 0 on success, -1 on failure
 */
int update_get_current_version(package_version_t *current);

/**
 * Check whether an update is available
 *
 * @param current_version Current version string
 * @return 1 if an update is available, 0 if no update, -1 on check failure
 */
int update_check_available(const char *current_version);

/**
 * Background worker function that periodically checks for updates (for thread invocation)
 *
 * @param arg Thread argument (unused)
 * @return NULL
 */
void *update_do_check_works(void *arg);

/**
 * Signal the background update checker thread to stop.
 *
 * Should be called from the main shutdown path so that the worker
 * exits its loop promptly (within ~1 second) instead of waiting for
 * the next 6-hour sleep cycle to complete.
 */
void update_stop(void);

#endif
