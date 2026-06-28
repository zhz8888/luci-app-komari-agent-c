/*
 * Common utility functions implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "utils.h"

int utils_read_file_string(const char *path, char *buf, size_t buf_len) {
    if (!path || !buf || buf_len == 0) return -1;
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    
    ssize_t n = read(fd, buf, buf_len - 1);
    close(fd);
    
    if (n < 0) return -1;
    
    buf[n] = '\0';
    return 0;
}

int utils_read_file_line(const char *path, char *buf, size_t buf_len) {
    if (!path || !buf || buf_len == 0) return -1;
    
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    
    if (fgets(buf, buf_len, fp) == NULL) {
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    
    return 0;
}

int utils_write_file_string(const char *path, const char *data) {
    if (!path || !data) return -1;
    
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    
    size_t len = strlen(data);
    ssize_t n = write(fd, data, len);
    close(fd);
    
    return (n == (ssize_t)len) ? 0 : -1;
}

uint64_t utils_get_uptime_seconds(void) {
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) return 0;
    
    double uptime;
    if (fscanf(fp, "%lf", &uptime) == 1) {
        fclose(fp);
        return (uint64_t)uptime;
    }
    fclose(fp);
    return 0;
}

int utils_get_hostname(char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) return -1;
    return gethostname(buf, buf_len);
}

char *utils_str_trim(char *str) {
    if (!str) return NULL;
    
    while (*str && (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r')) {
        str++;
    }
    
    if (*str == '\0') return str;
    
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    *(end + 1) = '\0';
    
    return str;
}

char *utils_str_duplicate(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char *dup = malloc(len);
    if (!dup) return NULL;
    memcpy(dup, str, len);
    return dup;
}

int utils_str_starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return 0;
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

int utils_str_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > str_len) return 0;
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

uint64_t utils_get_current_timestamp(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return (uint64_t)ts.tv_sec;
    }
    return (uint64_t)time(NULL);
}

int utils_format_timestamp(uint64_t timestamp, char *buf, size_t buf_len) {
    if (!buf || buf_len == 0) return -1;
    
    time_t t = (time_t)timestamp;
    struct tm *tm_info = localtime(&t);
    if (!tm_info) return -1;
    
    strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%S", tm_info);
    return 0;
}

int utils_exec_command(const char *cmd, char *output, size_t output_len, int *exit_code) {
    if (!cmd) return -1;
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    
    size_t total = 0;
    if (output && output_len > 0) {
        output[0] = '\0';
        while (fgets(output + total, output_len - total, fp) != NULL) {
            total = strlen(output);
            if (total >= output_len - 1) break;
        }
    } else {
        char buf[1024];
        while (fgets(buf, sizeof(buf), fp) != NULL);
    }
    
    int status = pclose(fp);
    if (exit_code) {
        if (WIFEXITED(status)) {
            *exit_code = WEXITSTATUS(status);
        } else {
            *exit_code = -1;
        }
    }
    
    return 0;
}

int utils_mkdir_p(const char *path) {
    if (!path) return -1;
    
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    
    size_t len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    
    return mkdir(tmp, 0755);
}

int utils_file_exists(const char *path) {
    if (!path) return 0;
    struct stat st;
    return stat(path, &st) == 0;
}

char *utils_json_escape(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    size_t escaped_len = len * 2 + 1;
    char *escaped = malloc(escaped_len);
    if (!escaped) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len && j < escaped_len - 1; i++) {
        switch (str[i]) {
            case '"':
                if (j + 2 < escaped_len) {
                    escaped[j++] = '\\';
                    escaped[j++] = '"';
                }
                break;
            case '\\':
                if (j + 2 < escaped_len) {
                    escaped[j++] = '\\';
                    escaped[j++] = '\\';
                }
                break;
            case '\n':
                if (j + 2 < escaped_len) {
                    escaped[j++] = '\\';
                    escaped[j++] = 'n';
                }
                break;
            case '\r':
                if (j + 2 < escaped_len) {
                    escaped[j++] = '\\';
                    escaped[j++] = 'r';
                }
                break;
            case '\t':
                if (j + 2 < escaped_len) {
                    escaped[j++] = '\\';
                    escaped[j++] = 't';
                }
                break;
            default:
                if ((unsigned char)str[i] >= 0x20) {
                    escaped[j++] = str[i];
                }
                break;
        }
    }
    escaped[j] = '\0';
    
    return escaped;
}

int utils_json_unescape(const char *json, char *out, size_t out_len) {
    if (!json || !out || out_len == 0) return -1;
    
    size_t j = 0;
    for (size_t i = 0; json[i] && j < out_len - 1; i++) {
        if (json[i] == '\\' && json[i + 1]) {
            i++;
            switch (json[i]) {
                case '"': out[j++] = '"'; break;
                case '\\': out[j++] = '\\'; break;
                case 'n': out[j++] = '\n'; break;
                case 'r': out[j++] = '\r'; break;
                case 't': out[j++] = '\t'; break;
                default: out[j++] = json[i]; break;
            }
        } else {
            out[j++] = json[i];
        }
    }
    out[j] = '\0';
    
    return 0;
}
