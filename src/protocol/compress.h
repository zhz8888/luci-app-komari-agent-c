/*
 * gzip compression/decompression helpers backed by zlib.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_COMPRESS_H
#define KOMARI_AGENT_C_COMPRESS_H

#include <stddef.h>

/**
 * gzip compression.
 *
 * @param input      Data to compress.
 * @param input_len  Length of the data to compress (in bytes).
 * @param output     Outputs a pointer to the output buffer, which is allocated
 *                   internally. The caller must free it.
 * @param output_len Outputs the length of the output data (in bytes).
 * @return 0 on success, -1 on failure.
 */
int compress_gzip(const char *input, size_t input_len,
                   char **output, size_t *output_len);

/**
 * gzip decompression.
 *
 * @param input      Data to decompress.
 * @param input_len  Length of the data to decompress (in bytes).
 * @param output     Outputs a pointer to the output buffer, which is allocated
 *                   internally. The caller must free it.
 * @param output_len Outputs the length of the output data (in bytes).
 * @return 0 on success, -1 on failure.
 */
int compress_gunzip(const char *input, size_t input_len,
                     char **output, size_t *output_len);

/**
 * Check whether zlib is available.
 *
 * @return Always returns 1 (the current implementation has a hard dependency on zlib).
 */
int compress_is_available(void);

#endif /* KOMARI_AGENT_C_COMPRESS_H */
