/*
 * Status report generation and upload implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

#include "report.h"
#include "monitoring.h"
#include "config.h"
#include "utils.h"
#include "virtual.h"
#include "gpu.h"
#include "cJSON.h"
#include "v2.h"
#include "jsonrpc.h"

/* Connect/send/recv timeout for HTTP requests (MIN-34: extracted magic number). */
#define HTTP_CONNECT_TIMEOUT_SEC 10

/* Escape special characters in a string for safe inclusion in a JSON string literal.
 * Conforms to RFC 8259 §7: escapes ", \, and control characters (0x00-0x1F)
 * using the named escapes (\b, \t, \n, \f, \r) where defined and \u00XX
 * for all other control characters. */
static void escape_json_string(const char *src, char *dst, size_t dst_len) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    /* Guard against NULL inputs and undersized buffers: the \u00XX escape
     * path below writes up to 6 bytes plus a NUL terminator, so bail out
     * early when the buffer cannot hold the worst-case escape. This also
     * prevents size_t underflow in the `dst_len - 6` bound check (size_t
     * is unsigned, so a small dst_len would wrap to a huge value). */
    if (!src || !dst || dst_len < 7) {
        if (dst && dst_len > 0) dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] && j < dst_len - 1; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            if (j < dst_len - 2) {
                dst[j++] = '\\';
                dst[j++] = c;
            }
        } else if (c == '\n') {
            if (j < dst_len - 2) {
                dst[j++] = '\\';
                dst[j++] = 'n';
            }
        } else if (c == '\r') {
            if (j < dst_len - 2) {
                dst[j++] = '\\';
                dst[j++] = 'r';
            }
        } else if (c == '\t') {
            if (j < dst_len - 2) {
                dst[j++] = '\\';
                dst[j++] = 't';
            }
        } else if (c == '\b') {
            if (j < dst_len - 2) {
                dst[j++] = '\\';
                dst[j++] = 'b';
            }
        } else if (c == '\f') {
            if (j < dst_len - 2) {
                dst[j++] = '\\';
                dst[j++] = 'f';
            }
        } else if (c < 0x20) {
            /* Other control characters: \u00XX (uppercase hex) */
            if (j < dst_len - 6) {
                dst[j++] = '\\';
                dst[j++] = 'u';
                dst[j++] = '0';
                dst[j++] = '0';
                dst[j++] = hex[(c >> 4) & 0xF];
                dst[j++] = hex[c & 0xF];
            }
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

/* URL-encode a string for safe inclusion in a URL query parameter value.
 * Encodes all characters except unreserved characters (A-Z, a-z, 0-9, -_~.)
 * as %XX hex sequences. Mirrors Go url.QueryEscape semantics for the
 * unreserved set; spaces are encoded as %20 (well-behaved servers accept
 * both %20 and + in query values).
 *
 * @param src Input string to encode (NUL-terminated)
 * @param dst Output buffer
 * @param dst_len Size of dst
 * @return Number of bytes written (excluding NUL) on success, -1 on
 *         failure (NULL argument or dst too small) */
static int url_encode(const char *src, char *dst, size_t dst_len) {
    if (!src || !dst || dst_len == 0) return -1;
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; src[i]; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            if (j + 1 >= dst_len) return -1;
            dst[j++] = (char)c;
        } else {
            if (j + 3 >= dst_len) return -1;
            dst[j++] = '%';
            dst[j++] = hex[(c >> 4) & 0xF];
            dst[j++] = hex[c & 0xF];
        }
    }
    dst[j] = '\0';
    return (int)j;
}

/* Send the entire buffer over the connected socket or TLS stream.
 * send(2) and SSL_write may return fewer bytes than requested (partial
 * write), so loop until all bytes are flushed. Retries on EINTR for
 * plain TCP sockets; SSL failures are treated as fatal since SSL_get_error
 * based retry logic is handled by OpenSSL internals for non-blocking mode.
 *
 * @param ssl  SSL object (NULL for plain TCP)
 * @param fd   Socket file descriptor (used when ssl is NULL)
 * @param data Buffer to send
 * @param len  Number of bytes to send
 * @return 0 on success, -1 on failure */
