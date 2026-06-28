/*
 * Internationalized Domain Name (IDN) conversion implementation.
 * Provides Punycode encoding (RFC 3492) and URL/hostname to ASCII conversion.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "idn.h"
#include "logger.h"

/* Punycode parameter definitions (RFC 3492) */
#define PUNYCODE_BASE 36
#define PUNYCODE_TMIN 1
#define PUNYCODE_TMAX 26
#define PUNYCODE_SKEW 38
#define PUNYCODE_DAMP 700
#define PUNYCODE_INITIAL_BIAS 72
#define PUNYCODE_INITIAL_N 0x80
#define IDN_MAX_INPUT_LEN 1024
#define IDN_MAX_OUTPUT_LEN 2048

/* Internal helper: returns 1 if the code point is a basic ASCII code point. */
static int is_basic_code_point(uint32_t cp) {
    return cp < 0x80;
}

/* Internal helper: returns 1 if the code point is a Punycode delimiter. */
static int is_delimiter(uint32_t cp) {
    return cp == 0x2D || cp == 0x2E || cp == 0x5F || cp == 0x7E;
}

/* Internal helper: Punycode adaptive bias function (RFC 3492 section 6). */
static int adapt(int delta, int numpoints, int first) {
    delta = first ? delta / PUNYCODE_DAMP : delta >> 1;
    delta += delta / numpoints;

    int k = 0;
    while (delta > ((PUNYCODE_BASE - PUNYCODE_TMIN) * PUNYCODE_TMAX) / 2) {
        delta /= PUNYCODE_BASE - PUNYCODE_TMIN;
        k += PUNYCODE_BASE;
    }

    return k + (((PUNYCODE_BASE - PUNYCODE_TMIN + 1) * delta) / (delta + PUNYCODE_SKEW));
}

