/*
 * WebSocket client implementation (RFC 6455) with optional TLS support.
 * Handles the opening handshake, frame send/receive, ping/pong keepalive,
 * JSON message dispatch and the v1/v2 protocol fallback mechanism.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/opensslv.h>
#include <pthread.h>

#include "websocket.h"
#include "utils.h"
#include "cJSON.h"
#include "logger.h"
#include "v2.h"
#include "v1.h"

#define WS_BUFFER_SIZE 4096
#define WS_HEADER_SIZE 14
#define WS_MAGIC_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Internal helper: Base64-encode `len` bytes from `data` into `out`. */
static void base64_encode(const unsigned char *data, size_t len, char *out) {
    size_t i, j;
    for (i = 0, j = 0; i < len; i += 3, j += 4) {
        unsigned int n = ((unsigned int)data[i]) << 16;
        if (i + 1 < len) n |= ((unsigned int)data[i + 1]) << 8;
        if (i + 2 < len) n |= data[i + 2];
        
        out[j] = base64_table[(n >> 18) & 0x3F];
        out[j + 1] = base64_table[(n >> 12) & 0x3F];
        out[j + 2] = (i + 1 < len) ? base64_table[(n >> 6) & 0x3F] : '=';
        out[j + 3] = (i + 2 < len) ? base64_table[n & 0x3F] : '=';
    }
    out[j] = '\0';
}

/* Internal helper: generate a random 16-byte WebSocket key, Base64-encoded. */
static int generate_ws_key(char *key, size_t key_len) {
    unsigned char random_bytes[16];
    
    if (RAND_bytes(random_bytes, 16) != 1) {
        return -1;
    }
    
    if (key_len < WS_KEY_LEN) {
        return -1;
    }
    
    base64_encode(random_bytes, 16, key);
    return 0;
}

/* Internal helper: compute the Sec-WebSocket-Accept value from the client key. */
static void compute_accept_key(const char *key, char *accept_key) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    char combined[256];
    
    snprintf(combined, sizeof(combined), "%s%s", key, WS_MAGIC_KEY);
    
    SHA1((unsigned char *)combined, strlen(combined), hash);
    base64_encode(hash, SHA_DIGEST_LENGTH, accept_key);
}

/* Internal helper: parse a ws:// or wss:// URL into scheme/host/port/path components. */
static int parse_url(const char *url, char *scheme, char *host, int *port, char *path) {
    if (strncmp(url, "ws://", 5) == 0) {
        strcpy(scheme, "ws");
        url += 5;
        *port = 80;
    } else if (strncmp(url, "wss://", 6) == 0) {
        strcpy(scheme, "wss");
        url += 6;
        *port = 443;
    } else {
        return -1;
    }
    
    const char *slash = strchr(url, '/');
    const char *colon = strchr(url, ':');
    
    if (colon && (!slash || colon < slash)) {
        size_t host_len = colon - url;
        strncpy(host, url, host_len);
        host[host_len] = '\0';
        *port = atoi(colon + 1);
        url = slash ? slash : "/";
    } else {
        if (slash) {
            size_t host_len = slash - url;
            strncpy(host, url, host_len);
            host[host_len] = '\0';
            url = slash;
        } else {
            strcpy(host, url);
            url = "/";
        }
    }
    
    strcpy(path, url);
    return 0;
}