static int send_full(SSL *ssl, int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n;
        if (ssl) {
            n = SSL_write(ssl, data + sent, (int)(len - sent));
        } else {
            n = send(fd, data + sent, len - sent, 0);
        }
        if (n <= 0) {
            if (errno == EINTR) continue;  /* Retry on signal interruption */
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int report_generate(const agent_config_t *config, char *buf, size_t buf_len) {
    if (!config || !buf || buf_len == 0) return -1;
    
    cpu_info_t cpu;
    mem_info_t mem, swap;
    disk_info_t disk;
    net_info_t net;
    load_info_t load;
    conn_info_t conn;
    
    monitoring_get_cpu_info(&cpu);
    monitoring_get_mem_info(&mem);
    monitoring_get_swap_info(&swap);
    monitoring_get_disk_info(&disk);
    monitoring_get_net_info(&net);
    monitoring_get_load_info(&load);
    monitoring_get_conn_info(&conn);
    
    uint64_t uptime = monitoring_get_uptime();
    int process_count = monitoring_get_process_count();
    
    /* Report the real CPU usage, including 0% for an idle system. The server
     * treats 0.00 as a valid value, so no artificial floor is needed. */
    double cpu_usage = cpu.cpu_usage;

    int len = snprintf(buf, buf_len,
        "{"
        "\"cpu\":{\"usage\":%.2f},"
        "\"ram\":{\"total\":%" PRIu64 ",\"used\":%" PRIu64 "},"
        "\"swap\":{\"total\":%" PRIu64 ",\"used\":%" PRIu64 "},"
        "\"load\":{\"load1\":%.2f,\"load5\":%.2f,\"load15\":%.2f},"
        "\"disk\":{\"total\":%" PRIu64 ",\"used\":%" PRIu64 "},"
        "\"network\":{\"up\":%" PRIu64 ",\"down\":%" PRIu64 ",\"totalUp\":%" PRIu64 ",\"totalDown\":%" PRIu64 "},"
        "\"connections\":{\"tcp\":%d,\"udp\":%d},"
        "\"uptime\":%" PRIu64 ","
        "\"process\":%d,"
        "\"message\":\"\""
        "}",
        cpu_usage,
        mem.total, mem.used,
        swap.total, swap.used,
        load.load1, load.load5, load.load15,
        disk.total, disk.used,
        net.tx_speed, net.rx_speed,
        net.tx_bytes, net.rx_bytes,
        conn.tcp_count, conn.udp_count,
        uptime,
        process_count
    );

    /* Treat truncation or encoding error as failure: a truncated JSON body
     * would be rejected by the server and could expose partial/malformed
     * data. */
    if (len < 0 || (size_t)len >= buf_len) return -1;

    return len;
}

/* Internal helper: wrap a pre-serialized v1 JSON payload as a v2 JSON-RPC 2.0
 * notification using the provided builder function, then copy the result into
 * the caller-provided buffer.
 *
 * @param v1_json   NUL-terminated v1 JSON string (will be parsed).
 * @param buf       Output buffer for the v2 JSON-RPC payload.
 * @param buf_len   Size of buf.
 * @param builder   Function that wraps a cJSON object as a JSON-RPC payload;
 *                  takes ownership of the cJSON object on input.
 * @return Number of bytes written on success, -1 on failure.
 */
static int report_wrap_v2(const char *v1_json, char *buf, size_t buf_len,
                           int (*builder)(cJSON *, char **)) {
    if (!v1_json || !buf || buf_len == 0 || !builder) return -1;

    /* Parse the v1 JSON into a cJSON object so the v2 builder can attach it
     * under params.report / params.info. cJSON_Parse is thread-safe.
     *
     * Ownership of `data` is transferred to the builder on success. The
     * builder also takes care of freeing `data` on every failure path
     * (MIN-43), so this function must NOT free `data` after a builder
     * failure - doing so would be a double-free when the builder already
     * freed it via cJSON_Delete(params). */
    cJSON *data = cJSON_Parse(v1_json);
    if (!data) return -1;

    char *v2_str = NULL;
    if (builder(data, &v2_str) != 0) {
        /* data has been freed by the builder (MIN-43). */
        return -1;
    }

    size_t v2_len = strlen(v2_str);
    if (v2_len >= buf_len) {
        /* Buffer too small for the wrapped payload. */
        free(v2_str);
        return -1;
    }
    memcpy(buf, v2_str, v2_len + 1);
    free(v2_str);

    return (int)v2_len;
}

int report_generate_v2(const agent_config_t *config, char *buf, size_t buf_len) {
    if (!config || !buf || buf_len == 0) return -1;

    /* Generate the v1-style report JSON first. Use a local buffer so the
     * caller's buffer is left untouched on failure. */
    char v1_buf[4096];
    int v1_len = report_generate(config, v1_buf, sizeof(v1_buf));
    if (v1_len <= 0) return -1;
    v1_buf[sizeof(v1_buf) - 1] = '\0';

    return report_wrap_v2(v1_buf, buf, buf_len, v2_build_report_payload);
}

int report_generate_v2_with_acks(const agent_config_t *config, char *buf,
                                 size_t buf_len, const int *ack_ids,
                                 int ack_count) {
    if (!config || !buf || buf_len == 0) return -1;
    if (ack_count < 0) ack_count = 0;

    /* Generate the v1-style report JSON first. */
    char v1_buf[4096];
    int v1_len = report_generate(config, v1_buf, sizeof(v1_buf));
    if (v1_len <= 0) return -1;
    v1_buf[sizeof(v1_buf) - 1] = '\0';

    /* Parse the v1 JSON into a cJSON object so jsonrpc_build_report_request
     * can attach it under params.report. cJSON_Parse is thread-safe.
     *
     * Ownership of `data` is transferred to jsonrpc_build_report_request on
     * success, and the builder also frees `data` on every failure path
     * (MIN-43). Do NOT free `data` here after a builder failure - that
     * would be a double-free when the builder already freed it via
     * cJSON_Delete(params). */
    cJSON *data = cJSON_Parse(v1_buf);
    if (!data) return -1;

    /* Build a v2 JSON-RPC request with the ACK IDs. The request id is derived
     * from the current time so each report cycle uses a distinct value (the
     * server uses it only for request/response correlation, not for dedup).
     * jsonrpc_build_report_request takes ownership of `data` on input. */
    int request_id = (int)time(NULL);
    cJSON *root = jsonrpc_build_report_request(request_id, data, ack_ids, ack_count);
    if (!root) {
        /* data has been freed by the builder (MIN-43). */
        return -1;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) return -1;

    size_t json_len = strlen(json_str);
    if (json_len >= buf_len) {
        free(json_str);
        return -1;
    }
    memcpy(buf, json_str, json_len + 1);
    free(json_str);

    return (int)json_len;
}

int report_generate_basic_info(const agent_config_t *config, char *buf, size_t buf_len) {
    if (!config || !buf || buf_len == 0) return -1;
    
    cpu_info_t cpu;
    mem_info_t mem, swap;
    disk_info_t disk;
    system_info_t sys;
    char ipv4[64] = "", ipv6[128] = "";
    
    monitoring_get_cpu_info(&cpu);
    monitoring_get_mem_info(&mem);
    monitoring_get_swap_info(&swap);
    monitoring_get_disk_info(&disk);
    monitoring_get_system_info(&sys);
    monitoring_get_ip_address(ipv4, sizeof(ipv4), ipv6, sizeof(ipv6));
    
    char cpu_name_escaped[256];
    char os_name_escaped[256];
    char kernel_escaped[128];
    char gpu_name[128] = "";
    char gpu_name_escaped[256] = "";

    escape_json_string(cpu.cpu_name, cpu_name_escaped, sizeof(cpu_name_escaped));
    escape_json_string(sys.os_name, os_name_escaped, sizeof(os_name_escaped));
    escape_json_string(sys.kernel_version, kernel_escaped, sizeof(kernel_escaped));

    /* Get GPU name (use empty string on failure) */
    if (gpu_get_name(gpu_name, sizeof(gpu_name)) == 0) {
        escape_json_string(gpu_name, gpu_name_escaped, sizeof(gpu_name_escaped));
    }

    const char *ipv4_val = config->custom_ipv4[0] ? config->custom_ipv4 : ipv4;
    const char *ipv6_val = config->custom_ipv6[0] ? config->custom_ipv6 : ipv6;

    const char *virt_type = virt_detect();

    int len = snprintf(buf, buf_len,
        "{"
        "\"cpu_name\":\"%s\","
        "\"cpu_cores\":%d,"
        "\"arch\":\"%s\","
        "\"os\":\"%s\","
        "\"kernel_version\":\"%s\","
        "\"ipv4\":\"%s\","
        "\"ipv6\":\"%s\","
        "\"mem_total\":%" PRIu64 ","
        "\"swap_total\":%" PRIu64 ","
        "\"disk_total\":%" PRIu64 ","
        "\"gpu_name\":\"%s\","
        "\"virtualization\":\"%s\","
        "\"version\":\"1.0.0\""
        "}",
        cpu_name_escaped,
        cpu.cpu_cores,
        cpu.cpu_arch,
        os_name_escaped,
        kernel_escaped,
        ipv4_val,
        ipv6_val,
        mem.total,
        swap.total,
        disk.total,
        gpu_name_escaped,
        virt_type
    );

    /* Treat truncation or encoding error as failure: a truncated JSON body
     * would be rejected by the server and could expose partial/malformed
     * data. */
    if (len < 0 || (size_t)len >= buf_len) return -1;

    return len;
}

int report_generate_basic_info_v2(const agent_config_t *config, char *buf, size_t buf_len) {
    if (!config || !buf || buf_len == 0) return -1;

    /* Generate the v1-style basic info JSON first. Use a local buffer so the
     * caller's buffer is left untouched on failure. */
    char v1_buf[4096];
    int v1_len = report_generate_basic_info(config, v1_buf, sizeof(v1_buf));
    if (v1_len <= 0) return -1;
    v1_buf[sizeof(v1_buf) - 1] = '\0';

    return report_wrap_v2(v1_buf, buf, buf_len, v2_build_basic_info_payload);
}

/* Perform an HTTP POST request with optional extra headers, supporting HTTPS.
 * Returns 0 when the server responds with HTTP 200, -1 otherwise. */
static int http_post_with_headers(const char *url, const char *payload, int ignore_cert,
                                   const char *extra_headers) {
    if (!url || !payload) return -1;
    
    /* Parse URL */
    char scheme[16] = "http";
    char host[256] = "";
    int port = 80;
    char path[512] = "/";
    
    const char *p = url;
    
    /* Parse scheme */
    const char *scheme_end = strstr(url, "://");
    if (scheme_end) {
        size_t scheme_len = scheme_end - url;
        if (scheme_len >= sizeof(scheme)) scheme_len = sizeof(scheme) - 1;
        strncpy(scheme, url, scheme_len);
        scheme[scheme_len] = '\0';
        p = scheme_end + 3;
    }
    
    if (strcmp(scheme, "https") == 0) {
        port = 443;
    }
    
    /* Parse host:port/path */
    const char *path_start = strchr(p, '/');
    if (path_start) {
        size_t host_len = path_start - p;
        if (host_len >= sizeof(host)) host_len = sizeof(host) - 1;
        strncpy(host, p, host_len);
        host[host_len] = '\0';
        strncpy(path, path_start, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    } else {
        strncpy(host, p, sizeof(host) - 1);
        host[sizeof(host) - 1] = '\0';
    }
    
    /* Parse port with validation: atoi gives no error indication, so use strtol
     * and verify the value is in the valid port range [1, 65535]. On invalid
     * input, keep the scheme default port (80 for http, 443 for https). */
    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        char *endptr = NULL;
        long parsed_port = strtol(colon + 1, &endptr, 10);
        if (endptr != colon + 1 && *endptr == '\0' &&
            parsed_port >= 1 && parsed_port <= 65535) {
            port = (int)parsed_port;
        }
    }
    
    /* DNS resolution: iterate through all addresses for IPv4/IPv6 support */
    char port_str[16];
    int port_n = snprintf(port_str, sizeof(port_str), "%d", port);
    if (port_n < 0 || (size_t)port_n >= sizeof(port_str)) {
        return -1;
    }

    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        return -1;
    }

    int fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        /* Set timeout */
        struct timeval tv;
        tv.tv_sec = HTTP_CONNECT_TIMEOUT_SEC;
        tv.tv_usec = 0;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            break;  /* Success */
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);

    if (fd < 0) {
        return -1;
    }
    
    SSL *ssl = NULL;
    SSL_CTX *ssl_ctx = NULL;
    
    /* TLS connection */
    if (strcmp(scheme, "https") == 0) {
        /* OpenSSL compatibility: 1.0.2 uses SSLv23_client_method, 1.1.0+ uses TLS_client_method */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        ssl_ctx = SSL_CTX_new(SSLv23_client_method());
#else
        ssl_ctx = SSL_CTX_new(TLS_client_method());
#endif
        if (!ssl_ctx) {
            close(fd);
            return -1;
        }
        
        /*
         * Mirror the Go reference implementation (see .komari-agent-main/
         * server/websocket.go newWSDialer): a tls.Config verifies the
         * peer by default; only set InsecureSkipVerify when ignore_cert
         * is explicitly requested. Without this, MITM attackers can
         * present any certificate and the handshake still succeeds.
         */
        if (ignore_cert) {
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);
        } else {
            /* Enable certificate verification by default (mirrors Go
             * tls.Config default). Load the system CA store so chain
             * verification can succeed, then require peer verification. */
            SSL_CTX_set_default_verify_paths(ssl_ctx);
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
        }

        ssl = SSL_new(ssl_ctx);
        if (!ssl) {
            SSL_CTX_free(ssl_ctx);
            close(fd);
            return -1;
        }
        
        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, host);
        /*
         * Verify hostname against certificate (mirrors Go
         * tls.Config.ServerName). SSL_set1_host is available in
         * OpenSSL 1.0.2+; for older builds we fall back to
         * SSL_VERIFY_PEER chain-only verification.
         */
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
        if (!ignore_cert) {
            SSL_set1_host(ssl, host);
        }
