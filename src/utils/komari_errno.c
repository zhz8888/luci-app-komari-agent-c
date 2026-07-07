/*
 * komari_strerror implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include "komari_errno.h"

const char *komari_strerror(komari_errno_t err) {
    switch (err) {
        case KOMARI_OK:                  return "success";
        case KOMARI_ERR_GENERIC:         return "unspecified error";
        case KOMARI_ERR_INVALID_ARG:     return "invalid argument";
        case KOMARI_ERR_NOT_FOUND:       return "not found";
        case KOMARI_ERR_BUFFER_TOO_SMALL: return "buffer too small";
        case KOMARI_ERR_PARSE:           return "parse error";
        case KOMARI_ERR_NETWORK:         return "network error";
        case KOMARI_ERR_NOMEM:           return "out of memory";
        case KOMARI_ERR_UNSUPPORTED:     return "operation not supported";
        default:                         return "unknown error code";
    }
}
