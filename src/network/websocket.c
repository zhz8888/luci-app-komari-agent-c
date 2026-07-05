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
#include <ctype.h>
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
#include "jsonrpc.h"

#define WS_BUFFER_SIZE 4096
#define WS_HEADER_SIZE 14
#define WS_MAGIC_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
/* Maximum length of a raw (unencoded) authentication token. The URL-encoded
 * form may be up to 3x this size (every byte becomes "%XX"), so query buffer
 * sizing must account for that expansion. */
#define TOKEN_MAX_LEN 256

/* Connect/send/recv timeout for the WebSocket transport (MIN-34: extracted
 * magic number). Slightly longer than the HTTP timeout so a slow TLS handshake
 * does not abort before the server has a chance to respond. */
#define WS_CONNECT_TIMEOUT_SEC 15

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

/* Internal helper: percent-encode a string for safe inclusion in a URL query
 * component (MAJ-08). Mirrors the behavior of the url_encode helpers in
 * src/autodiscovery/autodiscovery.c and src/report/report.c for consistency.
 *
 * Unreserved characters per RFC 3986 (A-Z, a-z, 0-9, '-', '_', '.', '~') are
 * copied as-is. All other bytes (including space, '&', '=', '+') are encoded
 * as "%XX" with uppercase hex digits. This prevents special characters in a
 * token from breaking out of the query parameter or injecting additional
 * parameters.
 *
 * Returns the number of bytes written (excluding NUL) on success, -1 if the
 * output buffer is too small or arguments are invalid. */
static int url_encode(const char *in, char *out, size_t out_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t i, j;

    if (!in || !out || out_size == 0) return -1;

    for (i = 0, j = 0; in[i] != '\0'; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            if (j + 1 >= out_size) return -1;
            out[j++] = (char)c;
        } else {
            if (j + 3 >= out_size) return -1;
            out[j++] = '%';
            out[j++] = hex[(c >> 4) & 0x0F];
            out[j++] = hex[c & 0x0F];
        }
    }
    out[j] = '\0';
    return (int)j;
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

/* Internal helper: compute the Sec-WebSocket-Accept value from the client key.
 *
 * Per RFC 6455 §4.2.2, the server must return Base64(SHA1(key || GUID)). This
 * helper computes that value so ws_handshake can verify the server's response
 * (MAJ-07). Returns 0 on success, -1 on snprintf truncation or if the output
 * buffer is too small to hold the Base64-encoded SHA1 (28 chars + NUL). */
static int compute_accept_key(const char *key, char *accept_key, size_t accept_key_size) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    char combined[256];
    int ret;

    /* Base64 of SHA1 (20 bytes) is 28 chars; require room for NUL as well. */
    if (accept_key_size < 29) return -1;

    ret = snprintf(combined, sizeof(combined), "%s%s", key, WS_MAGIC_KEY);
    if (ret < 0 || (size_t)ret >= sizeof(combined)) {
        /* Truncation would produce a wrong SHA1 and break the handshake
         * verification (MAJ-11). */
        return -1;
    }

    SHA1((unsigned char *)combined, strlen(combined), hash);
    base64_encode(hash, SHA_DIGEST_LENGTH, accept_key);
    return 0;
}

/* Internal helper: parse a ws:// or wss:// URL into scheme/host/port/path components.
 * Output buffers must be at least SCHEME_MAX/HOST_MAX/PATH_MAX bytes respectively. */
#define WS_SCHEME_MAX 8
#define WS_HOST_MAX 256
#define WS_PATH_MAX 512
static int parse_url(const char *url, char *scheme, char *host, int *port, char *path) {
    int sn_ret;
    if (strncmp(url, "ws://", 5) == 0) {
        sn_ret = snprintf(scheme, WS_SCHEME_MAX, "ws");
        if (sn_ret < 0 || (size_t)sn_ret >= WS_SCHEME_MAX) return -1;
        url += 5;
        *port = 80;
    } else if (strncmp(url, "wss://", 6) == 0) {
        sn_ret = snprintf(scheme, WS_SCHEME_MAX, "wss");
        if (sn_ret < 0 || (size_t)sn_ret >= WS_SCHEME_MAX) return -1;
        url += 6;
        *port = 443;
    } else {
        return -1;
    }

    const char *slash = strchr(url, '/');
    const char *colon = strchr(url, ':');

    if (colon && (!slash || colon < slash)) {
        size_t host_len = colon - url;
        if (host_len >= WS_HOST_MAX) return -1;
        sn_ret = snprintf(host, WS_HOST_MAX, "%.*s", (int)host_len, url);
        if (sn_ret < 0 || (size_t)sn_ret >= WS_HOST_MAX) return -1;
        /* Parse port with strtol and validate range (atoi gives no error checking). */
        char *endp = NULL;
        long port_val = strtol(colon + 1, &endp, 10);
        if (endp == colon + 1 || port_val < 1 || port_val > 65535) {
            return -1;
        }
        /* Port may be followed by '/' (path) or end of string; reject any other suffix. */
        if (*endp != '\0' && *endp != '/') {
            return -1;
        }
        *port = (int)port_val;
        url = slash ? slash : "/";
    } else {
        if (slash) {
            size_t host_len = slash - url;
            if (host_len >= WS_HOST_MAX) return -1;
            sn_ret = snprintf(host, WS_HOST_MAX, "%.*s", (int)host_len, url);
            if (sn_ret < 0 || (size_t)sn_ret >= WS_HOST_MAX) return -1;
            url = slash;
        } else {
            if (strlen(url) >= WS_HOST_MAX) return -1;
            sn_ret = snprintf(host, WS_HOST_MAX, "%s", url);
            if (sn_ret < 0 || (size_t)sn_ret >= WS_HOST_MAX) return -1;
            url = "/";
        }
    }

    if (strlen(url) >= WS_PATH_MAX) return -1;
    sn_ret = snprintf(path, WS_PATH_MAX, "%s", url);
    if (sn_ret < 0 || (size_t)sn_ret >= WS_PATH_MAX) return -1;
    return 0;
}

/* Internal helper: extract the value of an HTTP header from a response buffer.
 * Performs a case-insensitive match on the header name (RFC 7230 §3.2). The
 * value is trimmed of leading/trailing inline whitespace and copied into `out`
 * (NUL-terminated). Used by ws_handshake to read Sec-WebSocket-Accept (MAJ-07).
 * Returns 0 on success, -1 if the header is not found, the buffer is too small,
 * or the response is malformed. */