#else
        /* Fallback: rely on SSL_CTX_set_verify with SSL_VERIFY_PEER only */
#endif

        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            close(fd);
            return -1;
        }
    }

    /* Build HTTP request */
    char request[16384];
    int req_len;
    if (extra_headers && extra_headers[0] != '\0') {
        req_len = snprintf(request, sizeof(request),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "%s"
            "\r\n"
            "%s",
            path, host, strlen(payload), extra_headers, payload);
    } else {
        req_len = snprintf(request, sizeof(request),
            "POST %s HTTP/1.1\r\n"
            "Host: %s\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s",
            path, host, strlen(payload), payload);
    }

    /* Check for snprintf truncation or encoding error */
    if (req_len < 0 || (size_t)req_len >= sizeof(request)) {
        if (ssl) SSL_free(ssl);
        if (ssl_ctx) SSL_CTX_free(ssl_ctx);
        close(fd);
        return -1;
    }

    /* Send the entire request, handling partial writes and EINTR. */
    if (send_full(ssl, fd, request, (size_t)req_len) != 0) {
        if (ssl) SSL_free(ssl);
        if (ssl_ctx) SSL_CTX_free(ssl_ctx);
        close(fd);
        return -1;
    }
    
    /* Read response (simple read, does not parse the full response) */
    char response[1024];
    int recv_len = 0;
    if (ssl) {
        recv_len = SSL_read(ssl, response, sizeof(response) - 1);
    } else {
        recv_len = recv(fd, response, sizeof(response) - 1, 0);
    }
    
    int ret = 0;
    if (recv_len > 0) {
        response[recv_len] = '\0';
        /* Check HTTP status code at the start of the response. Using strncmp
         * avoids matching "HTTP/1.1 200" inside the response body. */
        if (strncmp(response, "HTTP/1.1 200", 12) == 0 ||
            strncmp(response, "HTTP/1.0 200", 12) == 0) {
            ret = 0;
        } else {
            ret = -1;
        }
    } else {
        ret = -1;
    }
    
    /* Cleanup resources */
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (ssl_ctx) SSL_CTX_free(ssl_ctx);
    close(fd);
    
    return ret;
}

