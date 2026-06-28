/*
 * Common utility functions interface declarations.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_UTILS_H
#define KOMARI_AGENT_C_UTILS_H

#include <stdint.h>
#include <stddef.h>

/**
 * Read the entire contents of a file as a NUL-terminated string.
 *
 * @param path File path
 * @param buf Output buffer
 * @param buf_len Size of buf
 * @return 0 on success, -1 on failure
 */
int utils_read_file_string(const char *path, char *buf, size_t buf_len);

/**
 * Read the first line of a file as a NUL-terminated string.
 *
 * @param path File path
 * @param buf Output buffer
 * @param buf_len Size of buf
 * @return 0 on success, -1 on failure
 */
int utils_read_file_line(const char *path, char *buf, size_t buf_len);

/**
 * Write a string to a file, truncating any existing content.
 *
 * @param path File path
 * @param data NUL-terminated string to write
 * @return 0 on success, -1 on failure
 */
int utils_write_file_string(const char *path, const char *data);

/**
 * Get the system uptime in seconds.
 *
 * @return Uptime in seconds, 0 on failure
 */
uint64_t utils_get_uptime_seconds(void);

/**
 * Get the system hostname.
 *
 * @param buf Output buffer
 * @param buf_len Size of buf
 * @return 0 on success, -1 on failure
 */
int utils_get_hostname(char *buf, size_t buf_len);

/**
 * Trim leading and trailing whitespace from a string in place.
 *
 * @param str String to trim
 * @return Pointer to the trimmed string
 */
char *utils_str_trim(char *str);

/**
 * Duplicate a string by allocating a new buffer.
 *
 * @param str String to duplicate
 * @return Pointer to the newly allocated string, NULL on failure
 */
char *utils_str_duplicate(const char *str);

/**
 * Check whether a string starts with the given prefix.
 *
 * @param str String to check
 * @param prefix Prefix to test
 * @return 1 if true, 0 otherwise
 */
int utils_str_starts_with(const char *str, const char *prefix);

/**
 * Check whether a string ends with the given suffix.
 *
 * @param str String to check
 * @param suffix Suffix to test
 * @return 1 if true, 0 otherwise
 */
int utils_str_ends_with(const char *str, const char *suffix);

/**
 * Get the current unix timestamp in seconds.
 *
 * @return Current timestamp in seconds
 */
uint64_t utils_get_current_timestamp(void);

/**
 * Format a unix timestamp as an ISO 8601 string (YYYY-MM-DDTHH:MM:SS).
 *
 * @param timestamp Unix timestamp
 * @param buf Output buffer
 * @param buf_len Size of buf
 * @return 0 on success, -1 on failure
 */
int utils_format_timestamp(uint64_t timestamp, char *buf, size_t buf_len);

/**
 * Execute a shell command and capture its stdout output.
 *
 * @param cmd Command to execute
 * @param output Output buffer for captured stdout (may be NULL)
 * @param output_len Size of output buffer
 * @param exit_code Output exit code (may be NULL)
 * @return 0 on success, -1 on failure
 */
int utils_exec_command(const char *cmd, char *output, size_t output_len, int *exit_code);

/**
 * Create a directory and all missing parent directories.
 *
 * @param path Directory path
 * @return 0 on success, -1 on failure
 */
int utils_mkdir_p(const char *path);

/**
 * Check whether a file or directory exists at the given path.
 *
 * @param path Path to check
 * @return 1 if exists, 0 otherwise
 */
int utils_file_exists(const char *path);

/**
 * Escape a string for safe inclusion in a JSON string literal.
 *
 * @param str String to escape
 * @return Newly allocated escaped string, NULL on failure (caller must free)
 */
char *utils_json_escape(const char *str);

/**
 * Unescape a JSON string literal into its raw form.
 *
 * @param json JSON string to unescape
 * @param out Output buffer
 * @param out_len Size of out
 * @return 0 on success, -1 on failure
 */
int utils_json_unescape(const char *json, char *out, size_t out_len);

#endif