/* Internal helper: perform the WebSocket opening handshake over the connected socket/TLS stream. */
static int ws_handshake(ws_client_t *client, const char *host, const char *path, const char *token, const char *extra_query) {
    char key[32];
    char accept_key[64];
    char request[2048];
    char response[2048];

    if (generate_ws_key(key, sizeof(key)) != 0) {
        return -1;
    }

    /* Build query string, supporting additional parameters (e.g., terminal session id) */
    char query[256];
    if (extra_query && *extra_query) {
        snprintf(query, sizeof(query), "?token=%s%s", token, extra_query);
    } else {
        snprintf(query, sizeof(query), "?token=%s", token);
    }

    snprintf(request, sizeof(request),
        "GET %s%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "User-Agent: komari-agent-c/1.0\r\n"
        "\r\n",
        path, query, host, key);
    
    ssize_t n;
    if (client->use_tls && client->ssl) {
        n = SSL_write(client->ssl, request, strlen(request));
    } else {
        n = send(client->fd, request, strlen(request), 0);
    }
    if (n < 0) return -1;
    
    n = 0;
    ssize_t total = 0;
    while (total < (ssize_t)sizeof(response) - 1) {
        if (client->use_tls && client->ssl) {
            ssize_t ret = SSL_read(client->ssl, response + total, sizeof(response) - 1 - total);
            if (ret <= 0) break;
            n += ret;
            total += ret;
        } else {
            ssize_t ret = recv(client->fd, response + total, sizeof(response) - 1 - total, 0);
            if (ret <= 0) break;
            n += ret;
            total += ret;
        }
        
        if (strstr(response, "\r\n\r\n") != NULL) {
            break;
        }
    }
    
    if (n <= 0) return -1;
    response[n] = '\0';
    
    if (strstr(response, "101") == NULL) return -1;
    if (strstr(response, "Upgrade") == NULL) return -1;
    if (strstr(response, "websocket") == NULL) return -1;
    
    return 0;
}

/* Internal helper: send a single WebSocket frame with the given opcode and payload. */
static int ws_send_frame(ws_client_t *client, int opcode, const char *data, size_t len) {
    unsigned char frame[WS_HEADER_SIZE + (len > 0 ? len : 0)];
    size_t frame_len = 0;

    frame[0] = 0x80 | (opcode & 0x0F);

    if (len <= 125) {
        frame[1] = len;
        frame_len = 2;
    } else if (len <= 65535) {
        frame[1] = 126;
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        frame_len = 4;
    } else {
        frame[1] = 127;
        for (int i = 0; i < 8; i++) {
            frame[2 + i] = (len >> (56 - i * 8)) & 0xFF;
        }
        frame_len = 10;
    }

    if (data && len > 0) {
        memcpy(frame + frame_len, data, len);
        frame_len += len;
    }
    
    pthread_mutex_lock(&client->send_mutex);
    ssize_t n;
    if (client->use_tls && client->ssl) {
        n = SSL_write(client->ssl, frame, frame_len);
    } else {
        n = send(client->fd, frame, frame_len, 0);
    }
    pthread_mutex_unlock(&client->send_mutex);
    
    return (n == (ssize_t)frame_len) ? 0 : -1;
}

