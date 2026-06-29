/*
 * gzip compression/decompression implementation using zlib.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include "compress.h"

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <zlib.h>

#include "logger.h"

/* Initial output buffer size */
#define COMPRESS_CHUNK_SIZE 4096

/* Per-call overhead added to input_len when sizing the initial compression
 * buffer (zlib header + trailer + small slack). */
#define COMPRESS_SIZE_OVERHEAD 128

/* Initial decompression expansion factor applied to input_len. */
#define DECOMPRESS_INITIAL_FACTOR 4

/* Maximum acceptable decompressed output size. Protects against gzip bombs
 * where a tiny compressed payload expands into an unbounded stream. */
#define MAX_DECOMPRESSED_SIZE (16 * 1024 * 1024)

int compress_gzip(const char *input, size_t input_len,
                   char **output, size_t *output_len)
{
    if (!input || !output || !output_len) {
        return -1;
    }

    *output = NULL;
    *output_len = 0;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    /* windowBits = 15 + 16 indicates gzip output format */
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                      15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return -1;
    }

    strm.next_in = (Bytef *)input;
    strm.avail_in = (uInt)input_len;

    /* Initial buffer: input size + header/trailer overhead, at least CHUNK_SIZE.
     * Guard against integer overflow on input_len + overhead (MIN-44/45):
     * if input_len is so large that adding the overhead would wrap around,
     * the requested buffer would be smaller than expected (or zero) and the
     * subsequent memcpy/deflate could write out of bounds. */
    if (input_len > SIZE_MAX - COMPRESS_SIZE_OVERHEAD) {
        KOMARI_LOG_ERROR("compress: input_len %zu overflows size_t", input_len);
        deflateEnd(&strm);
        return -1;
    }
    size_t buf_size = input_len + COMPRESS_SIZE_OVERHEAD;
    if (buf_size < COMPRESS_CHUNK_SIZE) {
        buf_size = COMPRESS_CHUNK_SIZE;
    }

    char *buf = (char *)malloc(buf_size);
    if (!buf) {
        deflateEnd(&strm);
        return -1;
    }

    strm.next_out = (Bytef *)buf;
    strm.avail_out = (uInt)buf_size;

    int ret;
    do {
        ret = deflate(&strm, Z_FINISH);

        if (ret == Z_OK || ret == Z_BUF_ERROR) {
            /* Output buffer insufficient; needs to be expanded.
             * Guard against size_t wrap when doubling (MIN-44/45). */
            size_t used = buf_size - strm.avail_out;
            if (buf_size > SIZE_MAX / 2) {
                KOMARI_LOG_ERROR("compress: output buffer size %zu overflows on growth", buf_size);
                free(buf);
                deflateEnd(&strm);
                return -1;
            }
            size_t new_size = buf_size * 2;
            char *new_buf = (char *)realloc(buf, new_size);
            if (!new_buf) {
                free(buf);
                deflateEnd(&strm);
                return -1;
            }
            buf = new_buf;
            buf_size = new_size;
            strm.next_out = (Bytef *)(buf + used);
            strm.avail_out = (uInt)(buf_size - used);
        }
    } while (ret == Z_OK || ret == Z_BUF_ERROR);

    if (ret != Z_STREAM_END) {
        free(buf);
        deflateEnd(&strm);
        return -1;
    }

    *output = buf;
    *output_len = buf_size - strm.avail_out;

    deflateEnd(&strm);
    return 0;
}

int compress_gunzip(const char *input, size_t input_len,
                     char **output, size_t *output_len)
{
    if (!input || !output || !output_len) {
        return -1;
    }

    *output = NULL;
    *output_len = 0;

    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    /* windowBits = 15 + 16 indicates the input is in gzip format */
    if (inflateInit2(&strm, 15 + 16) != Z_OK) {
        return -1;
    }

    strm.next_in = (Bytef *)input;
    strm.avail_in = (uInt)input_len;

    /* Initial buffer: 4x the input size, at least CHUNK_SIZE.
     * Guard against size_t overflow on the multiplication (MIN-44/45): if
     * input_len exceeds SIZE_MAX / 4 the product wraps to a small value and
     * inflate would write past the end of the undersized buffer. Cap the
     * initial size at MAX_DECOMPRESSED_SIZE so the bomb guard below stays
     * meaningful. */
    if (input_len > MAX_DECOMPRESSED_SIZE / DECOMPRESS_INITIAL_FACTOR) {
        KOMARI_LOG_ERROR("gunzip: input_len %zu exceeds decompression limit", input_len);
        inflateEnd(&strm);
        return -1;
    }
    size_t buf_size = input_len * DECOMPRESS_INITIAL_FACTOR;
    if (buf_size < COMPRESS_CHUNK_SIZE) {
        buf_size = COMPRESS_CHUNK_SIZE;
    }

    char *buf = (char *)malloc(buf_size);
    if (!buf) {
        inflateEnd(&strm);
        return -1;
    }

    size_t total_out = 0;
    int ret;

    do {
        /* Bomb guard: refuse to keep producing output beyond the cap. */
        if (total_out > MAX_DECOMPRESSED_SIZE) {
            KOMARI_LOG_WARN("gunzip: decompressed output exceeds %d bytes limit, aborting",
                            MAX_DECOMPRESSED_SIZE);
            free(buf);
            inflateEnd(&strm);
            return -1;
        }

        strm.next_out = (Bytef *)(buf + total_out);
        strm.avail_out = (uInt)(buf_size - total_out);

        ret = inflate(&strm, Z_NO_FLUSH);

        total_out = buf_size - strm.avail_out;

        if (ret == Z_STREAM_END) {
            /* Decompression finished successfully */
            break;
        }

        /* Z_DATA_ERROR, Z_MEM_ERROR, Z_NEED_DICT, etc. are fatal. The
         * original loop kept retrying inflate on these states, which
         * caused an infinite loop on truncated or corrupt gzip data. */
        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            free(buf);
            inflateEnd(&strm);
            return -1;
        }

        /* Z_BUF_ERROR with a non-full output buffer means no progress is
         * possible (typically truncated input). Break to avoid looping
         * forever; the Z_STREAM_END check below reports the failure. */
        if (ret == Z_BUF_ERROR && strm.avail_out > 0) {
            break;
        }

        /* Output buffer is full; expand it (subject to the bomb guard). */
        if (strm.avail_out == 0) {
            if (buf_size >= MAX_DECOMPRESSED_SIZE) {
                KOMARI_LOG_WARN("gunzip: decompressed output exceeds %d bytes limit, aborting",
                                MAX_DECOMPRESSED_SIZE);
                free(buf);
                inflateEnd(&strm);
                return -1;
            }

            size_t new_size = buf_size * 2;
            if (new_size > MAX_DECOMPRESSED_SIZE) {
                new_size = MAX_DECOMPRESSED_SIZE;
            }
            char *new_buf = (char *)realloc(buf, new_size);
            if (!new_buf) {
                free(buf);
                inflateEnd(&strm);
                return -1;
            }
            buf = new_buf;
            buf_size = new_size;
        }
    } while (1);

    if (ret != Z_STREAM_END) {
        free(buf);
        inflateEnd(&strm);
        return -1;
    }

    *output = buf;
    *output_len = total_out;

    inflateEnd(&strm);
    return 0;
}

int compress_is_available(void)
{
    /* The current implementation has a hard dependency on zlib; always available */
    return 1;
}
