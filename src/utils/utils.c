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
#include <sys/wait.h>
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

int utils_write_file_string(const char *path, const char *data, mode_t mode) {
    if (!path || !data) return -1;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
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
    int ret = gethostname(buf, buf_len);
    /* gethostname may not NUL-terminate when the buffer is too small */
    buf[buf_len - 1] = '\0';
    return ret;
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
    struct tm tm_info;
    if (!localtime_r(&t, &tm_info)) return -1;
    
    strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%S", &tm_info);
    return 0;
}

int utils_exec_command(const char *cmd, char *output, size_t output_len, int *exit_code) {
    /* WARNING: cmd is executed via the shell. Never pass untrusted input here. */
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

int utils_exec_command_argv(char *const argv[], char *output, size_t output_size, int *exit_code) {
    if (!argv || !argv[0]) return -1;

    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child: redirect stdout and stderr to the pipe write end */
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        execvp(argv[0], argv);
        /* execvp only returns on failure */
        _exit(127);
    }

    /* Parent: close write end and read child output */
    close(pipefd[1]);

    size_t total = 0;
    if (output && output_size > 0) {
        output[0] = '\0';
        while (total < output_size - 1) {
            ssize_t n = read(pipefd[0], output + total, output_size - 1 - total);
            if (n > 0) {
                total += (size_t)n;
            } else {
                break;
            }
        }
        output[total] = '\0';
    } else {
        char buf[1024];
        while (read(pipefd[0], buf, sizeof(buf)) > 0) {
            /* drain */
        }
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
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
    if (len == 0) return -1;
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) < 0) {
                if (errno != EEXIST) {
                    /* real error */
                    return -1;
                }
                /* EEXIST is ok, directory already exists */
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, 0755) < 0) {
        if (errno != EEXIST) {
            /* real error */
            return -1;
        }
        /* EEXIST is ok, directory already exists */
    }
    return 0;
}

int utils_file_exists(const char *path) {
    if (!path) return 0;
    struct stat st;
    return stat(path, &st) == 0;
}

char *utils_json_escape(const char *str) {
    if (!str) return NULL;

    size_t len = strlen(str);
    /* Worst case: each char becomes \u00XX (6 bytes) plus NUL */
    size_t escaped_len = len * 6 + 1;
    char *escaped = malloc(escaped_len);
    if (!escaped) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        switch (c) {
            case '"':
                escaped[j++] = '\\';
                escaped[j++] = '"';
                break;
            case '\\':
                escaped[j++] = '\\';
                escaped[j++] = '\\';
                break;
            case '\b':
                escaped[j++] = '\\';
                escaped[j++] = 'b';
                break;
            case '\f':
                escaped[j++] = '\\';
                escaped[j++] = 'f';
                break;
            case '\n':
                escaped[j++] = '\\';
                escaped[j++] = 'n';
                break;
            case '\r':
                escaped[j++] = '\\';
                escaped[j++] = 'r';
                break;
            case '\t':
                escaped[j++] = '\\';
                escaped[j++] = 't';
                break;
            default:
                if (c < 0x20) {
                    /* Other control characters: \u00XX per RFC 8259 */
                    int n = snprintf(escaped + j, escaped_len - j, "\\u%04X", c);
                    if (n > 0) j += (size_t)n;
                } else {
                    escaped[j++] = c;
                }
                break;
        }
    }
    escaped[j] = '\0';

    return escaped;
}

int utils_json_unescape(const char *json, char *out, size_t out_len) {
    if (!json || !out || out_len == 0) return -1;

    size_t len = strlen(json);
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
                case 'u':
                    /* Parse \uXXXX as a Unicode code point and encode as UTF-8.
                     * Surrogate pairs are not handled (simplified). */
                    if (i + 4 < len) {
                        char hex[5] = {0};
                        memcpy(hex, json + i + 1, 4);
                        unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
                        if (cp < 0x80) {
                            if (j < out_len - 1) out[j++] = (char)cp;
                        } else if (cp < 0x800) {
                            if (j + 1 < out_len - 1) {
                                out[j++] = (char)(0xC0 | (cp >> 6));
                                out[j++] = (char)(0x80 | (cp & 0x3F));
                            }
                        } else {
                            if (j + 2 < out_len - 1) {
                                out[j++] = (char)(0xE0 | (cp >> 12));
                                out[j++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                out[j++] = (char)(0x80 | (cp & 0x3F));
                            }
                        }
                        i += 4;
                    } else {
                        /* Malformed \u escape: output 'u' literally */
                        out[j++] = 'u';
                    }
                    break;
                default: out[j++] = json[i]; break;
            }
        } else {
            out[j++] = json[i];
        }
    }
    out[j] = '\0';

    return 0;
}