/* Internal helper: receive a single WebSocket frame into `data`, returning the opcode and length. */
static int ws_recv_frame(ws_client_t *client, int *opcode, char *data, size_t *len) {
    unsigned char header[2];
    ssize_t n;
    
    if (client->use_tls && client->ssl) {
        n = SSL_read(client->ssl, header, 2);
    } else {
        n = recv(client->fd, header, 2, 0);
    }
    if (n <= 0) return -1;
    
    *opcode = header[0] & 0x0F;
    int masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;
    
    if (payload_len == 126) {
        unsigned char ext[2];
        if (client->use_tls && client->ssl) {
            n = SSL_read(client->ssl, ext, 2);
        } else {
            n = recv(client->fd, ext, 2, 0);
        }
        if (n <= 0) return -1;
        payload_len = (ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        unsigned char ext[8];
        if (client->use_tls && client->ssl) {
            n = SSL_read(client->ssl, ext, 8);
        } else {
            n = recv(client->fd, ext, 8, 0);
        }
        if (n <= 0) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }
    
    unsigned char mask[4] = {0};
    if (masked) {
        if (client->use_tls && client->ssl) {
            n = SSL_read(client->ssl, mask, 4);
        } else {
            n = recv(client->fd, mask, 4, 0);
        }
        if (n <= 0) return -1;
    }
    
    if (payload_len > *len) payload_len = *len;
    
    if (client->use_tls && client->ssl) {
        n = SSL_read(client->ssl, data, payload_len);
    } else {
        n = recv(client->fd, data, payload_len, 0);
    }
    if (n <= 0) return -1;
    
    if (masked) {
        for (ssize_t i = 0; i < n; i++) {
            data[i] ^= mask[i % 4];
        }
    }
    
    *len = n;
    return 0;
}

/* Internal helper: background receive thread. Reads frames, handles ping/pong,
 * close frames, and dispatches text frames to the JSON or raw handler. */
static void *ws_recv_thread(void *arg) {
    ws_client_t *client = (ws_client_t *)arg;
    char buffer[WS_MAX_MESSAGE_SIZE];
    
    while (!client->should_stop) {
        pthread_mutex_lock(&client->state_mutex);
        bool connected = client->connected;
        pthread_mutex_unlock(&client->state_mutex);
        
        if (!connected) {
            break;
        }
        
        int opcode;
        size_t len = sizeof(buffer);
        
        if (ws_recv_frame(client, &opcode, buffer, &len) == 0) {
            if (opcode == 0x08) {
                pthread_mutex_lock(&client->state_mutex);
                client->connected = false;
                pthread_mutex_unlock(&client->state_mutex);
                break;
            } else if (opcode == 0x09) {
                ws_send_frame(client, 0x0A, buffer, len);
            } else if (opcode == 0x01) {
                buffer[len] = '\0';

                if (client->raw_handler) {
                    /* Raw data mode: pass data directly without JSON parsing (used for terminal sessions) */
                    client->raw_handler(client, buffer, len);
                } else if (client->handler) {
                    /* JSON mode: parse JSON and invoke message handler */
                    ws_message_t msg = {0};

                    /* Parse message with cJSON, correctly handling escape characters and nested JSON */
                    cJSON *root = cJSON_Parse(buffer);
                    if (root) {
                        cJSON *item = NULL;

                        /* Extract message field */
                        if ((item = cJSON_GetObjectItem(root, "message")) && cJSON_IsString(item)) {
                            strncpy(msg.message, item->valuestring, sizeof(msg.message) - 1);
                        }

                        /* Extract terminal_id field (compatible with request_id) */
                        item = cJSON_GetObjectItem(root, "terminal_id");
                        if (!item) item = cJSON_GetObjectItem(root, "request_id");
                        if (item && cJSON_IsString(item)) {
                            strncpy(msg.terminal_id, item->valuestring, sizeof(msg.terminal_id) - 1);
                        }

                        /* Extract exec_command field (compatible with command) */
                        item = cJSON_GetObjectItem(root, "exec_command");
                        if (!item) item = cJSON_GetObjectItem(root, "command");
                        if (item && cJSON_IsString(item)) {
                            strncpy(msg.exec_command, item->valuestring, sizeof(msg.exec_command) - 1);
                        }

                        /* Extract exec_task_id field (compatible with task_id) */
                        item = cJSON_GetObjectItem(root, "exec_task_id");
                        if (!item) item = cJSON_GetObjectItem(root, "task_id");
                        if (item && cJSON_IsString(item)) {
                            strncpy(msg.exec_task_id, item->valuestring, sizeof(msg.exec_task_id) - 1);
                        }

                        /* Extract ping_type field */
                        if ((item = cJSON_GetObjectItem(root, "ping_type")) && cJSON_IsString(item)) {
                            strncpy(msg.ping_type, item->valuestring, sizeof(msg.ping_type) - 1);
                        }

                        /* Extract ping_target field */
                        if ((item = cJSON_GetObjectItem(root, "ping_target")) && cJSON_IsString(item)) {
                            strncpy(msg.ping_target, item->valuestring, sizeof(msg.ping_target) - 1);
                        }

                        /* Extract ping_task_id field */
                        if ((item = cJSON_GetObjectItem(root, "ping_task_id")) && cJSON_IsNumber(item)) {
                            msg.ping_task_id = (uint32_t)item->valuedouble;
                        }

                        cJSON_Delete(root);
                    } else {
                        KOMARI_LOG_WARN("Failed to parse WebSocket JSON message");
                    }

                    client->handler(client, &msg);
                }
            }
        } else {
            pthread_mutex_lock(&client->state_mutex);
            client->connected = false;
            pthread_mutex_unlock(&client->state_mutex);
            break;
        }
    }
    
    return NULL;
}

ws_client_t *ws_client_create(const ws_client_config_t *config) {
    ws_client_t *client = calloc(1, sizeof(ws_client_t));
    if (!client) return NULL;

    client->fd = -1;
    client->connected = false;
    client->should_stop = false;
    client->use_tls = false;
    client->ssl = NULL;
    client->ssl_ctx = NULL;
    client->protocol_version = 2;   /* Use v2 protocol by default */

    if (config) {
        memcpy(&client->config, config, sizeof(ws_client_config_t));
    }

    /* Initialize v2 protocol state */
    if (v2_state_init(&client->v2_state) != 0) {
        free(client);
        return NULL;
    }

    pthread_mutex_init(&client->send_mutex, NULL);
    pthread_mutex_init(&client->state_mutex, NULL);

    return client;
}

void ws_client_destroy(ws_client_t *client) {
    if (!client) return;

    ws_client_disconnect(client);

    if (client->ssl_ctx) {
        SSL_CTX_free(client->ssl_ctx);
        client->ssl_ctx = NULL;
    }

    /* Clean up v2 protocol state */
    v2_state_cleanup(&client->v2_state);

    pthread_mutex_destroy(&client->send_mutex);
    pthread_mutex_destroy(&client->state_mutex);
    free(client);
}

int ws_client_connect(ws_client_t *client) {
    if (!client || !client->config.endpoint) return -1;
    
    char scheme[8], host[256], path[512];
    int port;
    
    if (parse_url(client->config.endpoint, scheme, host, &port, path) != 0) {
        return -1;
    }
    
    client->use_tls = (strcmp(scheme, "wss") == 0);
    
    struct hostent *he = gethostbyname(host);
    if (!he) return -1;
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);
    
    client->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client->fd < 0) return -1;
    
    struct timeval tv = {.tv_sec = 15, .tv_usec = 0};
    setsockopt(client->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    
    if (connect(client->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(client->fd);
        client->fd = -1;
        return -1;
    }
    
    if (client->use_tls) {
        /* OpenSSL version compatibility: 1.0.2 uses SSLv23_client_method, 1.1.0+ uses TLS_client_method */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
        client->ssl_ctx = SSL_CTX_new(SSLv23_client_method());
#else
        client->ssl_ctx = SSL_CTX_new(TLS_client_method());
#endif
        if (!client->ssl_ctx) {
            close(client->fd);
            client->fd = -1;
            return -1;
        }
        
        if (client->config.ignore_cert) {
            SSL_CTX_set_verify(client->ssl_ctx, SSL_VERIFY_NONE, NULL);
        }
        
        client->ssl = SSL_new(client->ssl_ctx);
        if (!client->ssl) {
            SSL_CTX_free(client->ssl_ctx);
            client->ssl_ctx = NULL;
            close(client->fd);
            client->fd = -1;
            return -1;
        }
        
        SSL_set_fd(client->ssl, client->fd);
        SSL_set_tlsext_host_name(client->ssl, host);
        
        if (SSL_connect(client->ssl) <= 0) {
            SSL_free(client->ssl);
            client->ssl = NULL;
            SSL_CTX_free(client->ssl_ctx);
            client->ssl_ctx = NULL;
            close(client->fd);
            client->fd = -1;
            return -1;
        }
    }
    
    if (ws_handshake(client, host, path, client->config.token, client->config.extra_query) != 0) {
        if (client->ssl) {
            SSL_shutdown(client->ssl);
            SSL_free(client->ssl);
            client->ssl = NULL;
        }
        if (client->ssl_ctx) {
            SSL_CTX_free(client->ssl_ctx);
            client->ssl_ctx = NULL;
        }
        close(client->fd);
        client->fd = -1;
        return -1;
    }
    
    pthread_mutex_lock(&client->state_mutex);
    client->connected = true;
    pthread_mutex_unlock(&client->state_mutex);
    
    if (pthread_create(&client->recv_thread, NULL, ws_recv_thread, client) != 0) {
        if (client->ssl) {
            SSL_shutdown(client->ssl);
            SSL_free(client->ssl);
            client->ssl = NULL;
        }
        if (client->ssl_ctx) {
            SSL_CTX_free(client->ssl_ctx);
            client->ssl_ctx = NULL;
        }
        close(client->fd);
        client->fd = -1;
        pthread_mutex_lock(&client->state_mutex);
        client->connected = false;
        pthread_mutex_unlock(&client->state_mutex);
        return -1;
    }

    /* Connection established successfully, record protocol attempt result (reset failure count, upgrade back to v2 if needed) */
    ws_client_note_protocol_result(client, true);

    return 0;
}

void ws_client_disconnect(ws_client_t *client) {
    if (!client) return;
    
    client->should_stop = true;
    
    pthread_mutex_lock(&client->state_mutex);
    client->connected = false;
    pthread_mutex_unlock(&client->state_mutex);
    
    if (client->fd >= 0) {
        if (client->ssl) {
            SSL_shutdown(client->ssl);
        }
        shutdown(client->fd, SHUT_RDWR);
        close(client->fd);
        client->fd = -1;
    }
    
    if (client->ssl) {
        SSL_free(client->ssl);
        client->ssl = NULL;
    }
    
    if (client->recv_thread) {
        pthread_join(client->recv_thread, NULL);
        client->recv_thread = 0;
    }
}

int ws_client_send_text(ws_client_t *client, const char *data, size_t len) {
    if (!client || !client->connected || !data) return -1;
    return ws_send_frame(client, 0x01, data, len);
}

int ws_client_send_ping(ws_client_t *client) {
    if (!client || !client->connected) return -1;
    return ws_send_frame(client, 0x09, NULL, 0);
}

void ws_client_set_handler(ws_client_t *client, ws_message_handler_t handler) {
    if (client) client->handler = handler;
}

void ws_client_set_raw_handler(ws_client_t *client, ws_raw_handler_t handler) {
    if (client) client->raw_handler = handler;
}

void ws_client_set_user_data(ws_client_t *client, void *data) {
    if (client) client->user_data = data;
}

void ws_client_stop(ws_client_t *client) {
    if (client) client->should_stop = true;
}

/* ================ Protocol fallback mechanism implementation ================ */

int ws_client_get_protocol_version(ws_client_t *client) {
    if (!client) return 1;
    return client->protocol_version;
}

bool ws_client_should_use_v2(ws_client_t *client) {
    if (!client) return false;
    /* Fall back to v1 when consecutive v2 failures reach threshold (3 times) */
    if (v2_should_fallback_to_v1(&client->v2_state)) {
        return false;
    }
    return true;
}

void ws_client_note_protocol_result(ws_client_t *client, bool success) {
    if (!client) return;

    if (success) {
        /* Success: reset failure count and automatically upgrade back to v2 */
        v2_note_attempt_result(&client->v2_state, 1);
        if (client->protocol_version != 2) {
            KOMARI_LOG_INFO("[Protocol] Upgrading to v2 after successful connection");
            client->protocol_version = 2;
        }
    } else {
        /* Failure: increment failure count */
        int fail_count = v2_note_attempt_result(&client->v2_state, 0);
        KOMARI_LOG_DEBUG("[Protocol] v2 attempt failed (%d/%d)",
                         fail_count, V2_FALLBACK_THRESHOLD);

        /* Fall back to v1 when failure count reaches threshold */
        if (v2_should_fallback_to_v1(&client->v2_state)) {
            if (client->protocol_version != 1) {
                KOMARI_LOG_WARN("[Protocol] v2 failed %d times, falling back to v1",
                                V2_FALLBACK_THRESHOLD);
                client->protocol_version = 1;
            }
        }
    }
}
