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

#include "logger.h"

static logger_config_t g_logger_config;

void logger_init(logger_config_t *config) {
    if (!config) return;
    
    memcpy(&g_logger_config, config, sizeof(logger_config_t));
    
    if (g_logger_config.use_syslog) {
        openlog(g_logger_config.tag ? g_logger_config.tag : "komari-agent-c",
                LOG_PID | LOG_NDELAY, LOG_DAEMON);
    }
}

void logger_cleanup(void) {
    if (g_logger_config.use_syslog) {
        closelog();
    }
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
        case LOG_LEVEL_DEBUG: return 7;  /* LOG_DEBUG */
        case LOG_LEVEL_INFO:  return 6;  /* LOG_INFO */
        case LOG_LEVEL_WARN:  return 4;  /* LOG_WARNING */
        case LOG_LEVEL_ERROR: return 3;  /* LOG_ERR */
        case LOG_LEVEL_FATAL: return 2;  /* LOG_CRIT */
        default:              return 6;  /* LOG_INFO */
    }
}

void logger_log(log_level_t level, const char *format, ...) {
    if (level < g_logger_config.level) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    if (g_logger_config.use_stdout) {
        char time_buf[64];
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
        
        fprintf(stdout, "[%s] [%s] ", time_buf, level_to_string(level));
        vfprintf(stdout, format, args);
        fprintf(stdout, "\n");
        fflush(stdout);
    }
    
    if (g_logger_config.use_syslog) {
        va_list args_syslog;
        va_copy(args_syslog, args);
        vsyslog(level_to_syslog_priority(level), format, args_syslog);
        va_end(args_syslog);
    }
    
    va_end(args);
}
