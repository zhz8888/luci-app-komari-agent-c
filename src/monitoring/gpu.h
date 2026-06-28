/*
 * GPU detection and information interface declarations.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_GPU_H
#define KOMARI_AGENT_C_GPU_H

#include <stddef.h>

/* GPU information structure */
typedef struct {
    char name[128];      /* GPU name */
    char driver[64];     /* Driver name */
    int count;           /* GPU count */
} gpu_info_t;

/**
 * Get GPU name
 * Prefers lspci command, falls back to sysfs
 * @param name output buffer
 * @param name_len buffer size
 * @return 0 on success, -1 on failure
 */
int gpu_get_name(char *name, size_t name_len);

/**
 * Get GPU information
 * @param info output GPU information structure
 * @return 0 on success, -1 on failure
 */
int gpu_get_info(gpu_info_t *info);

#endif /* KOMARI_AGENT_C_GPU_H */
