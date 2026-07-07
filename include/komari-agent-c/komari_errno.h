/*
 * Komari Agent unified error codes.
 *
 * Historically every public API returned 0 on success and -1 on failure, so
 * callers could not tell parameter errors from I/O failures, parse errors or
 * allocation failures. komari_errno_t replaces the bare int return with a
 * small enumerated set of failure causes. Existing 0/-1 APIs continue to
 * work because KOMARI_OK == 0 and KOMARI_ERR_GENERIC == -1, so a function
 * that still returns -1 on any failure is compatible with callers that test
 * `rc != KOMARI_OK`.
 *
 * Migration policy: new APIs and refactored APIs should return
 * komari_errno_t and pick the most specific code. Existing APIs are not
 * retrofitted in a single pass — they migrate when their signatures change
 * for other reasons, to keep the diff reviewable.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_ERRNO_H
#define KOMARI_AGENT_C_ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

/* Unified error codes. Values are deliberately negative for failures and
 * zero for success, so `if (rc != KOMARI_OK)` continues to work for legacy
 * callers that only check the sign. */
typedef enum {
    KOMARI_OK                  =  0, /* Success */
    KOMARI_ERR_GENERIC         = -1, /* Unspecified failure (legacy 0/-1 APIs) */
    KOMARI_ERR_INVALID_ARG     = -2, /* NULL pointer, empty string, out-of-range value */
    KOMARI_ERR_NOT_FOUND       = -3, /* File, key or entry not present */
    KOMARI_ERR_BUFFER_TOO_SMALL = -4, /* Caller-provided buffer cannot hold the result */
    KOMARI_ERR_PARSE           = -5, /* JSON / config / protocol parse failure */
    KOMARI_ERR_NETWORK         = -6, /* Socket connect / send / recv failure */
    KOMARI_ERR_NOMEM           = -7, /* Memory allocation failure */
    KOMARI_ERR_UNSUPPORTED     = -8, /* Operation not supported on this platform/build */
} komari_errno_t;

/**
 * Return a short, static, English description of an error code.
 *
 * @param err Error code returned by a komari API.
 * @return NUL-terminated string (never NULL). The pointer is static and must
 *         not be freed by the caller.
 */
const char *komari_strerror(komari_errno_t err);

#ifdef __cplusplus
}
#endif

#endif /* KOMARI_AGENT_C_ERRNO_H */
