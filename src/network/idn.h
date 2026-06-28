/*
 * Internationalized Domain Name (IDN) helpers.
 * Converts Unicode hostnames/URLs to ASCII Compatible Encoding (Punycode).
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_IDN_H
#define KOMARI_AGENT_C_IDN_H

#include <stdint.h>
#include <stddef.h>

#define IDN_MAX_INPUT_LEN 1024
#define IDN_MAX_OUTPUT_LEN 2048
#define IDN_PUNYCODE_PREFIX "xn--"

/**
 * Convert a URL containing an Internationalized Domain Name (IDN) to ASCII
 * Compatible Encoding (ACE) format.
 * e.g.: "https://münchen.de" -> "https://xn--mnchen-3ya.de"
 *
 * @param url_str     Input URL string.
 * @param output      Output buffer.
 * @param output_size Output buffer size (in bytes).
 * @return 0 on success, -1 on parameter error, -2 on conversion failure.
 */
int idn_convert_url_to_ascii(const char *url_str, char *output, size_t output_size);

/**
 * Convert a hostname (possibly containing a port) to ASCII Compatible Encoding
 * format.
 * e.g.: "münchen.de:8080" -> "xn--mnchen-3ya.de:8080"
 *
 * @param host        Input hostname string.
 * @param output      Output buffer.
 * @param output_size Output buffer size (in bytes).
 * @return 0 on success, -1 on parameter error, -2 on conversion failure.
 */
int idn_convert_host_to_ascii(const char *host, char *output, size_t output_size);

/**
 * Check whether the string contains non-ASCII characters.
 *
 * @param str Input string.
 * @return 1 if the string contains non-ASCII characters, 0 if it is ASCII only.
 */
int idn_contains_non_ascii(const char *str);

/**
 * Punycode encoding (RFC 3492).
 *
 * @param input       Input Unicode string (UTF-8).
 * @param input_len   Input length (in bytes).
 * @param output      Output buffer.
 * @param output_size Output buffer size (in bytes).
 * @return 0 on success, -1 on parameter error, -2 if the output buffer is too small.
 */
int idn_punycode_encode(const char *input, size_t input_len, char *output, size_t output_size);

#endif
