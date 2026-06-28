/*
 * gzip compression/decompression implementation using zlib.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include "compress.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* Initial output buffer size */
#define COMPRESS_CHUNK_SIZE 4096

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

    /* Initial buffer: input size + header/trailer overhead, at least CHUNK_SIZE */
    size_t buf_size = input_len + 128;
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
            /* Output buffer insufficient; needs to be expanded */
            size_t used = buf_size - strm.avail_out;
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

    /* Initial buffer: 4x the input size, at least CHUNK_SIZE */
    size_t buf_size = input_len * 4;
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
        strm.next_out = (Bytef *)(buf + total_out);
        strm.avail_out = (uInt)(buf_size - total_out);

        ret = inflate(&strm, Z_NO_FLUSH);

        total_out = buf_size - strm.avail_out;

        if (ret == Z_OK) {
            if (strm.avail_out == 0) {
                /* Output buffer full; needs to be expanded */
                size_t new_size = buf_size * 2;
                char *new_buf = (char *)realloc(buf, new_size);
                if (!new_buf) {
                    free(buf);
                    inflateEnd(&strm);
                    return -1;
                }
                buf = new_buf;
                buf_size = new_size;
            }
        } else if (ret == Z_BUF_ERROR) {
            if (strm.avail_out == 0) {
                /* Output buffer full; needs to be expanded */
                size_t new_size = buf_size * 2;
                char *new_buf = (char *)realloc(buf, new_size);
                if (!new_buf) {
                    free(buf);
                    inflateEnd(&strm);
                    return -1;
                }
                buf = new_buf;
                buf_size = new_size;
            } else {
                /* Input data insufficient or exhausted */
                break;
            }
        }
    } while (ret != Z_STREAM_END);

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
