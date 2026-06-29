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
#include "logger.h"

/* Maximum fd value to scan when closing inherited descriptors, used as a
 * fallback when sysconf(_SC_OPEN_MAX) is unavailable. */
#define UTILS_FD_CLOSE_FALLBACK 1024

/* Close all open file descriptors starting from lowfd upward.
 *
 * After fork() the child inherits a copy of the parent's file descriptor
 * table. Any descriptor other than stdin/stdout/stderr must be closed before
 * execvp() to avoid leaking fds (e.g. pipes, sockets, log files) into the
 * spawned process. */
static void utils_close_inherited_fds(const int lowfd) {
#ifdef F_CLOSEM
    /* Non-standard but efficient: atomically close all fds >= lowfd.
     * Available on AIX, HP-UX and some BSDs. */
    if (fcntl(lowfd, F_CLOSEM, 0) == 0) return;
#endif
    long max_fd = sysconf(_SC_OPEN_MAX);
    if (max_fd < 0) max_fd = UTILS_FD_CLOSE_FALLBACK;
    for (int fd = lowfd; fd < max_fd; fd++) {
        close(fd);
    }
}

/* Common fork + execvp + capture implementation shared by
 * utils_exec_command() and utils_exec_command_argv().
 *
 * Redirects the child's stdout and stderr into a pipe, reads the output in
 * the parent, and reports the child exit status. Returns 0 on success (the
 * command may still have failed; check *exit_code), -1 on pipe/fork failure. */
static int utils_exec_capture(char *const argv[], char *output, size_t output_size, int *exit_code) {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        KOMARI_LOG_WARN("exec_capture: pipe() failed: %s", strerror(errno));
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        int saved_errno = errno;
        close(pipefd[0]);
        close(pipefd[1]);
        KOMARI_LOG_WARN("exec_capture: fork() failed: %s", strerror(saved_errno));
        return -1;
    }

    if (pid == 0) {
        /* Child: redirect stdout and stderr to the pipe write end */
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        if (dup2(pipefd[1], STDERR_FILENO) < 0) {
            _exit(127);
        }
        /* Close the original write end unless it aliases a std fd */
        if (pipefd[1] != STDOUT_FILENO && pipefd[1] != STDERR_FILENO) {
            close(pipefd[1]);
        }
        /* Close every other inherited descriptor to prevent fd leaks */
        utils_close_inherited_fds(STDERR_FILENO + 1);

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
            ssize_t n;
            /* Retry on EINTR (MIN-61) */
            do {
                n = read(pipefd[0], output + total, output_size - 1 - total);
            } while (n < 0 && errno == EINTR);
            if (n > 0) {
                total += (size_t)n;
            } else {
                break;
            }
        }
        output[total] = '\0';
    } else {
        char buf[1024];
        for (;;) {
            ssize_t n;
            do {
                n = read(pipefd[0], buf, sizeof(buf));
            } while (n < 0 && errno == EINTR);
            if (n <= 0) break;
        }
    }
    close(pipefd[0]);

    /* Reap the child, retrying on EINTR */
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        break;
    }
    if (exit_code) {
        if (WIFEXITED(status)) {
            *exit_code = WEXITSTATUS(status);
        } else {
            *exit_code = -1;
        }
    }

    return 0;
}

int utils_read_file_string(const char *path, char *buf, size_t buf_len) {
    if (!path || !buf || buf_len == 0) return -1;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        KOMARI_LOG_DEBUG("read_file_string: open(%s) failed: %s",
                         path, strerror(errno));
        return -1;
    }

    ssize_t n = read(fd, buf, buf_len - 1);
    int saved_errno = errno;
    close(fd);

    if (n < 0) {
        KOMARI_LOG_DEBUG("read_file_string: read(%s) failed: %s",
                         path, strerror(saved_errno));
        return -1;
    }

    buf[n] = '\0';
    return 0;
}