/* Thin wrapper around http_post_with_headers for callers that do not need
 * custom headers. Forwards the same arguments with extra_headers=NULL. */
static int http_post(const char *url, const char *payload, int ignore_cert) {
    return http_post_with_headers(url, payload, ignore_cert, NULL);
}

int report_upload_task_result(const agent_config_t *config,
                               const char *task_id,
                               const char *result,
                               int exit_code,
                               uint64_t finished_at) {
    if (!config || !task_id) return -1;

    char *escaped_result = utils_json_escape(result ? result : "");
    if (!escaped_result) return -1;

    /* Escape task_id to prevent breaking the JSON structure if it contains
     * quotes, backslashes, or control characters. */
    char *escaped_task_id = utils_json_escape(task_id);
    if (!escaped_task_id) {
        free(escaped_result);
        return -1;
    }

    /*
     * Design note: The token is passed via URL query string (e.g., "?token=xxx")
     * rather than an Authorization header. This is intentional and matches the
     * Go reference implementation (see .komari-agent-main/server/task.go and
     * basicInfo.go), where all HTTP reporting endpoints
     * (/api/clients/task/result, /api/clients/ping/result,
     * /api/clients/uploadBasicInfo) accept the token as a query parameter.
     * The server side already supports this auth scheme, so changing to a
     * header-based approach would break compatibility.
     */
    /* URL-encode the token to handle special characters safely (mirrors
     * Go url.QueryEscape used by the reference client). The encoded form
     * may be up to 3x the original length (each byte -> %XX). */
    char encoded_token[MAX_TOKEN_LEN * 3 + 1];
    if (url_encode(config->token, encoded_token, sizeof(encoded_token)) < 0) {
        free(escaped_result);
        free(escaped_task_id);
        return -1;
    }

    /* Size URL buffer for endpoint + path + encoded token with margin. */
    char url[MAX_ENDPOINT_LEN + 64 + MAX_TOKEN_LEN * 3 + 1];
    int url_n = snprintf(url, sizeof(url), "%s/api/clients/task/result?token=%s",
                         config->endpoint, encoded_token);
    if (url_n < 0 || (size_t)url_n >= sizeof(url)) {
        free(escaped_result);
        free(escaped_task_id);
        return -1;
    }

    char payload[8192];
    int n = snprintf(payload, sizeof(payload),
        "{\"task_id\":\"%s\",\"result\":\"%s\",\"exit_code\":%d,\"finished_at\":%" PRIu64 "}",
        escaped_task_id, escaped_result, exit_code, finished_at);

    free(escaped_result);
    free(escaped_task_id);

    /* Abort if the payload was truncated: a truncated JSON body would be
     * rejected by the server and could expose partial/malformed data. */
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        return -1;
    }

    char headers[512] = "";
    if (config->cf_access_client_id[0] != '\0' && config->cf_access_client_secret[0] != '\0') {
        int hdr_n = snprintf(headers, sizeof(headers),
                 "CF-Access-Client-Id: %s\r\n"
                 "CF-Access-Client-Secret: %s\r\n",
                 config->cf_access_client_id, config->cf_access_client_secret);
        if (hdr_n < 0 || (size_t)hdr_n >= sizeof(headers)) {
            return -1;
        }
    }
    
    return http_post_with_headers(url, payload, config->ignore_unsafe_cert,
                                   headers[0] != '\0' ? headers : NULL);
}

