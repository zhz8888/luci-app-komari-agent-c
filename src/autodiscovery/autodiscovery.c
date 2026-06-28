/*
 * Auto-discovery implementation: HTTP registration, config persistence
 * and reuse logic for the Komari agent.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

#include "autodiscovery.h"
#include "utils.h"
#include "logger.h"
#include "cJSON.h"

/* HTTP response buffer size */
#define HTTP_RESPONSE_BUF_SIZE 8192

/* Maximum length of each part after URL parsing */
#define URL_SCHEME_LEN  16
#define URL_HOST_LEN    256
#define URL_PATH_LEN    768

/**
 * Extract the body from an HTTP response (skip headers)
 *
 * @param response Complete HTTP response string
 * @return Pointer to the start of the body, or NULL if separator not found
 */
static const char *find_http_body(const char *response) {
    if (!response) return NULL;

    /* HTTP headers and body are separated by \r\n\r\n */
    const char *body = strstr(response, "\r\n\r\n");
    if (body) {
        return body + 4;
    }

    /* Compatible with \n\n only case */
    body = strstr(response, "\n\n");
    if (body) {
        return body + 2;
    }

    return NULL;
}

/**
 * Send an HTTP POST request and get the response body
 *
 * @param url           Complete URL
 * @param payload       Request body
 * @param ignore_cert   Whether to ignore certificate validation (HTTPS)
 * @param extra_headers Additional request headers (can be NULL)
 * @param response_buf  Response body output buffer
 * @param response_len  Buffer length
 * @return 0 on success, -1 on failure
 */
static int http_post_get_body(const char *url,
                               const char *payload,
                               int ignore_cert,
                               const char *extra_headers,
                               char *response_buf,
                               size_t response_len) {
    if (!url || !payload || !response_buf || response_len == 0) return -1;

    char scheme[URL_SCHEME_LEN] = "http";
    char host[URL_HOST_LEN] = "";
    int port = 80;
    char path[URL_PATH_LEN] = "/";

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

    /* Parse port */
    char *colon = strchr(host, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }

    /* DNS resolution: iterate through all addresses for IPv4/IPv6 support */
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port_str, &hints, &res) != 0) {
        KOMARI_LOG_ERROR("Auto-discovery: DNS resolution failed host=%s", host);
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
        KOMARI_LOG_ERROR("Auto-discovery: Connection failed host=%s port=%d", host, port);
        return -1;
    }

    SSL *ssl = NULL;
    SSL_CTX *ssl_ctx = NULL;

    /* TLS connection */
    if (strcmp(scheme, "https") == 0) {
        /* OpenSSL version compatibility: 1.0.2 uses SSLv23_client_method, 1.1.0+ uses TLS_client_method */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        ssl_ctx = SSL_CTX_new(SSLv23_client_method());
#else
        ssl_ctx = SSL_CTX_new(TLS_client_method());
#endif
        if (!ssl_ctx) {
            close(fd);
            KOMARI_LOG_ERROR("Auto-discovery: Failed to create SSL_CTX");
            return -1;
        }

        if (ignore_cert) {
            SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);
        }

        ssl = SSL_new(ssl_ctx);
        if (!ssl) {
            SSL_CTX_free(ssl_ctx);
            close(fd);
            KOMARI_LOG_ERROR("Auto-discovery: Failed to create SSL object");
            return -1;
        }

        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, host);

        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl);
            SSL_CTX_free(ssl_ctx);
            close(fd);
            KOMARI_LOG_ERROR("Auto-discovery: SSL handshake failed");
            return -1;
        }
    }

    /* Build HTTP request */
    char request[4096];
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

    if (req_len < 0 || (size_t)req_len >= sizeof(request)) {
        if (ssl) SSL_free(ssl);
        if (ssl_ctx) SSL_CTX_free(ssl_ctx);
        close(fd);
        KOMARI_LOG_ERROR("Auto-discovery: HTTP request too long");
        return -1;
    }

    /* Send request */
    int sent = 0;
    if (ssl) {
        sent = SSL_write(ssl, request, req_len);
    } else {
        sent = send(fd, request, req_len, 0);
    }

    if (sent < 0) {
        if (ssl) SSL_free(ssl);
        if (ssl_ctx) SSL_CTX_free(ssl_ctx);
        close(fd);
        KOMARI_LOG_ERROR("Auto-discovery: Failed to send HTTP request");
        return -1;
    }

    /* Read response in a loop until connection closes or buffer is full */
    char raw_buf[HTTP_RESPONSE_BUF_SIZE];
    size_t total = 0;
    int n = 0;

    while (total < sizeof(raw_buf) - 1) {
        if (ssl) {
            n = SSL_read(ssl, raw_buf + total, sizeof(raw_buf) - 1 - total);
        } else {
            n = recv(fd, raw_buf + total, sizeof(raw_buf) - 1 - total, 0);
        }

        if (n > 0) {
            total += (size_t)n;
        } else {
            break;
        }
    }
    raw_buf[total] = '\0';

    /* Cleanup resources */
    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    if (ssl_ctx) SSL_CTX_free(ssl_ctx);
    close(fd);

    if (total == 0) {
        KOMARI_LOG_ERROR("Auto-discovery: HTTP response is empty");
        return -1;
    }

    /* Validate HTTP status code */
    if (!strstr(raw_buf, "HTTP/1.1 200") && !strstr(raw_buf, "HTTP/1.0 200")) {
        KOMARI_LOG_ERROR("Auto-discovery: HTTP status code is not 200");
        return -1;
    }

    /* Extract response body */
    const char *body = find_http_body(raw_buf);
    if (!body) {
        KOMARI_LOG_ERROR("Auto-discovery: Cannot locate HTTP response body");
        return -1;
    }

    strncpy(response_buf, body, response_len - 1);
    response_buf[response_len - 1] = '\0';

    return 0;
}

