/*
 * Status report generation and upload implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* Escape special characters in a string for safe inclusion in a JSON string literal.
 * Conforms to RFC 8259: escapes ", \, and control characters (0x00-0x1F). */
static void escape_json_string(const char *src, char *dst, size_t dst_len) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
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
        "\"ram\":{\"total\":%lu,\"used\":%lu},"
        "\"swap\":{\"total\":%lu,\"used\":%lu},"
        "\"load\":{\"load1\":%.2f,\"load5\":%.2f,\"load15\":%.2f},"
        "\"disk\":{\"total\":%lu,\"used\":%lu},"
        "\"network\":{\"up\":%lu,\"down\":%lu,\"totalUp\":%lu,\"totalDown\":%lu},"
        "\"connections\":{\"tcp\":%d,\"udp\":%d},"
        "\"uptime\":%lu,"
        "\"process\":%d,"
        "\"message\":\"\""
        "}",
        cpu_usage,
        (unsigned long)mem.total, (unsigned long)mem.used,
        (unsigned long)swap.total, (unsigned long)swap.used,
        load.load1, load.load5, load.load15,
        (unsigned long)disk.total, (unsigned long)disk.used,
        (unsigned long)net.tx_speed, (unsigned long)net.rx_speed,
        (unsigned long)net.tx_bytes, (unsigned long)net.rx_bytes,
        conn.tcp_count, conn.udp_count,
        (unsigned long)uptime,
        process_count
    );
    
    return len;
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
        "\"mem_total\":%lu,"
        "\"swap_total\":%lu,"
        "\"disk_total\":%lu,"
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
        (unsigned long)mem.total,
        (unsigned long)swap.total,
        (unsigned long)disk.total,
        gpu_name_escaped,
        virt_type
    );

    return len;
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
    snprintf(port_str, sizeof(port_str), "%d", port);

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
        tv.tv_sec = 10;
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
        
        if (ignore_cert) {
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);
        }
        
        ssl = SSL_new(ssl_ctx);
        if (!ssl) {
            SSL_CTX_free(ssl_ctx);
            close(fd);
            return -1;
        }
        
        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, host);
        
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

    /* Send request: loop until all bytes are flushed. send(2) and SSL_write
     * may return fewer bytes than requested, so retry the remainder. */
    size_t sent = 0;
    while (sent < (size_t)req_len) {
        ssize_t n;
        if (ssl) {
            n = SSL_write(ssl, request + sent, req_len - sent);
        } else {
            n = send(fd, request + sent, req_len - sent, 0);
        }
        if (n <= 0) {
            if (!ssl && n < 0 && errno == EINTR) continue;
            break;  /* error or connection closed */
        }
        sent += (size_t)n;
    }

    if (sent < (size_t)req_len) {
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

    /* The token is passed as a URL query parameter for server-side
     * compatibility. Using an Authorization: Bearer header would be safer
     * (avoids token leakage via access logs and intermediary caches), but
     * the server endpoint expects the token in the query string and does not
     * accept the Authorization header. Switching would break authentication,
     * so the current approach is retained. */
    char url[512];
    snprintf(url, sizeof(url), "%s/api/clients/task/result?token=%s",
             config->endpoint, config->token);

    char payload[8192];
    int n = snprintf(payload, sizeof(payload),
        "{\"task_id\":\"%s\",\"result\":\"%s\",\"exit_code\":%d,\"finished_at\":%lu}",
        escaped_task_id, escaped_result, exit_code, (unsigned long)finished_at);

    free(escaped_result);
    free(escaped_task_id);

    /* Abort if the payload was truncated: a truncated JSON body would be
     * rejected by the server and could expose partial/malformed data. */
    if (n < 0 || (size_t)n >= sizeof(payload)) {
        return -1;
    }

    char headers[512] = "";
    if (config->cf_access_client_id[0] != '\0' && config->cf_access_client_secret[0] != '\0') {
        snprintf(headers, sizeof(headers),
                 "CF-Access-Client-Id: %s\r\n"
                 "CF-Access-Client-Secret: %s\r\n",
                 config->cf_access_client_id, config->cf_access_client_secret);
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
    
    char url[512];
    snprintf(url, sizeof(url), "%s/api/clients/ping/result?token=%s",
             config->endpoint, config->token);
    
    char payload[512];
    snprintf(payload, sizeof(payload),
        "{\"type\":\"ping_result\",\"task_id\":%u,\"ping_type\":\"%s\",\"value\":%d,\"finished_at\":%lu}",
        task_id, ping_type, value, (unsigned long)finished_at);
    
    char headers[512] = "";
    if (config->cf_access_client_id[0] != '\0' && config->cf_access_client_secret[0] != '\0') {
        snprintf(headers, sizeof(headers),
                 "CF-Access-Client-Id: %s\r\n"
                 "CF-Access-Client-Secret: %s\r\n",
                 config->cf_access_client_id, config->cf_access_client_secret);
    }
    
    return http_post_with_headers(url, payload, config->ignore_unsafe_cert,
                                   headers[0] != '\0' ? headers : NULL);
}