int report_upload_ping_result(const agent_config_t *config,
                               uint32_t task_id,
                               const char *ping_type,
                               int value,
                               uint64_t finished_at) {
    if (!config || !ping_type) return -1;

    /* Escape ping_type before embedding it into the JSON payload to prevent
     * log injection / JSON structural breakage when the field contains
     * quotes, backslashes or control characters (MIN-18, T26.4). Mirrors
     * the escaping applied to task_id/result in report_upload_task_result. */
    char *escaped_ping_type = utils_json_escape(ping_type);
    if (!escaped_ping_type) return -1;

    /* Token passed via URL query string by design; see design note above. */
    /* URL-encode the token to handle special characters safely (mirrors
     * Go url.QueryEscape used by the reference client). */
    char encoded_token[MAX_TOKEN_LEN * 3 + 1];
    if (url_encode(config->token, encoded_token, sizeof(encoded_token)) < 0) {
        free(escaped_ping_type);
        return -1;
    }

    /* Size URL buffer for endpoint + path + encoded token with margin. */
    char url[MAX_ENDPOINT_LEN + 64 + MAX_TOKEN_LEN * 3 + 1];
    int url_n = snprintf(url, sizeof(url), "%s/api/clients/ping/result?token=%s",
                         config->endpoint, encoded_token);
    if (url_n < 0 || (size_t)url_n >= sizeof(url)) {
        free(escaped_ping_type);
        return -1;
    }

    char payload[512];
    int payload_n = snprintf(payload, sizeof(payload),
        "{\"type\":\"ping_result\",\"task_id\":%u,\"ping_type\":\"%s\",\"value\":%d,\"finished_at\":%" PRIu64 "}",
        task_id, escaped_ping_type, value, finished_at);
    if (payload_n < 0 || (size_t)payload_n >= sizeof(payload)) {
        free(escaped_ping_type);
        return -1;
    }

    /* escaped_ping_type is no longer needed once the payload has been built. */
    free(escaped_ping_type);

    char headers[512] = "";
    if (config->cf_access_client_id[0] != '\0' && config->cf_access_client_secret[0] != '\0') {
        int hdr_n = snprintf(headers, sizeof(headers),
                 "CF-Access-Client-Id: %s\r\n"
                 "CF-Access-Client-Secret: %s\r\n",
                 config->cf_access_client_id, config->cf_access_client_secret);
        if (hdr_n < 0 || (size_t)hdr_n >= sizeof(headers)) {
            return -1;
        }
    }

    return http_post_with_headers(url, payload, config->ignore_unsafe_cert,
                                   headers[0] != '\0' ? headers : NULL);
}