int autodiscovery_get_file_path(char *path, size_t path_len) {
    if (!path || path_len == 0) return -1;

    const char *src = AUTODISCOVERY_FILE_PATH;
    if (strlen(src) >= path_len) return -1;

    strncpy(path, src, path_len - 1);
    path[path_len - 1] = '\0';
    return 0;
}

int autodiscovery_load_config(autodiscovery_config_t *config) {
    if (!config) return -1;

    memset(config, 0, sizeof(*config));

    char path[256];
    if (autodiscovery_get_file_path(path, sizeof(path)) != 0) {
        return -1;
    }

    if (!utils_file_exists(path)) {
        return -1;
    }

    char *buf = malloc(4096);
    if (!buf) {
        KOMARI_LOG_ERROR("Auto-discovery: Failed to allocate read buffer");
        return -1;
    }

    if (utils_read_file_string(path, buf, 4096) != 0) {
        free(buf);
        KOMARI_LOG_ERROR("Auto-discovery: Failed to read configuration file path=%s", path);
        return -1;
    }

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        KOMARI_LOG_ERROR("Auto-discovery: Failed to parse configuration JSON");
        return -1;
    }

    cJSON *uuid_item = cJSON_GetObjectItem(root, "uuid");
    cJSON *token_item = cJSON_GetObjectItem(root, "token");

    int ret = 0;
    if (uuid_item && cJSON_IsString(uuid_item) && uuid_item->valuestring) {
        strncpy(config->uuid, uuid_item->valuestring, sizeof(config->uuid) - 1);
        config->uuid[sizeof(config->uuid) - 1] = '\0';
    } else {
        ret = -1;
    }

    if (token_item && cJSON_IsString(token_item) && token_item->valuestring) {
        strncpy(config->token, token_item->valuestring, sizeof(config->token) - 1);
        config->token[sizeof(config->token) - 1] = '\0';
    } else {
        ret = -1;
    }

    cJSON_Delete(root);

    if (ret != 0) {
        KOMARI_LOG_WARN("Auto-discovery: Configuration file missing uuid or token field");
    }

    return ret;
}

int autodiscovery_save_config(const autodiscovery_config_t *config) {
    if (!config) return -1;

    char path[256];
    if (autodiscovery_get_file_path(path, sizeof(path)) != 0) {
        return -1;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        KOMARI_LOG_ERROR("Auto-discovery: Failed to create JSON object");
        return -1;
    }

    if (!cJSON_AddStringToObject(root, "uuid", config->uuid) ||
        !cJSON_AddStringToObject(root, "token", config->token)) {
        cJSON_Delete(root);
        KOMARI_LOG_ERROR("Auto-discovery: Failed to add JSON field");
        return -1;
    }

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        KOMARI_LOG_ERROR("Auto-discovery: Failed to serialize JSON");
        return -1;
    }

    /* Persist with restrictive permissions: the file contains the agent token */
    int ret = utils_write_file_string(path, json_str, 0600);
    cJSON_free(json_str);

    if (ret != 0) {
        KOMARI_LOG_ERROR("Auto-discovery: Failed to write configuration file path=%s", path);
        return -1;
    }

    KOMARI_LOG_DEBUG("Auto-discovery: Configuration saved path=%s", path);
    return 0;
}

/* Check whether a string contains CR or LF characters (header injection prevention). */
static int contains_crlf(const char *s) {
    if (!s) return 0;
    for (; *s; s++) {
        if (*s == '\r' || *s == '\n') return 1;
    }
    return 0;
}