int utils_read_file_line(const char *path, char *buf, size_t buf_len) {
    if (!path || !buf || buf_len == 0) return -1;

    FILE *fp = fopen(path, "r");
    if (!fp) {
        KOMARI_LOG_DEBUG("read_file_line: fopen(%s) failed: %s",
                         path, strerror(errno));
        return -1;
    }

    if (fgets(buf, buf_len, fp) == NULL) {
        int saved_errno = errno;
        fclose(fp);
        KOMARI_LOG_DEBUG("read_file_line: fgets(%s) failed: %s",
                         path, strerror(saved_errno));
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
    if (fd < 0) {
        KOMARI_LOG_WARN("write_file_string: open(%s) failed: %s",
                        path, strerror(errno));
        return -1;
    }

    size_t len = strlen(data);
    ssize_t n = write(fd, data, len);
    int saved_errno = errno;
    close(fd);

    if (n != (ssize_t)len) {
        KOMARI_LOG_WARN("write_file_string: write(%s) incomplete: %s",
                        path, strerror(saved_errno));
        return -1;
    }

    return 0;
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
    /* WARNING: cmd is executed via "sh -c" and is therefore subject to shell
     * interpretation. Never pass untrusted input here due to command injection
     * risk. Use utils_exec_command_argv() instead for commands built from
     * untrusted data.
     *
     * Implemented with fork() + execvp() rather than popen() so that the
     * child properly closes inherited file descriptors (MIN-02). The const
     * cast on cmd is safe because execvp/sh treat the argument as read-only. */
    if (!cmd) return -1;

    KOMARI_LOG_DEBUG("exec_command: running '%s'", cmd);

    char *argv[] = {"sh", "-c", (char *)cmd, NULL};
    return utils_exec_capture(argv, output, output_len, exit_code);
}

int utils_exec_command_argv(char *const argv[], char *output, size_t output_size, int *exit_code) {
    if (!argv || !argv[0]) return -1;

    KOMARI_LOG_DEBUG("exec_command_argv: running '%s'", argv[0]);

    return utils_exec_capture(argv, output, output_size, exit_code);
}

int utils_mkdir_p(const char *path) {
    if (!path) return -1;

    KOMARI_LOG_DEBUG("mkdir_p: creating directory tree %s", path);

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
                    KOMARI_LOG_WARN("mkdir_p: mkdir(%s) failed: %s",
                                    tmp, strerror(errno));
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
            KOMARI_LOG_WARN("mkdir_p: mkdir(%s) failed: %s",
                            tmp, strerror(errno));
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
                    /* Other control characters: \u00XX per RFC 8259.
                     * The escape sequence is exactly 6 chars; require room
                     * for it plus the NUL terminator. */
                    if (escaped_len - j < 7) {
                        /* Buffer too small: truncation would occur. Stop
                         * encoding to avoid writing a partial escape. */
                        KOMARI_LOG_WARN("json_escape: buffer truncated at offset %zu", j);
                        goto escape_done;
                    }
                    int n = snprintf(escaped + j, escaped_len - j, "\\u%04X", c);
                    if (n < 0 || (size_t)n >= escaped_len - j) {
                        /* snprintf error or truncation (MAJ-11) */
                        KOMARI_LOG_WARN("json_escape: snprintf failed/truncated for byte 0x%02X", c);
                        goto escape_done;
                    }
                    j += (size_t)n;
                } else {
                    escaped[j++] = c;
                }
                break;
        }
    }
escape_done:
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
                case 'b': out[j++] = '\b'; break;
                case 'f': out[j++] = '\f'; break;
                case 'n': out[j++] = '\n'; break;
                case 'r': out[j++] = '\r'; break;
                case 't': out[j++] = '\t'; break;
                case 'u':
                    /* Parse \uXXXX as a Unicode code point and encode as UTF-8.
                     * Surrogate pairs are not handled (simplified). */
                    if (i + 4 < len) {
                        char hex[5] = {0};
                        memcpy(hex, json + i + 1, sizeof(hex) - 1);
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