static int ws_extract_header(const char *response, const char *header_name,
                             char *out, size_t out_size) {
    const char *line;
    const char *first_eol;
    const char *eol;
    const char *v;
    size_t name_len;
    size_t i;
    size_t vlen;

    if (!response || !header_name || !out || out_size == 0) return -1;
    name_len = strlen(header_name);
    if (name_len == 0) return -1;

    /* Skip the status line so we only scan headers. */
    first_eol = strstr(response, "\r\n");
    if (!first_eol) return -1;
    line = first_eol + 2;

    while (*line) {
        /* End of headers: blank line. */
        if (line[0] == '\r' && line[1] == '\n') return -1;
        if (line[0] == '\n') return -1;

        /* Compare header name case-insensitively. */
        for (i = 0; i < name_len; i++) {
            if (line[i] == '\0' || line[i] == '\r' || line[i] == '\n') break;
            if (tolower((unsigned char)line[i]) != tolower((unsigned char)header_name[i])) {
                break;
            }
        }

        if (i == name_len && line[name_len] == ':') {
            /* Found the header; locate and trim the value. */
            v = line + name_len + 1;
            while (*v == ' ' || *v == '\t') v++;
            eol = strstr(v, "\r\n");
            if (!eol) eol = v + strlen(v);
            while (eol > v && (eol[-1] == ' ' || eol[-1] == '\t')) eol--;
            vlen = (size_t)(eol - v);
            if (vlen + 1 > out_size) return -1;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return 0;
        }

        /* Move to next line. */
        eol = strstr(line, "\r\n");
        if (!eol) return -1;
        line = eol + 2;
    }
    return -1;
}

/* Internal helper: perform the WebSocket opening handshake over the connected socket/TLS stream. */
static int ws_handshake(ws_client_t *client, const char *host, const char *path, const char *token, const char *extra_query) {
    char key[32];
    char expected_accept[64];
    char encoded_token[TOKEN_MAX_LEN * 3 + 1];
    char request[2048];
    char response[2048];
    int sn_ret;

    /* Reset any leftover pending bytes from a previous connection so stale
     * data cannot leak into this session (MAJ-24). The recv thread is not
     * running yet at this point, so the write is race-free. */
    client->pending_len = 0;
    client->pending_off = 0;

    if (generate_ws_key(key, sizeof(key)) != 0) {
        return -1;
    }

    /* Compute the expected Sec-WebSocket-Accept value so we can verify the
     * server's response (RFC 6455 §4.2.2, MAJ-07). A non-compliant or
     * malicious intermediary cannot complete the handshake without producing
     * the correct SHA1+Base64 of (client-key || magic GUID). */
    if (compute_accept_key(key, expected_accept, sizeof(expected_accept)) != 0) {
        return -1;
    }

    /*
     * Design note: The token is passed via URL query string (e.g., "?token=xxx")
     * rather than an Authorization header. This is intentional and matches the
     * Go reference implementation (see .komari-agent-main/server/websocket.go),
     * where all client-facing WebSocket endpoints (/api/clients/report,
     * /api/clients/v2/rpc, /api/clients/terminal) accept the token as a query
     * parameter. The server side already supports this auth scheme, so changing
     * to a header-based approach would break compatibility.
     */
    /* URL-encode the token before placing it in the query string so that
     * special characters (e.g., '&', '=', '+', space) cannot break out of
     * the query parameter or inject additional parameters (MAJ-08). The
     * encoded form may be up to 3x the original length ("%XX" per byte). */
    if (url_encode(token, encoded_token, sizeof(encoded_token)) < 0) {
        KOMARI_LOG_WARN("WebSocket token too long to URL-encode (max %d bytes)",
                        TOKEN_MAX_LEN);
        return -1;
    }

    /* Build query string, supporting additional parameters (e.g., terminal session id).
     * The buffer must accommodate "?token=" + URL-encoded token (up to
     * TOKEN_MAX_LEN*3 bytes) + extra_query + NUL. extra_query is treated as
     * already-encoded by the caller. */
    char query[TOKEN_MAX_LEN * 3 + 256];
    if (extra_query && *extra_query) {
        sn_ret = snprintf(query, sizeof(query), "?token=%s%s",
                          encoded_token, extra_query);
    } else {
        sn_ret = snprintf(query, sizeof(query), "?token=%s", encoded_token);
    }
    if (sn_ret < 0 || (size_t)sn_ret >= sizeof(query)) {
        KOMARI_LOG_WARN("WebSocket query string truncated (needed %d, had %zu)",
                        sn_ret < 0 ? -1 : sn_ret + 1, sizeof(query));
        return -1;
    }

    sn_ret = snprintf(request, sizeof(request),
        "GET %s%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "User-Agent: komari-agent-c/1.0\r\n"
        "\r\n",
        path, query, host, key);
    if (sn_ret < 0 || (size_t)sn_ret >= sizeof(request)) {
        KOMARI_LOG_WARN("WebSocket handshake request truncated (needed %d, had %zu)",
                        sn_ret < 0 ? -1 : sn_ret + 1, sizeof(request));
        return -1;
    }

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

        /* NUL-terminate before strstr so we never scan uninitialized bytes */
        response[total] = '\0';
        if (strstr(response, "\r\n\r\n") != NULL) {
            break;
        }
    }

    if (n <= 0) return -1;
    response[total] = '\0';

    /* Locate the end of the HTTP headers (MAJ-24). The server may start
     * sending WebSocket frames immediately after the 101 response, and the
     * recv loop above may have pulled those frame bytes into `response`
     * together with the headers. Save any bytes past the trailing CRLFCRLF
     * into client->pending_buf so the recv thread can consume them via
     * read_full() instead of losing them. */
    char *header_end = strstr(response, "\r\n\r\n");
    if (header_end) {
        size_t header_bytes = (size_t)(header_end - response) + 4;  /* include CRLFCRLF */
        size_t extra = (size_t)total > header_bytes
                         ? (size_t)total - header_bytes
                         : 0;
        if (extra > 0) {
            if (extra > WS_PENDING_BUF_SIZE) {
                /* Should be impossible: response and pending_buf are the same
                 * size (2048 bytes), so extra can never exceed the buffer.
                 * Guard defensively anyway to avoid a silent truncation. */
                KOMARI_LOG_WARN("WebSocket handshake overflow: %zu trailing bytes "
                                "exceed pending buffer %d", extra, WS_PENDING_BUF_SIZE);
                return -1;
            }
            memcpy(client->pending_buf, response + header_bytes, extra);
            client->pending_len = extra;
            client->pending_off = 0;
        }
    }

    /* Validate the status line strictly rather than searching for "101" anywhere */
    if (strncmp(response, "HTTP/1.1 101", 12) != 0) return -1;
    if (strstr(response, "Upgrade") == NULL) return -1;
    if (strstr(response, "websocket") == NULL) return -1;

    /* Verify the Sec-WebSocket-Accept header matches the expected value
     * (RFC 6455 §4.2.2, MAJ-07). Without this check a non-WebSocket responder
     * (e.g., a transparent proxy returning a 101 with no real upgrade) could
     * trick the client into treating the stream as a WebSocket. */
    char server_accept[128];
    if (ws_extract_header(response, "Sec-WebSocket-Accept:",
                          server_accept, sizeof(server_accept)) != 0) {
        KOMARI_LOG_WARN("WebSocket handshake missing Sec-WebSocket-Accept header");
        return -1;
    }
    if (strcmp(server_accept, expected_accept) != 0) {
        KOMARI_LOG_WARN("WebSocket Sec-WebSocket-Accept mismatch (expected %s, got %s)",
                        expected_accept, server_accept);
        return -1;
    }

    return 0;
}