int autodiscovery_register(const char *endpoint,
                            const char *auto_discovery_key,
                            const char *hostname,
                            autodiscovery_config_t *config) {
    if (!endpoint || !auto_discovery_key || !hostname || !config) return -1;

    /* Prevent HTTP header injection: reject CR/LF in user-controlled fields */
    if (contains_crlf(auto_discovery_key)) {
        KOMARI_LOG_ERROR("Auto-discovery: auto_discovery_key contains CR/LF characters");
        return -1;
    }
    if (contains_crlf(hostname)) {
        KOMARI_LOG_ERROR("Auto-discovery: hostname contains CR/LF characters");
        return -1;
    }

    /* Build registration URL: {endpoint}/api/clients/register?name={hostname} */
    char url[768];
    int n = snprintf(url, sizeof(url), "%s/api/clients/register?name=%s",
                     endpoint, hostname);
    if (n < 0 || (size_t)n >= sizeof(url)) {
        KOMARI_LOG_ERROR("Auto-discovery: Registration URL too long");
        return -1;
    }

    /* Build Authorization header */
    char auth_header[512];
    n = snprintf(auth_header, sizeof(auth_header),
                 "Authorization: Bearer %s\r\n", auto_discovery_key);
    if (n < 0 || (size_t)n >= sizeof(auth_header)) {
        KOMARI_LOG_ERROR("Auto-discovery: Authorization header too long");
        return -1;
    }

    /* Send POST request (empty body) */
    char response[HTTP_RESPONSE_BUF_SIZE];
    if (http_post_get_body(url, "", 0, auth_header,
                            response, sizeof(response)) != 0) {
        KOMARI_LOG_ERROR("Auto-discovery: Registration request failed url=%s", url);
        return -1;
    }

    /* Parse response JSON, extract uuid and token */
    cJSON *root = cJSON_Parse(response);
    if (!root) {
        KOMARI_LOG_ERROR("Auto-discovery: Failed to parse response JSON");
        return -1;
    }

    memset(config, 0, sizeof(*config));

    cJSON *uuid_item = cJSON_GetObjectItem(root, "uuid");
    cJSON *token_item = cJSON_GetObjectItem(root, "token");

    int ret = 0;
    if (uuid_item && cJSON_IsString(uuid_item) && uuid_item->valuestring) {
        strncpy(config->uuid, uuid_item->valuestring, sizeof(config->uuid) - 1);
        config->uuid[sizeof(config->uuid) - 1] = '\0';
    } else {
        ret = -1;
    }

    if (token_item && cJSON_IsString(token_item) && token_item->valuestring) {
        strncpy(config->token, token_item->valuestring, sizeof(config->token) - 1);
        config->token[sizeof(config->token) - 1] = '\0';
    } else {
        ret = -1;
    }

    cJSON_Delete(root);

    if (ret != 0) {
        KOMARI_LOG_ERROR("Auto-discovery: Response missing uuid or token field");
        return -1;
    }

    /* Save to configuration file */
    if (autodiscovery_save_config(config) != 0) {
        /* Save failure does not affect in-memory configuration */
        KOMARI_LOG_WARN("Auto-discovery: Failed to save configuration file, using in-memory configuration only");
    }

    KOMARI_LOG_INFO("Auto-discovery: Registration succeeded uuid=%s", config->uuid);
    return 0;
}

int autodiscovery_handle(const char *endpoint,
                          const char *auto_discovery_key,
                          char *token,
                          size_t token_len) {
    if (!endpoint || !auto_discovery_key || !token || token_len == 0) return -1;

    token[0] = '\0';

    /* 1. Try to load existing configuration */
    autodiscovery_config_t config;
    if (autodiscovery_load_config(&config) == 0 && config.token[0] != '\0') {
        /* 2. Configuration exists and token is not empty, reuse directly */
        if (strlen(config.token) >= token_len) {
            KOMARI_LOG_ERROR("Auto-discovery: Token buffer too small");
            return -1;
        }
        strncpy(token, config.token, token_len - 1);
        token[token_len - 1] = '\0';
        KOMARI_LOG_INFO("Auto-discovery: Reusing saved configuration uuid=%s", config.uuid);
        return 0;
    }

    /* 3. Configuration does not exist or token is empty, initiate registration */
    char hostname[256];
    if (utils_get_hostname(hostname, sizeof(hostname)) != 0) {
        KOMARI_LOG_ERROR("Auto-discovery: Failed to get hostname");
        return -1;
    }

    if (autodiscovery_register(endpoint, auto_discovery_key, hostname, &config) != 0) {
        KOMARI_LOG_ERROR("Auto-discovery: Failed to register to panel");
        return -1;
    }

    if (strlen(config.token) >= token_len) {
        KOMARI_LOG_ERROR("Auto-discovery: Token buffer too small");
        return -1;
    }
    strncpy(token, config.token, token_len - 1);
    token[token_len - 1] = '\0';

    return 0;
}
