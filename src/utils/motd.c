/*
 * MOTD (Message of the Day) implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "motd.h"
#include "utils.h"
#include "logger.h"

#define MOTD_SCRIPT_PATH "/etc/update-motd.d"
#define MOTD_FILE_PATH "/etc/motd"
#define MOTD_MAX_SCRIPTS 64
#define MOTD_MAX_OUTPUT_LEN 8192

/* Comparison function used to sort by file name */
static int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

/* Execute all executable scripts under /etc/update-motd.d/ */
int motd_execute_scripts(char *output, size_t output_size) {
    if (!output || output_size == 0) return -1;
    
    output[0] = '\0';
    
    DIR *dir = opendir(MOTD_SCRIPT_PATH);
    if (!dir) {
        KOMARI_LOG_DEBUG("MOTD scripts directory %s not found", MOTD_SCRIPT_PATH);
        return 0;
    }
    
    /* Collect all script file names */
    char *scripts[MOTD_MAX_SCRIPTS];
    int script_count = 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && script_count < MOTD_MAX_SCRIPTS) {
        if (entry->d_name[0] == '.') continue;
        
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", MOTD_SCRIPT_PATH, entry->d_name);
        
        struct stat st;
        if (stat(path, &st) == 0 && (st.st_mode & S_IXUSR)) {
            scripts[script_count] = strdup(entry->d_name);
            if (scripts[script_count]) {
                script_count++;
            }
        }
    }
    closedir(dir);
    
    if (script_count == 0) {
        return 0;
    }
    
    /* Sort by file name */
    qsort(scripts, script_count, sizeof(char *), compare_strings);

    /* Execute each script */
    size_t total_len = 0;
    for (int i = 0; i < script_count; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", MOTD_SCRIPT_PATH, scripts[i]);
        
        char script_output[4096];
        int exit_code = 0;
        
        if (utils_exec_command(path, script_output, sizeof(script_output), &exit_code) == 0) {
            if (exit_code == 0 && script_output[0] != '\0') {
                size_t len = strlen(script_output);
                if (total_len + len < output_size - 1) {
                    memcpy(output + total_len, script_output, len);
                    total_len += len;
                    output[total_len] = '\0';
                }
            }
        }
        
        free(scripts[i]);
    }
    
    return 0;
}

/* Read the contents of the /etc/motd file */
int motd_read_file(char *output, size_t output_size) {
    if (!output || output_size == 0) return -1;
    
    output[0] = '\0';
    
    if (!utils_file_exists(MOTD_FILE_PATH)) {
        KOMARI_LOG_DEBUG("MOTD file %s not found", MOTD_FILE_PATH);
        return -2;
    }
    
    return utils_read_file_string(MOTD_FILE_PATH, output, output_size);
}

/* Get the full MOTD content */
int motd_get_content(char *output, size_t output_size) {
    if (!output || output_size == 0) return -1;

    output[0] = '\0';
    size_t total_len = 0;

    /* 1. Execute scripts under /etc/update-motd.d/ */
    char scripts_output[MOTD_MAX_OUTPUT_LEN];
    if (motd_execute_scripts(scripts_output, sizeof(scripts_output)) == 0) {
        size_t len = strlen(scripts_output);
        if (len > 0 && total_len + len < output_size - 1) {
            memcpy(output + total_len, scripts_output, len);
            total_len += len;
            output[total_len] = '\0';
        }
    }

    /* 2. Read the /etc/motd file */
    char file_output[MOTD_MAX_OUTPUT_LEN];
    if (motd_read_file(file_output, sizeof(file_output)) == 0) {
        size_t len = strlen(file_output);
        if (len > 0 && total_len + len < output_size - 1) {
            /* If there is existing content that does not end with a newline, add one */
            if (total_len > 0 && output[total_len - 1] != '\n') {
                output[total_len++] = '\n';
            }
            memcpy(output + total_len, file_output, len);
            total_len += len;
            output[total_len] = '\0';
        }
    }

    return (total_len > 0) ? 0 : -2;
}
