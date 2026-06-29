/*
 * Logging facility implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <syslog.h>
#include <pthread.h>

#include "logger.h"

static logger_config_t g_logger_config;
/* Protects g_logger_config (level, use_stdout, use_syslog, tag) against
 * concurrent reads/writes from multiple threads (MIN-54). Statically
 * initialized so logger_log() is safe to use before logger_init(). */
static pthread_mutex_t g_logger_mutex = PTHREAD_MUTEX_INITIALIZER;

void logger_init(const logger_config_t *config) {
    if (!config) return;

    pthread_mutex_lock(&g_logger_mutex);
    /* Release previously deep-copied tag to support re-initialization */
    free(g_logger_config.tag);

    memcpy(&g_logger_config, config, sizeof(logger_config_t));
    g_logger_config.tag = NULL;

    if (config->tag) {
        g_logger_config.tag = strdup(config->tag);
        /* If strdup fails, tag remains NULL and the default is used below */
    }

    if (g_logger_config.use_syslog) {
        openlog(g_logger_config.tag ? g_logger_config.tag : "komari-agent-c",
                LOG_PID | LOG_NDELAY, LOG_DAEMON);
    }
    pthread_mutex_unlock(&g_logger_mutex);
}

void logger_cleanup(void) {
    pthread_mutex_lock(&g_logger_mutex);
    if (g_logger_config.use_syslog) {
        closelog();
    }
    free(g_logger_config.tag);
    g_logger_config.tag = NULL;
    pthread_mutex_unlock(&g_logger_mutex);
}

/* Convert a log level enum to its uppercase string representation. */
static const char *level_to_string(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_FATAL: return "FATAL";
        default:              return "UNKNOWN";
    }
}

/* Map a log level to the corresponding syslog priority value. */
static int level_to_syslog_priority(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return LOG_DEBUG;
        case LOG_LEVEL_INFO:  return LOG_INFO;
        case LOG_LEVEL_WARN:  return LOG_WARNING;
        case LOG_LEVEL_ERROR: return LOG_ERR;
        case LOG_LEVEL_FATAL: return LOG_CRIT;
        default:              return LOG_INFO;
    }
}

/* Format the current wall-clock time as an ISO 8601 string with millisecond
 * precision (YYYY-MM-DDTHH:MM:SS.mmm). This matches the format used by
 * utils_format_timestamp() so logs and timestamps are consistent (MIN-07). */
static void format_log_timestamp(char *buf, size_t buf_len) {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        ts.tv_sec = time(NULL);
        ts.tv_nsec = 0;
    }

    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);
    size_t used = strftime(buf, buf_len, "%Y-%m-%dT%H:%M:%S", &tm_info);
    if (used == 0 || buf_len <= used) {
        /* Buffer too small for the base timestamp; ensure NUL termination */
        if (buf_len > 0) buf[buf_len - 1] = '\0';
        return;
    }

    /* Append milliseconds if there is room (".NNN" + NUL = 5 chars) */
    if (buf_len > used + 5) {
        snprintf(buf + used, buf_len - used, ".%03ld",
                 (long)(ts.tv_nsec / 1000000L));
    }
}

void logger_log(log_level_t level, const char *format, ...) {
    pthread_mutex_lock(&g_logger_mutex);

    if (level < g_logger_config.level) {
        pthread_mutex_unlock(&g_logger_mutex);
        return;
    }

    /* Snapshot the output flags. The mutex is held for the entire emit so
     * that concurrent loggers cannot interleave a single log line. */
    bool use_stdout = g_logger_config.use_stdout;
    bool use_syslog = g_logger_config.use_syslog;

    va_list args;
    va_start(args, format);

    if (use_stdout) {
        char time_buf[32];
        format_log_timestamp(time_buf, sizeof(time_buf));

        /* Format the user message into a temporary buffer.
         * Use va_copy so args remains valid for the syslog path below. */
        va_list args_stdout;
        va_copy(args_stdout, args);
        char body[768];
        int bn = vsnprintf(body, sizeof(body), format, args_stdout);
        va_end(args_stdout);
        if (bn < 0) bn = 0;
        if ((size_t)bn >= sizeof(body)) bn = (int)sizeof(body) - 1;

        /* Single fwrite to avoid interleaved output across threads */
        char output[1024];
        int n = snprintf(output, sizeof(output), "[%s] [%s] %s\n",
                         time_buf, level_to_string(level), body);
        if (n < 0) n = 0;
        if ((size_t)n >= sizeof(output)) n = (int)sizeof(output) - 1;
        fwrite(output, 1, (size_t)n, stdout);
        fflush(stdout);
    }

    if (use_syslog) {
        va_list args_syslog;
        va_copy(args_syslog, args);
        vsyslog(level_to_syslog_priority(level), format, args_syslog);
        va_end(args_syslog);
    }

    va_end(args);
    pthread_mutex_unlock(&g_logger_mutex);
}