/* Internal helper: decode a UTF-8 byte sequence into an array of Unicode code points. */
static int utf8_decode(const char *input, size_t input_len, uint32_t *codepoints, size_t *num_codepoints) {
    if (!input || !codepoints || !num_codepoints) return -1;

    size_t i = 0, j = 0;
    while (i < input_len && j < IDN_MAX_INPUT_LEN) {
        uint32_t cp = 0;
        unsigned char c = (unsigned char)input[i];

        if (c < 0x80) {
            cp = c;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 >= input_len) return -1;
            cp = ((c & 0x1F) << 6) | ((unsigned char)input[i + 1] & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            if (i + 2 >= input_len) return -1;
            cp = ((c & 0x0F) << 12) | (((unsigned char)input[i + 1] & 0x3F) << 6) | ((unsigned char)input[i + 2] & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            if (i + 3 >= input_len) return -1;
            cp = ((c & 0x07) << 18) | (((unsigned char)input[i + 1] & 0x3F) << 12) | (((unsigned char)input[i + 2] & 0x3F) << 6) | ((unsigned char)input[i + 3] & 0x3F);
            i += 4;
        } else {
            return -1; /* Invalid UTF-8 */
        }

        codepoints[j++] = cp;
    }

    *num_codepoints = j;
    return 0;
}

/* Internal helper: convert a Punycode digit value to a character
 * (0-25 -> 'a'-'z', 26-35 -> '0'-'9', per RFC 3492). */
static char punycode_digit_to_char(int digit) {
    if (digit < 26) {
        return (char)('a' + digit);
    }
    return (char)('0' + (digit - 26));
}

/* Punycode encoding implementation */
int idn_punycode_encode(const char *input, size_t input_len, char *output, size_t output_size) {
    if (!input || !output || output_size == 0) return -1;
    
    uint32_t codepoints[IDN_MAX_INPUT_LEN];
    size_t num_codepoints = 0;
    
    if (utf8_decode(input, input_len, codepoints, &num_codepoints) != 0) {
        return -1;
    }
    
    size_t out_idx = 0;
    size_t n = PUNYCODE_INITIAL_N;
    int bias = PUNYCODE_INITIAL_BIAS;
    size_t h = 0;
    size_t b = 0;
    
    /* Copy all basic code points to output */
    for (size_t i = 0; i < num_codepoints; i++) {
        if (is_basic_code_point(codepoints[i])) {
            if (out_idx >= output_size - 1) return -2;
            output[out_idx++] = (char)codepoints[i];
            b++;
        }
    }
    
    h = b;
    
    /* If basic code points exist and non-basic code points still need encoding, add delimiter */
    if (b > 0 && h < num_codepoints) {
        if (out_idx >= output_size - 1) return -2;
        output[out_idx++] = '-';
    }
    
    size_t delta = 0;
    while (h < num_codepoints) {
        uint32_t m = 0xFFFFFFFF;
        
        /* Find the minimum non-basic code point */
        for (size_t i = 0; i < num_codepoints; i++) {
            if (codepoints[i] >= n && codepoints[i] < m) {
                m = codepoints[i];
            }
        }
        
        if (m - n > (0xFFFFFFFF - delta) / (h + 1)) {
            return -2; /* Overflow */
        }
        
        delta += (m - n) * (h + 1);
        n = m;
        
        for (size_t i = 0; i < num_codepoints; i++) {
            uint32_t c = codepoints[i];
            
            if (c < n) {
                delta++;
                if (delta == 0) return -2; /* Overflow */
            }
            
            if (c == n) {
                int q = delta;
                int k = PUNYCODE_BASE;
                int t_val = 0;
                
                while (1) {
                    if (k <= bias) {
                        t_val = PUNYCODE_TMIN;
                    } else if (k >= bias + PUNYCODE_TMAX) {
                        t_val = PUNYCODE_TMAX;
                    } else {
                        t_val = k - bias;
                    }
                    
                    if (q < t_val) break;
                    
                    if (out_idx >= output_size - 1) return -2;
                    int digit = t_val + (q - t_val) % (PUNYCODE_BASE - t_val);
                    output[out_idx++] = punycode_digit_to_char(digit);
                    
                    q = (q - t_val) / (PUNYCODE_BASE - t_val);
                    k += PUNYCODE_BASE;
                }
                
                if (out_idx >= output_size - 1) return -2;
                output[out_idx++] = punycode_digit_to_char(q);
                
                bias = adapt(delta, h + 1, h == b);
                delta = 0;
                h++;
            }
        }
        
        delta++;
        n++;
    }
    
    output[out_idx] = '\0';
    return 0;
}

/* Check whether the string contains non-ASCII characters */
int idn_contains_non_ascii(const char *str) {
    if (!str) return 0;
    
    while (*str) {
        if ((unsigned char)*str >= 0x80) {
            return 1;
        }
        str++;
    }
    return 0;
}

/* Convert hostname to ASCII format */
int idn_convert_host_to_ascii(const char *host, char *output, size_t output_size) {
    if (!host || !output || output_size == 0) return -1;
    
    /* Check if it is an IP address */
    struct in_addr addr4;
    struct in6_addr addr6;
    if (inet_pton(AF_INET, host, &addr4) == 1 || inet_pton(AF_INET6, host, &addr6) == 1) {
        strncpy(output, host, output_size - 1);
        output[output_size - 1] = '\0';
        return 0;
    }
    
    /* Separate hostname and port */
    const char *hostname = host;
    const char *port = NULL;
    int has_brackets = 0;
    
    /* Handle IPv6 format [::1]:port */
    if (host[0] == '[') {
        has_brackets = 1;
        const char *close_bracket = strchr(host, ']');
        if (close_bracket) {
            hostname = host + 1;
            size_t hostname_len = close_bracket - hostname;
            char hostname_buf[IDN_MAX_OUTPUT_LEN];
            if (hostname_len >= sizeof(hostname_buf)) return -1;
            strncpy(hostname_buf, hostname, hostname_len);
            hostname_buf[hostname_len] = '\0';
            
            if (close_bracket[1] == ':') {
                port = close_bracket + 1;
            }
            
            /* Convert hostname */
            if (idn_contains_non_ascii(hostname_buf)) {
                char encoded[IDN_MAX_OUTPUT_LEN];
                if (idn_punycode_encode(hostname_buf, strlen(hostname_buf), encoded, sizeof(encoded)) != 0) {
                    return -2;
                }
                
                if (port) {
                    snprintf(output, output_size, "[%s]%s", encoded, port);
                } else {
                    snprintf(output, output_size, "[%s]", encoded);
                }
            } else {
                if (port) {
                    snprintf(output, output_size, "[%s]%s", hostname_buf, port);
                } else {
                    snprintf(output, output_size, "[%s]", hostname_buf);
                }
            }
            return 0;
        }
    }
    
    /* Check whether it contains a port */
    size_t host_len = strlen(host);
    const char *last_colon = NULL;
    for (size_t i = 0; i < host_len; i++) {
        if (host[i] == ':') {
            last_colon = &host[i];
        }
    }
    
    /* If there are multiple colons, it may be an IPv6 address */
    int colon_count = 0;
    for (size_t i = 0; i < host_len; i++) {
        if (host[i] == ':') colon_count++;
    }
    
    if (colon_count > 1) {
        /* IPv6 address, copy directly */
        strncpy(output, host, output_size - 1);
        output[output_size - 1] = '\0';
        return 0;
    }
    
    /* IPv4:port or hostname:port */
    char hostname_buf[IDN_MAX_OUTPUT_LEN];
    if (last_colon) {
        size_t hostname_len = last_colon - host;
        if (hostname_len >= sizeof(hostname_buf)) return -1;
        strncpy(hostname_buf, host, hostname_len);
        hostname_buf[hostname_len] = '\0';
        port = last_colon;
    } else {
        strncpy(hostname_buf, host, sizeof(hostname_buf) - 1);
        hostname_buf[sizeof(hostname_buf) - 1] = '\0';
    }
    
    /* Convert hostname */
    if (idn_contains_non_ascii(hostname_buf)) {
        char encoded[IDN_MAX_OUTPUT_LEN];
        if (idn_punycode_encode(hostname_buf, strlen(hostname_buf), encoded, sizeof(encoded)) != 0) {
            return -2;
        }

        if (port) {
            snprintf(output, output_size, "%s%s", encoded, port);
        } else {
            snprintf(output, output_size, "%s", encoded);
        }
    } else {
        if (port) {
            snprintf(output, output_size, "%s%s", hostname_buf, port);
        } else {
            snprintf(output, output_size, "%s", hostname_buf);
        }
    }
    
    return 0;
}

/* Convert URL containing IDN to ASCII format */
int idn_convert_url_to_ascii(const char *url_str, char *output, size_t output_size) {
    if (!url_str || !output || output_size == 0) return -1;
    
    /* Parse URL structure: scheme://host:port/path?query#fragment */
    const char *scheme_end = strstr(url_str, "://");
    const char *host_start;
    const char *host_end = NULL;
    
    if (scheme_end) {
        host_start = scheme_end + 3;
    } else {
        host_start = url_str;
    }
    
    /* Find the end position of the hostname */
    const char *path_start = strchr(host_start, '/');
    const char *query_start = strchr(host_start, '?');
    const char *fragment_start = strchr(host_start, '#');
    
    if (path_start) host_end = path_start;
    if (query_start && (!host_end || query_start < host_end)) host_end = query_start;
    if (fragment_start && (!host_end || fragment_start < host_end)) host_end = fragment_start;
    
    if (!host_end) {
        host_end = url_str + strlen(url_str);
    }
    
    /* Extract the host part */
    size_t host_len = host_end - host_start;
    if (host_len >= IDN_MAX_INPUT_LEN) return -1;
    
    char host_buf[IDN_MAX_INPUT_LEN];
    strncpy(host_buf, host_start, host_len);
    host_buf[host_len] = '\0';
    
    /* Convert hostname */
    char ascii_host[IDN_MAX_OUTPUT_LEN];
    if (idn_convert_host_to_ascii(host_buf, ascii_host, sizeof(ascii_host)) != 0) {
        return -2;
    }
    
    /* Reassemble URL */
    if (scheme_end) {
        size_t scheme_len = scheme_end - url_str + 3;
        size_t remaining = host_end - url_str;
        snprintf(output, output_size, "%.*s%s%s", (int)scheme_len, url_str, ascii_host, host_end);
    } else {
        snprintf(output, output_size, "%s%s", ascii_host, host_end);
    }
    
    return 0;
}
