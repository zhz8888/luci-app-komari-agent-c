/*
 * Logging facility interface declarations.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_LOGGER_H
#define KOMARI_AGENT_C_LOGGER_H

#include <stdio.h>
#include <syslog.h>
#include <stdbool.h>

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL
} log_level_t;

typedef struct {
    log_level_t level;
    bool use_syslog;
    bool use_stdout;
    char *tag;
} logger_config_t;

/**
 * Initialize the logger with the given configuration.
 *
 * @param config Logger configuration (level, outputs, tag)
 */
void logger_init(logger_config_t *config);

/**
 * Release logger resources, closing syslog if enabled.
 */
void logger_cleanup(void);

/**
 * Log a message at the given level if it passes the configured threshold.
 *
 * @param level Log level for this message
 * @param format printf-style format string
 * @param ... Format arguments
 */
void logger_log(log_level_t level, const char *format, ...);

#define KOMARI_LOG_DEBUG(fmt, ...) logger_log(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define KOMARI_LOG_INFO(fmt, ...) logger_log(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define KOMARI_LOG_WARN(fmt, ...) logger_log(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define KOMARI_LOG_ERROR(fmt, ...) logger_log(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define KOMARI_LOG_FATAL(fmt, ...) logger_log(LOG_LEVEL_FATAL, fmt, ##__VA_ARGS__)

#endif