/* Internal helper: send exactly `len` bytes over the socket/TLS stream, looping
 * over send()/SSL_write() to handle partial writes (MAJ-16 / T14.1). Mirrors
 * the send_full helpers in src/report/report.c and src/autodiscovery/autodiscovery.c.
 * Returns 0 on success (all bytes sent), -1 on error or connection-closed.
 * EINTR from send() is retried; SSL_write errors are surfaced as failures. */
static int send_full(ws_client_t *client, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n;
        if (client->use_tls && client->ssl) {
            n = SSL_write(client->ssl, data + sent, (int)(len - sent));
        } else {
            n = (int)send(client->fd, data + sent, len - sent, 0);
        }
        if (n <= 0) {
            /* Retry on signal interruption (only meaningful for plain send();
             * SSL_write surfaces EINTR indirectly via its own error stack, but
             * checking errno is consistent with the existing helpers). */
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

/* Internal helper: send a single WebSocket frame with the given opcode and payload.
 * Per RFC 6455 §5.1, all frames sent from a client to the server MUST be masked
 * with a 4-byte masking key chosen by the client. */
static int ws_send_frame(ws_client_t *client, int opcode, const char *data, size_t len) {
    /* WS_HEADER_SIZE (14) already accounts for 2-byte basic header + 8-byte extended
     * length + 4-byte mask key, which is the worst case. */
    unsigned char frame[WS_HEADER_SIZE + (len > 0 ? len : 0)];
    size_t frame_len = 0;
    unsigned char mask[4];

    /* Generate a 4-byte random mask key. Prefer RAND_bytes for cryptographic
     * randomness; fall back to rand() if it fails (still satisfies RFC 6455
     * which only requires unpredictable-to-server randomness). */
    if (RAND_bytes(mask, 4) != 1) {
        for (int i = 0; i < 4; i++) {
            mask[i] = (unsigned char)rand();
        }
    }

    frame[0] = 0x80 | (opcode & 0x0F);

    if (len <= 125) {
        /* Set mask bit (0x80) and 7-bit payload length */
        frame[1] = 0x80 | (unsigned char)len;
        frame_len = 2;
    } else if (len <= 65535) {
        frame[1] = 0x80 | 126;
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        frame_len = 4;
    } else {
        frame[1] = 0x80 | 127;
        for (int i = 0; i < 8; i++) {
            frame[2 + i] = (len >> (56 - i * 8)) & 0xFF;
        }
        frame_len = 10;
    }

    /* Append the 4-byte mask key right after the (possibly extended) length */
    memcpy(frame + frame_len, mask, 4);
    frame_len += 4;

    /* Copy payload and apply the mask: data[i] ^= mask[i % 4] */
    if (data && len > 0) {
        for (size_t i = 0; i < len; i++) {
            frame[frame_len + i] = (unsigned char)data[i] ^ mask[i % 4];
        }
        frame_len += len;
    }

    pthread_mutex_lock(&client->send_mutex);
    /* Use send_full to handle partial writes from send()/SSL_write() and to
     * retry on EINTR (MAJ-16 / T14.1). The mutex is held across the entire
     * send so frames from concurrent callers are not interleaved. */
    int rc = send_full(client, (const char *)frame, frame_len);
    pthread_mutex_unlock(&client->send_mutex);

    return rc;
}

/* Internal helper: read exactly `len` bytes from the socket/TLS stream.
 * Loops over recv()/SSL_read() to handle partial reads. Returns 0 on success
 * (all bytes read), or -1 on error/connection-closed before the full length.
 * Bytes left over from the HTTP handshake (MAJ-24) are drained from
 * client->pending_buf first, before any recv()/SSL_read() call is issued. */
static int read_full(ws_client_t *client, void *buf, size_t len) {
    size_t got = 0;
    char *p = (char *)buf;

    /* Drain bytes saved by ws_handshake first so we don't lose data that
     * was already pulled off the socket together with the 101 response. */
    while (got < len && client->pending_off < client->pending_len) {
        p[got++] = (char)client->pending_buf[client->pending_off++];
    }

    while (got < len) {
        ssize_t ret;
        if (client->use_tls && client->ssl) {
            ret = SSL_read(client->ssl, p + got, len - got);
        } else {
            ret = recv(client->fd, p + got, len - got, 0);
        }
        if (ret <= 0) {
            /* Retry on signal interruption (only meaningful for plain recv()). */
            if (ret < 0 && errno == EINTR) continue;
            return -1;
        }
        got += (size_t)ret;
    }
    return 0;
}

/* Internal helper: receive a single WebSocket frame into `data`, returning the
 * opcode, FIN flag and length. Per RFC 6455 §5.4 the caller is responsible for
 * assembling fragmented messages; this function does not reject continuation
 * frames (opcode 0x00) or non-final frames (fin=0).
 *
 * Protocol-validity checks performed here (RFC 6455 §5.2 / §5.5):
 *   - MIN-56: RSV1/RSV2/RSV3 bits must be zero (no extensions negotiated).
 *   - MIN-57: Reserved opcodes (0x3-0x7, 0xB-0xF) are rejected.
 *   - MIN-55: Control frame payload must be <= 125 bytes and FIN must be set.
 *   - MAJ-17: Payload length exceeding the caller's buffer is an error
 *     (previously the data was silently truncated). */
static int ws_recv_frame(ws_client_t *client, int *opcode, int *fin, char *data, size_t *len) {
    unsigned char header[2];

    if (read_full(client, header, 2) != 0) return -1;

    *fin = (header[0] & 0x80) != 0;
    *opcode = header[0] & 0x0F;

    /* MIN-56: RSV1/RSV2/RSV3 (bits 6/5/4 of byte 0) must be zero unless an
     * extension was negotiated during the handshake. We never negotiate
     * extensions, so any non-zero reserved bit is a protocol error. */
    if (header[0] & 0x70) {
        KOMARI_LOG_WARN("WebSocket protocol error: RSV bits non-zero (byte0=0x%02x)",
                        header[0]);
        return -1;
    }

    /* MIN-57: Reject reserved opcodes 0x3-0x7 (data) and 0xB-0xF (control)
     * per RFC 6455 §5.2. The remaining opcodes (0x0 continuation, 0x1 text,
     * 0x2 binary, 0x8 close, 0x9 ping, 0xA pong) are valid. */
    int op = *opcode;
    if ((op >= 0x03 && op <= 0x07) || (op >= 0x0B && op <= 0x0F)) {
        KOMARI_LOG_WARN("WebSocket protocol error: reserved opcode 0x%x", op);
        return -1;
    }

    int masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;

    if (payload_len == 126) {
        unsigned char ext[2];
        if (read_full(client, ext, 2) != 0) return -1;
        payload_len = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (payload_len == 127) {
        unsigned char ext[8];
        if (read_full(client, ext, 8) != 0) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }

    /* MIN-55: Control frames (opcodes 0x8-0xA) MUST NOT be fragmented and
     * their payload MUST fit in the 7-bit basic length field (<= 125 bytes).
     * RFC 6455 §5.5. */
    if (op >= 0x08 && op <= 0x0A) {
        if (payload_len > 125) {
            KOMARI_LOG_WARN("WebSocket protocol error: control frame (opcode 0x%x) "
                            "payload %llu exceeds 125 bytes", op,
                            (unsigned long long)payload_len);
            return -1;
        }
        if (!*fin) {
            KOMARI_LOG_WARN("WebSocket protocol error: control frame (opcode 0x%x) "
                            "fragmented (FIN=0)", op);
            return -1;
        }
    }

    unsigned char mask[4] = {0};
    if (masked) {
        if (read_full(client, mask, 4) != 0) return -1;
    }

    /* MAJ-17 / T14.2: A payload that does not fit in the caller's buffer is a
     * protocol error. Previously the data was silently truncated, which could
     * cause the caller to process a partial message as if it were complete.
     * Returning -1 forces the recv thread to close the connection so the
     * caller can reconnect with a clean state. */
    if (payload_len > (uint64_t)*len) {
        KOMARI_LOG_WARN("WebSocket frame payload %llu bytes exceeds buffer %zu",
                        (unsigned long long)payload_len, *len);
        return -1;
    }

    if (read_full(client, data, (size_t)payload_len) != 0) return -1;

    if (masked) {
        for (size_t i = 0; i < (size_t)payload_len; i++) {
            data[i] ^= mask[i % 4];
        }
    }

    *len = (size_t)payload_len;
    return 0;
}

/* Internal helper: background receive thread. Reads frames, handles ping/pong,
 * close frames, and dispatches text frames to the JSON or raw handler. */
static void *ws_recv_thread(void *arg) {
    ws_client_t *client = (ws_client_t *)arg;
    /* +1 byte so that buffer[len] = '\0' is safe even when len == WS_MAX_MESSAGE_SIZE */
    char buffer[WS_MAX_MESSAGE_SIZE + 1];

    /* Reset any leftover fragment state from a previous connection so the
     * first frame of this session is treated as a fresh message. Both
     * fragment_len and fragment_opcode must be cleared: fragment_opcode
     * doubles as the "fragment in progress" flag (0 = idle, 0x01/0x02 =
     * accumulating) so it must be reset to handle empty-payload first
     * fragments correctly. */
    client->fragment_len = 0;
    client->fragment_opcode = 0;

    while (1) {
        pthread_mutex_lock(&client->state_mutex);
        bool should_stop = client->should_stop;
        bool connected = client->connected;
        pthread_mutex_unlock(&client->state_mutex);

        if (should_stop || !connected) {
            break;
        }

        int opcode;
        int fin;
        size_t len = WS_MAX_MESSAGE_SIZE;

        if (ws_recv_frame(client, &opcode, &fin, buffer, &len) != 0) {
            /* Receive failure: record protocol result so repeated v2 failures
             * trigger fallback to v1 (M-8). This must be called before the
             * thread exits, as the recv thread is the only consumer of
             * incoming frames. */
            ws_client_note_protocol_result(client, false);
            pthread_mutex_lock(&client->state_mutex);
            client->connected = false;
            pthread_mutex_unlock(&client->state_mutex);
            break;
        }

        /* Control frames may be interleaved with fragments (RFC 6455 §5.4).
         * Handle them directly without touching the fragment accumulation
         * state so a ping/pong/close mid-message does not corrupt assembly. */
        if (opcode == 0x08) {
            /* Close frame: mark connection as closed and exit the loop */
            pthread_mutex_lock(&client->state_mutex);
            client->connected = false;
            pthread_mutex_unlock(&client->state_mutex);
            break;
        } else if (opcode == 0x09) {
            /* Ping frame: reply with a pong carrying the same payload */
            ws_send_frame(client, 0x0A, buffer, len);
            continue;
        } else if (opcode == 0x0A) {
            /* Pong frame: ignore */
            continue;
        }

        /* Non-control frame: feed into the fragment accumulation state machine */
        char *msg_data = NULL;
        size_t msg_len = 0;
        int msg_opcode = 0;
        int r = ws_fragment_accumulate(client, opcode, fin, buffer, len,
                                       &msg_data, &msg_len, &msg_opcode);
        if (r < 0) {
            /* Oversize, allocation failure or protocol error: close the
             * connection so the caller can reconnect with a clean state. */
            pthread_mutex_lock(&client->state_mutex);
            client->connected = false;
            pthread_mutex_unlock(&client->state_mutex);
            break;
        }
        if (r == 0) {
            /* Fragment accumulated, waiting for more frames */
            continue;
        }

        /* Message complete: dispatch to the appropriate handler */
        if (msg_opcode == 0x01) {
            /* Text frame: NUL-terminate so cJSON_Parse sees a proper C string.
             * Both `buffer` and `fragment_buf` have at least one extra byte
             * reserved for the terminator (see buffer declaration above and
             * the +1 capacity guarantee in ws_fragment_accumulate). */
            msg_data[msg_len] = '\0';

            if (client->raw_handler) {
                /* Raw data mode: pass data directly without JSON parsing
                 * (used for terminal sessions) */
                client->raw_handler(client, msg_data, msg_len);
            } else if (client->handler) {
                /* JSON mode: parse JSON and dispatch to the appropriate handler.
                 * v2 JSON-RPC events (jsonrpc == "2.0" with a method field) are
                 * routed to ws_handle_v2_event for dedup + method dispatch + ACK
                 * accumulation; v1 messages fall through to the legacy field
                 * extraction path. */
                cJSON *root = cJSON_Parse(msg_data);
                if (root) {
                    cJSON *jsonrpc = cJSON_GetObjectItem(root, "jsonrpc");
                    cJSON *method = cJSON_GetObjectItem(root, "method");
                    if (jsonrpc && cJSON_IsString(jsonrpc) &&
                        strcmp(jsonrpc->valuestring, JSONRPC_VERSION) == 0 &&
                        method && cJSON_IsString(method)) {
                        /* v2 JSON-RPC event: dispatch to the v2 handler which
                         * handles dedup, method-based dispatch and ACK
                         * accumulation. ws_handle_v2_event re-parses the JSON
                         * internally via jsonrpc_parse_event, so free root
                         * here to avoid a leak. */
                        cJSON_Delete(root);
                        ws_handle_v2_event(client, msg_data);
                    } else {
                        /* v1 message: extract fields and invoke handler */
                        ws_message_t msg = {0};
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
                        client->handler(client, &msg);
                    }
                } else {
                    KOMARI_LOG_WARN("Failed to parse WebSocket JSON message");
                    /* Preserve original behavior: call handler with an empty
                     * message so the handler can still run its default path. */
                    ws_message_t msg = {0};
                    client->handler(client, &msg);
                }
            }
        } else if (msg_opcode == 0x02) {
            /* Binary frame: dispatch to raw handler if registered */
            if (client->raw_handler) {
                client->raw_handler(client, msg_data, msg_len);
            }
        }
    }

    return NULL;
}

/* Accumulate a WebSocket fragment into the client's fragment buffer.
 * Implements RFC 6455 §5.4 message fragmentation:
 *   - Unfragmented message (FIN=1, opcode 0x01/0x02): returned directly.
 *   - First fragment (FIN=0, opcode 0x01/0x02): starts a new accumulation.
 *   - Continuation (opcode 0x00): appends to the running buffer.
 *   - Final continuation (FIN=1, opcode 0x00): completes the message.
 * Returns 0 when more fragments are needed, 1 when the message is complete
 * (out/out_len/out_opcode are populated), or -1 on error. */
int ws_fragment_accumulate(ws_client_t *client, int opcode, int fin,
                           char *data, size_t len,
                           char **out, size_t *out_len, int *out_opcode) {
    if (!client || !data || !out || !out_len || !out_opcode) {
        return -1;
    }

    /* Unfragmented message: FIN=1 with a non-continuation opcode. If a
     * fragment was in progress, this is a protocol error (RFC 6455 §5.4).
     * fragment_opcode (non-zero) is the authoritative "in progress" flag so
     * that empty-payload first fragments are detected correctly. */
    if (fin && opcode != 0x00) {
        if (client->fragment_opcode != 0) {
            KOMARI_LOG_WARN("WebSocket protocol error: non-continuation frame "
                            "(opcode=%d) received while fragment in progress", opcode);
            client->fragment_len = 0;
            client->fragment_opcode = 0;
            return -1;
        }
        *out = data;
        *out_len = len;
        *out_opcode = opcode;
        return 1;
    }

    /* First fragment: FIN=0 with a text/binary opcode starts a new message */
    if (!fin && (opcode == 0x01 || opcode == 0x02)) {
        if (client->fragment_opcode != 0) {
            KOMARI_LOG_WARN("WebSocket protocol error: new fragment (opcode=%d) "
                            "started while previous fragment in progress", opcode);
            client->fragment_len = 0;
            client->fragment_opcode = 0;
            return -1;
        }

        /* Enforce the maximum accumulated size even on the first fragment to
         * avoid allocating oversized buffers. */
        if (len > WS_FRAGMENT_MAX_SIZE) {
            KOMARI_LOG_WARN("WebSocket fragment first frame exceeds %d bytes",
                            WS_FRAGMENT_MAX_SIZE);
            return -1;
        }

        /* Allocate (or reuse) the accumulation buffer. Use a 4 KB initial
         * capacity to avoid frequent reallocs for small messages. Always
         * reserve +1 byte for a NUL terminator that the caller may write. */
        size_t needed = len + 1;
        size_t cap = needed > 4096 ? needed : 4096;
        if (client->fragment_buf == NULL || client->fragment_capacity < cap) {
            char *new_buf = realloc(client->fragment_buf, cap);
            if (!new_buf) {
                return -1;
            }
            client->fragment_buf = new_buf;
            client->fragment_capacity = cap;
        }
        memcpy(client->fragment_buf, data, len);
        client->fragment_len = len;
        client->fragment_opcode = opcode;
        return 0;
    }

    /* Continuation frame: opcode == 0x00 */
    if (opcode == 0x00) {
        if (client->fragment_opcode == 0) {
            KOMARI_LOG_WARN("WebSocket protocol error: continuation frame "
                            "received without a starting fragment");
            return -1;
        }

        /* Reject oversized accumulated messages before appending. This keeps
         * memory usage bounded by WS_FRAGMENT_MAX_SIZE. */
        if (client->fragment_len + len > WS_FRAGMENT_MAX_SIZE) {
            KOMARI_LOG_WARN("WebSocket fragment accumulation exceeded %d bytes, "
                            "closing connection", WS_FRAGMENT_MAX_SIZE);
            client->fragment_len = 0;
            client->fragment_opcode = 0;
            return -1;
        }

        /* Grow the buffer if necessary. Geometric growth (doubling) keeps
         * amortized realloc cost O(1) while still bounding peak usage. */
        size_t needed = client->fragment_len + len + 1;
        if (client->fragment_capacity < needed) {
            size_t new_cap = client->fragment_capacity * 2;
            if (new_cap < needed) new_cap = needed;
            char *new_buf = realloc(client->fragment_buf, new_cap);
            if (!new_buf) {
                client->fragment_len = 0;
                client->fragment_opcode = 0;
                return -1;
            }
            client->fragment_buf = new_buf;
            client->fragment_capacity = new_cap;
        }
        memcpy(client->fragment_buf + client->fragment_len, data, len);
        client->fragment_len += len;

        if (fin) {
            /* Final fragment: hand the assembled message back to the caller.
             * Both fragment_len and fragment_opcode are reset so the buffer
             * can be reused for the next message; fragment_buf itself stays
             * allocated to avoid a malloc/free churn on the next message. */
            *out = client->fragment_buf;
            *out_len = client->fragment_len;
            *out_opcode = client->fragment_opcode;
            client->fragment_len = 0;
            client->fragment_opcode = 0;
            return 1;
        }
        return 0;
    }

    /* Unknown opcode or invalid fin/opcode combination */
    KOMARI_LOG_WARN("WebSocket protocol error: unexpected opcode=%d fin=%d",
                    opcode, fin);
    return -1;
}

/* Internal helper: extract a string field from a cJSON object into a fixed-size
 * buffer. Silently truncates if the value is longer than the buffer. */
static void v2_extract_string(const cJSON *obj, const char *field,
                              char *dst, size_t dst_size) {
    if (!obj || !field || !dst || dst_size == 0) return;
    cJSON *item = cJSON_GetObjectItem(obj, field);
    if (item && cJSON_IsString(item)) {
        strncpy(dst, item->valuestring, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

/* Internal helper: extract a numeric field from a cJSON object as uint32. */
static uint32_t v2_extract_uint32(const cJSON *obj, const char *field) {
    if (!obj || !field) return 0;
    cJSON *item = cJSON_GetObjectItem(obj, field);
    if (item && cJSON_IsNumber(item)) {
        return (uint32_t)item->valuedouble;
    }
    return 0;
}

/* Handle a v2 JSON-RPC event: dedup by event ID, dispatch by method to the
 * registered ws_message_handler_t, and accumulate the ACK ID for the next
 * report. Mirrors the Go reference implementation (server/websocket.go,
 * processV2Event) while adapting to the C v2 interface which uses int ACK IDs.
 *
 * Returns 0 on success (event processed or duplicate skipped), -1 on parse
 * failure or invalid arguments. */
int ws_handle_v2_event(ws_client_t *client, const char *json_str) {
    if (!client || !json_str) return -1;

    jsonrpc_event_t event;
    if (jsonrpc_parse_event(json_str, &event) != 0) {
        return -1;
    }

    /* Determine the event ID for dedup and ACK.
     * jsonrpc_parse_event only captures string IDs; when event.id is NULL we
     * re-parse the raw JSON to check for a numeric ID, so ACK accumulation
     * works for both string and numeric event IDs (per SubTask 7.4). */
    const char *id_str = NULL;       /* String form of ID, used for dedup */
    char id_buf[32] = {0};           /* Buffer for numeric ID rendered as string */
    int ack_id = 0;                  /* Numeric ID for ACK (0 = skip ACK) */

    if (event.id && event.id[0] != '\0') {
        id_str = event.id;
        /* Try to convert string ID to int for ACK accumulation */
        char *endp = NULL;
        long val = strtol(event.id, &endp, 10);
        if (endp != event.id && val > 0) {
            ack_id = (int)val;
        }
    } else {
        /* Fall back to checking the raw JSON for a numeric id field */
        cJSON *root = cJSON_Parse(json_str);
        if (root) {
            cJSON *id_node = cJSON_GetObjectItem(root, "id");
            if (id_node && cJSON_IsNumber(id_node)) {
                long val = (long)id_node->valuedouble;
                if (val > 0) {
                    int id_ret;
                    ack_id = (int)val;
                    id_ret = snprintf(id_buf, sizeof(id_buf), "%ld", val);
                    if (id_ret < 0 || (size_t)id_ret >= sizeof(id_buf)) {
                        /* Truncation only happens for values exceeding 31
                         * digits, which cannot fit in an int anyway. Log and
                         * skip dedup for this event (MAJ-11). */
                        KOMARI_LOG_WARN("[v2] Event id %ld truncated for dedup", val);
                    } else {
                        id_str = id_buf;
                    }
                }
            }
            cJSON_Delete(root);
        }
    }

    /* Dedup by event ID (string form). Events without an ID are always
     * dispatched (matching Go's markV2EventSeen which returns true for ""). */
    int is_duplicate = 0;
    if (id_str) {
        if (v2_is_event_seen(&client->v2_state, id_str)) {
            is_duplicate = 1;
        } else if (v2_add_seen_event(&client->v2_state, id_str) != 0) {
            KOMARI_LOG_WARN("[v2] Failed to record seen event id: %s", id_str);
        }
    }

    /* Dispatch based on method. Duplicate events are not dispatched (to avoid
     * re-executing commands) but are still ACKed below so the server stops
     * retransmitting. */
    int processed = 0;
    if (!is_duplicate && event.method) {
        ws_message_t msg = {0};

        if (strcmp(event.method, AGENT_EXEC) == 0) {
            /* agent.exec: params = { task_id, command } */
            strncpy(msg.message, "exec", sizeof(msg.message) - 1);
            if (event.params) {
                v2_extract_string(event.params, "task_id",
                                  msg.exec_task_id, sizeof(msg.exec_task_id));
                v2_extract_string(event.params, "command",
                                  msg.exec_command, sizeof(msg.exec_command));
            }
            if (client->handler) client->handler(client, &msg);
            processed = 1;
        } else if (strcmp(event.method, AGENT_PING) == 0) {
            /* agent.ping: params = { ping_task_id, ping_type, ping_target } */
            strncpy(msg.message, "ping", sizeof(msg.message) - 1);
            if (event.params) {
                msg.ping_task_id = v2_extract_uint32(event.params, "ping_task_id");
                v2_extract_string(event.params, "ping_type",
                                  msg.ping_type, sizeof(msg.ping_type));
                v2_extract_string(event.params, "ping_target",
                                  msg.ping_target, sizeof(msg.ping_target));
            }
            if (client->handler) client->handler(client, &msg);
            processed = 1;
        } else if (strcmp(event.method, AGENT_TERMINAL_REQUEST) == 0) {
            /* agent.terminal.request: params = { request_id }
             * The v1 handler dispatches terminal requests when terminal_id is
             * non-empty, so we populate terminal_id and leave message empty. */
            if (event.params) {
                v2_extract_string(event.params, "request_id",
                                  msg.terminal_id, sizeof(msg.terminal_id));
            }
            if (client->handler) client->handler(client, &msg);
            processed = 1;
        } else if (strcmp(event.method, AGENT_MESSAGE) == 0 ||
                   strcmp(event.method, AGENT_EVENT) == 0) {
            /* agent.message / agent.event: log only, no handler dispatch.
             * These carry informational payloads that do not map to ws_message_t. */
            KOMARI_LOG_INFO("[v2] Received %s event", event.method);
            processed = 1;
        } else {
            KOMARI_LOG_WARN("[v2] Unknown event method: %s", event.method);
        }
    }

    /* ACK accumulation: record the event ID so the next report cycle carries
     * it in ack_event_ids. Both duplicates (to stop server retransmission)
     * and successfully processed events are ACKed, matching the Go behavior.
     * Skip ACK if the ID is missing, empty, or non-numeric (the C v2
     * interface uses int ACK IDs). */
    if (ack_id > 0 && (is_duplicate || processed)) {
        if (v2_add_ack_event(&client->v2_state, ack_id) != 0) {
            KOMARI_LOG_WARN("[v2] Failed to accumulate ACK id: %d", ack_id);
        }
    }

    jsonrpc_free_event(&event);
    return 0;
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

    /* MIN-64: pthread_mutex_init can fail (e.g. ENOMEM on some platforms).
     * Check the return value of each init so we only call pthread_mutex_destroy
     * on mutexes that were successfully initialized, and roll back all
     * previously allocated resources (mutexes + v2 state) before freeing the
     * client. */
    int mutex_ret = pthread_mutex_init(&client->send_mutex, NULL);
    if (mutex_ret != 0) {
        KOMARI_LOG_ERROR("ws_client_create: pthread_mutex_init(send) failed: %s",
                         strerror(mutex_ret));
        v2_state_cleanup(&client->v2_state);
        free(client);
        return NULL;
    }

    mutex_ret = pthread_mutex_init(&client->state_mutex, NULL);
    if (mutex_ret != 0) {
        KOMARI_LOG_ERROR("ws_client_create: pthread_mutex_init(state) failed: %s",
                         strerror(mutex_ret));
        pthread_mutex_destroy(&client->send_mutex);
        v2_state_cleanup(&client->v2_state);
        free(client);
        return NULL;
    }

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

    /* Free the fragment accumulation buffer to avoid leaking memory across
     * reconnects or when the client is destroyed mid-message. */
    free(client->fragment_buf);
    client->fragment_buf = NULL;
    client->fragment_len = 0;
    client->fragment_capacity = 0;
    client->fragment_opcode = 0;

    pthread_mutex_destroy(&client->send_mutex);
    pthread_mutex_destroy(&client->state_mutex);
    free(client);
}

int ws_client_connect(ws_client_t *client) {
    if (!client || !client->config.endpoint) return -1;

    /* Reset stop flag so the recv thread can run again after a reconnect */
    pthread_mutex_lock(&client->state_mutex);
    client->should_stop = false;
    pthread_mutex_unlock(&client->state_mutex);

    /* Close any previously open fd to avoid leaking socket descriptors across reconnects */
    if (client->fd >= 0) {
        ws_client_disconnect(client);
    }

    char scheme[8], host[256], path[512];
    int port;

    if (parse_url(client->config.endpoint, scheme, host, &port, path) != 0) {
        /* Record protocol failure so repeated v2 endpoint failures trigger
         * fallback to v1 (M-8). */
        ws_client_note_protocol_result(client, false);
        return -1;
    }

    /* Override the request path based on the negotiated protocol version.
     * The v2 protocol uses a JSON-RPC endpoint, while v1 uses the legacy
     * report endpoint. The token query string is appended separately in
     * ws_handshake, so only the path component is selected here. This
     * mirrors the Go reference implementation (server/websocket.go,
     * buildWebSocketEndpoint) which picks the path by protocol version. */
    if (ws_client_should_use_v2(client)) {
        int path_ret = snprintf(path, WS_PATH_MAX, "%s", V2_RPC_ENDPOINT);
        if (path_ret < 0 || (size_t)path_ret >= WS_PATH_MAX) {
            KOMARI_LOG_WARN("v2 RPC endpoint path truncated (MAJ-11)");
            ws_client_note_protocol_result(client, false);
            return -1;
        }
    } else {
        int path_ret = snprintf(path, WS_PATH_MAX, "%s", "/api/clients/report");
        if (path_ret < 0 || (size_t)path_ret >= WS_PATH_MAX) {
            KOMARI_LOG_WARN("v1 report endpoint path truncated (MAJ-11)");
            ws_client_note_protocol_result(client, false);
            return -1;
        }
    }

    client->use_tls = (strcmp(scheme, "wss") == 0);

    /* Use getaddrinfo() instead of gethostbyname() for IPv4/IPv6 dual-stack support.
     * gethostbyname() only returns IPv4 addresses and cannot connect to IPv6 endpoints.
     * Iterate over all returned addrinfo entries (like Go's net.Dialer) and keep the
     * first socket that connects successfully. */
    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;       /* allow IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;

    char port_str[16];
    int port_ret = snprintf(port_str, sizeof(port_str), "%d", port);
    if (port_ret < 0 || (size_t)port_ret >= sizeof(port_str)) {
        /* Port is validated by parse_url as 1..65535, so at most 5 digits.
         * Truncation is impossible in practice but check anyway (MAJ-11). */
        KOMARI_LOG_WARN("Port string truncated (MAJ-11)");
        ws_client_note_protocol_result(client, false);
        return -1;
    }

    int gai_err = getaddrinfo(host, port_str, &hints, &res);
    if (gai_err != 0) {
        ws_client_note_protocol_result(client, false);
        return -1;
    }

    client->fd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) {
            continue;
        }

        struct timeval tv = {.tv_sec = WS_CONNECT_TIMEOUT_SEC, .tv_usec = 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            client->fd = fd;
            break;  /* success */
        }
        close(fd);
    }
    freeaddrinfo(res);

    if (client->fd < 0) {
        /* all connect attempts failed */
        ws_client_note_protocol_result(client, false);
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
            ws_client_note_protocol_result(client, false);
            return -1;
        }
        
        /*
         * Mirror the Go reference implementation (see .komari-agent-main/
         * server/websocket.go newWSDialer): a tls.Config verifies the
         * peer by default; only set InsecureSkipVerify when ignore_cert
         * is explicitly requested. Without this, MITM attackers can
         * present any certificate and the handshake still succeeds.
         */
        if (client->config.ignore_cert) {
            SSL_CTX_set_verify(client->ssl_ctx, SSL_VERIFY_NONE, NULL);
        } else {
            /* Enable certificate verification by default (mirrors Go
             * tls.Config default). Load the system CA store so chain
             * verification can succeed, then require peer verification. */
            SSL_CTX_set_default_verify_paths(client->ssl_ctx);
            SSL_CTX_set_verify(client->ssl_ctx, SSL_VERIFY_PEER, NULL);
        }
        
        client->ssl = SSL_new(client->ssl_ctx);
        if (!client->ssl) {
            SSL_CTX_free(client->ssl_ctx);
            client->ssl_ctx = NULL;
            close(client->fd);
            client->fd = -1;
            ws_client_note_protocol_result(client, false);
            return -1;
        }
        
        SSL_set_fd(client->ssl, client->fd);
        SSL_set_tlsext_host_name(client->ssl, host);
        /*
         * Verify hostname against certificate (mirrors Go
         * tls.Config.ServerName). SSL_set1_host is available in
         * OpenSSL 1.0.2+; for older builds we fall back to
         * SSL_VERIFY_PEER chain-only verification.
         */
#if OPENSSL_VERSION_NUMBER >= 0x10002000L
        if (!client->config.ignore_cert) {
            SSL_set1_host(client->ssl, host);
        }
#else
        /* Fallback: rely on SSL_CTX_set_verify with SSL_VERIFY_PEER only */
#endif
        
        if (SSL_connect(client->ssl) <= 0) {
            SSL_free(client->ssl);
            client->ssl = NULL;
            SSL_CTX_free(client->ssl_ctx);
            client->ssl_ctx = NULL;
            close(client->fd);
            client->fd = -1;
            ws_client_note_protocol_result(client, false);
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
        ws_client_note_protocol_result(client, false);
        return -1;
    }

    /* Re-check should_stop after the handshake: the handshake can block for
     * up to WS_CONNECT_TIMEOUT_SEC (15s) on SSL_connect. If ws_client_stop
     * was called during that window (e.g. main shutdown), honor it instead
     * of overwriting it with connected=true. Without this check, the recv
     * thread would start on a soon-to-be-destroyed client, racing with
     * main's ws_client_destroy. */
    pthread_mutex_lock(&client->state_mutex);
    if (client->should_stop) {
        pthread_mutex_unlock(&client->state_mutex);
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
        ws_client_note_protocol_result(client, false);
        return -1;
    }

    /* Connection established successfully, record protocol attempt result (reset failure count, upgrade back to v2 if needed) */
    ws_client_note_protocol_result(client, true);

    return 0;
}

void ws_client_disconnect(ws_client_t *client) {
    if (!client) return;

    pthread_mutex_lock(&client->state_mutex);
    client->should_stop = true;
    client->connected = false;
    pthread_mutex_unlock(&client->state_mutex);

    /* Signal the recv thread to exit. shutdown(fd, SHUT_RDWR) causes any
     * blocking recv()/SSL_read() on the underlying socket to return
     * immediately so the recv thread can observe the failure, set
     * connected=false and break out of its loop.
     *
     * We deliberately do NOT call SSL_shutdown, close(fd) or SSL_free here
     * even though the recv thread may still be inside SSL_read/recv. Calling
     * SSL_shutdown concurrently with SSL_read is unsafe (the SSL object is
     * not reentrant) and freeing fd/ssl while the recv thread reads from
     * them is a use-after-free (MAJ-18). Mirrors Go's ws.SafeConn.Close
     * which signals the reader to stop before tearing down the connection. */
    if (client->fd >= 0) {
        shutdown(client->fd, SHUT_RDWR);
    }

    /* Wait for the recv thread to fully exit before touching SSL/fd for
     * cleanup. Only the fd has been shut down (not closed), so the recv
     * thread can safely return from its blocking read and exit. */
    if (client->recv_thread) {
        pthread_join(client->recv_thread, NULL);
        client->recv_thread = 0;
    }

    /* Now that no other thread is using the SSL object or fd, it is safe to
     * perform the TLS shutdown, free the SSL state and close the socket. */
    if (client->ssl) {
        SSL_shutdown(client->ssl);
        SSL_free(client->ssl);
        client->ssl = NULL;
    }

    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
}

int ws_client_send_text(ws_client_t *client, const char *data, size_t len) {
    if (!client || !data) return -1;
    pthread_mutex_lock(&client->state_mutex);
    bool connected = client->connected;
    pthread_mutex_unlock(&client->state_mutex);
    if (!connected) return -1;
    int ret = ws_send_frame(client, 0x01, data, len);
    if (ret != 0) {
        /* Send failure: record protocol result so repeated v2 failures
         * trigger fallback to v1 (M-8). */
        ws_client_note_protocol_result(client, false);
    }
    return ret;
}

int ws_client_send_ping(ws_client_t *client) {
    if (!client) return -1;
    pthread_mutex_lock(&client->state_mutex);
    bool connected = client->connected;
    pthread_mutex_unlock(&client->state_mutex);
    if (!connected) return -1;
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
    if (!client) return;
    pthread_mutex_lock(&client->state_mutex);
    client->should_stop = true;
    pthread_mutex_unlock(&client->state_mutex);
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
