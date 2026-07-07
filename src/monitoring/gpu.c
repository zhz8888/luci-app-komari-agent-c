/*
 * GPU detection and information implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include "gpu.h"
#include "utils.h"
#include "logger.h"
#include "paths.h"

/* Virtual graphics card driver list (kernel driver names, used for sysfs detection) */
static const char *g_virtual_drivers[] = {
    "virtio-pci",
    "vmwgfx",
    "qxl",
    "cirrus",
    "vboxvideo",
    "hyperv_fb",
    NULL
};

/* Common virtual graphics card indicators in lspci output (used for lspci detection) */
static const char *g_virtual_indicators[] = {
    "VMware",
    "VirtualBox",
    "InnoTek",
    "Cirrus",
    "QXL",
    "Hyper-V",
    "virtio",
    NULL
};

/* GPU vendor priority matching table (lower number means higher priority) */
static const struct {
    const char *keyword;
    int priority;
} g_vendor_priority[] = {
    { "NVIDIA", 0 },
    { "Advanced Micro Devices", 1 },
    { "AMD", 1 },
    { "Intel", 2 },
    { "ARC", 3 },
    { "Qualcomm", 4 },
    { "Adreno", 4 },
    { NULL, -1 }
};

/* Check whether the driver name is a virtual graphics card driver */
static int is_virtual_driver(const char *driver) {
    if (!driver || !*driver) return 0;
    for (int i = 0; g_virtual_drivers[i] != NULL; i++) {
        if (strstr(driver, g_virtual_drivers[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

/* Check whether the lspci description contains virtual graphics card indicators */
static int is_virtual_description(const char *desc) {
    if (!desc || !*desc) return 0;
    for (int i = 0; g_virtual_indicators[i] != NULL; i++) {
        if (strstr(desc, g_virtual_indicators[i]) != NULL) {
            return 1;
        }
    }
    return 0;
}

/* Get vendor priority, lower number means higher priority, returns 99 if no match */
static int get_vendor_priority(const char *desc) {
    if (!desc) return 99;
    for (int i = 0; g_vendor_priority[i].keyword != NULL; i++) {
        if (strstr(desc, g_vendor_priority[i].keyword) != NULL) {
            return g_vendor_priority[i].priority;
        }
    }
    return 99;
}

/* Determine whether it is a GPU-related lspci line (VGA/3D/Display devices) */
static int is_gpu_line(const char *line) {
    if (!line) return 0;
    return (strstr(line, "VGA") != NULL ||
            strstr(line, "3D") != NULL ||
            strstr(line, "Display") != NULL);
}

/* Extract GPU description from an lspci line
 * Example: "00:00.0 VGA compatible controller: NVIDIA Corporation GP107 [GeForce GTX 1050]"
 * Extracted: "NVIDIA Corporation GP107 [GeForce GTX 1050]"
 */
static int extract_gpu_desc(const char *line, char *desc, size_t desc_len) {
    if (!line || !desc || desc_len == 0) return -1;

    /* Find the ": " delimiter (between class name and description) */
    const char *sep = strstr(line, ": ");
    if (!sep) return -1;

    sep += 2;  /* Skip ": " */

    /* Copy the description part */
    strncpy(desc, sep, desc_len - 1);
    desc[desc_len - 1] = '\0';

    /* Remove trailing newline characters */
    char *nl = strchr(desc, '\n');
    if (nl) *nl = '\0';
    char *cr = strchr(desc, '\r');
    if (cr) *cr = '\0';

    return 0;
}

/* Get GPU name via lspci command */
static int gpu_get_name_via_lspci(char *name, size_t name_len) {
    char output[4096];
    int exit_code = 0;

    /* Execute lspci command, redirect stderr to avoid noise */
    if (utils_exec_command("lspci 2>/dev/null", output, sizeof(output), &exit_code) != 0) {
        return -1;
    }
    if (exit_code != 0 || output[0] == '\0') {
        return -1;
    }

    char best_desc[256] = {0};
    int best_priority = 100;  /* Larger than unmatched 99 */
    int found = 0;

    /* Scan lspci output line by line, select best GPU by vendor priority */
    char *saveptr = NULL;
    char *line = strtok_r(output, "\n", &saveptr);
    while (line != NULL) {
        if (!is_gpu_line(line)) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Extract GPU description */
        char desc[256] = {0};
        if (extract_gpu_desc(line, desc, sizeof(desc)) != 0) {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Exclude virtual graphics cards */
        if (is_virtual_description(desc)) {
            KOMARI_LOG_DEBUG("Skipping virtual GPU: %s", desc);
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Select best GPU by vendor priority */
        int priority = get_vendor_priority(desc);
        if (priority < best_priority) {
            best_priority = priority;
            strncpy(best_desc, desc, sizeof(best_desc) - 1);
            best_desc[sizeof(best_desc) - 1] = '\0';
            found = 1;
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    if (!found) return -1;

    strncpy(name, best_desc, name_len - 1);
    name[name_len - 1] = '\0';
    return 0;
}

/* Read the last component of a symbolic link as the name
 * e.g., /sys/bus/pci/drivers/i915 -> "i915"
 */
static int read_driver_name(const char *link_path, char *buf, size_t buf_len) {
    if (!link_path || !buf || buf_len == 0) return -1;

    char target[512] = {0};
    ssize_t n = readlink(link_path, target, sizeof(target) - 1);
    if (n < 0) return -1;
    target[n] = '\0';

    /* Extract the last segment of the path */
    char *slash = strrchr(target, '/');
    const char *driver = slash ? slash + 1 : target;

    strncpy(buf, driver, buf_len - 1);
    buf[buf_len - 1] = '\0';
    return 0;
}

/* Determine whether a directory entry is in cardN format (card followed by digits only) */
static int is_card_entry(const char *name) {
    if (!name || strncmp(name, "card", 4) != 0) return 0;
    const char *p = name + 4;
    if (*p == '\0') return 0;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') return 0;
    }
    return 1;
}

/* Identify SoC GPU via device tree compatible
 * Supports: Qualcomm Adreno, ARM Mali, Broadcom VideoCore (Raspberry Pi), Allwinner, NVIDIA Tegra
 */
static int detect_soc_gpu(const char *card_path, char *name, size_t name_len) {
    char compatible_path[512];
    char buf[512] = {0};

    snprintf(compatible_path, sizeof(compatible_path),
             "%s/device/of_node/compatible", card_path);

    /* Read device tree compatible file directly to obtain the raw byte count,
     * since the content may contain multiple NUL-separated strings */
    int fd = open(compatible_path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t len = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (len < 0) return -1;
    buf[len] = '\0';

    /* The device tree compatible field contains multiple strings separated by NUL.
     * Iterate through all strings to find a matching SoC GPU. */
    char *p = buf;
    char *end = buf + len;
    while (p < end) {
        if (strstr(p, "adreno") != NULL || strstr(p, "qcom,") != NULL) {
            strncpy(name, "Qualcomm Adreno", name_len - 1);
            name[name_len - 1] = '\0';
            return 0;
        }
        if (strstr(p, "mali") != NULL) {
            strncpy(name, "ARM Mali", name_len - 1);
            name[name_len - 1] = '\0';
            return 0;
        }
        if (strstr(p, "brcm,bcm") != NULL || strstr(p, "vc4") != NULL) {
            strncpy(name, "Broadcom VideoCore", name_len - 1);
            name[name_len - 1] = '\0';
            return 0;
        }
        if (strstr(p, "allwinner") != NULL) {
            strncpy(name, "Allwinner GPU", name_len - 1);
            name[name_len - 1] = '\0';
            return 0;
        }
        if (strstr(p, "nvidia,tegra") != NULL || strstr(p, "tegra") != NULL) {
            strncpy(name, "NVIDIA Tegra", name_len - 1);
            name[name_len - 1] = '\0';
            return 0;
        }
        p += strlen(p) + 1;
    }

    return -1;
}

/* Get GPU name via sysfs (/sys/class/drm/card*) */
static int gpu_get_name_via_sysfs(char *name, size_t name_len) {
    DIR *dir = opendir(KOMARI_PATH_SYS_DRM);
    if (!dir) return -1;

    int virtual_count = 0;
    int found = 0;
    char found_name[128] = {0};

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!is_card_entry(entry->d_name)) continue;

        char card_path[512];
        snprintf(card_path, sizeof(card_path), "%s/%s", KOMARI_PATH_SYS_DRM, entry->d_name);

        /* Read driver name (symbolic link) */
        char driver_link[640];
        char driver_name[64] = {0};
        snprintf(driver_link, sizeof(driver_link), "%s/device/driver", card_path);

        if (read_driver_name(driver_link, driver_name, sizeof(driver_name)) != 0) {
            /* No driver link, skip this card */
            continue;
        }

        /* Exclude virtual graphics card drivers */
        if (is_virtual_driver(driver_name)) {
            KOMARI_LOG_DEBUG("Skipping virtual graphics driver: %s (card: %s)",
                             driver_name, entry->d_name);
            virtual_count++;
            continue;
        }

        /* Prefer identifying SoC GPU via device tree compatible */
        if (detect_soc_gpu(card_path, found_name, sizeof(found_name)) == 0) {
            found = 1;
            break;
        }

        /* Fallback: use driver name as GPU name */
        strncpy(found_name, driver_name, sizeof(found_name) - 1);
        found_name[sizeof(found_name) - 1] = '\0';
        found = 1;
        break;
    }
    closedir(dir);

    if (!found) {
        /* Only virtual graphics cards detected, return None */
        if (virtual_count > 0) {
            strncpy(name, "None", name_len - 1);
            name[name_len - 1] = '\0';
            return 0;
        }
        return -1;
    }

    strncpy(name, found_name, name_len - 1);
    name[name_len - 1] = '\0';
    return 0;
}

int gpu_get_name(char *name, size_t name_len) {
    if (!name || name_len == 0) return -1;

    name[0] = '\0';

    /* Prefer using lspci command */
    if (gpu_get_name_via_lspci(name, name_len) == 0) {
        KOMARI_LOG_DEBUG("GPU detected via lspci: %s", name);
        return 0;
    }

    /* Fall back to sysfs */
    if (gpu_get_name_via_sysfs(name, name_len) == 0) {
        KOMARI_LOG_DEBUG("GPU detected via sysfs: %s", name);
        return 0;
    }

    /* Both failed, return None */
    strncpy(name, "None", name_len - 1);
    name[name_len - 1] = '\0';
    KOMARI_LOG_DEBUG("No GPU detected, returning None");
    return 0;
}

int gpu_get_info(gpu_info_t *info) {
    if (!info) return -1;

    memset(info, 0, sizeof(gpu_info_t));

    /* Get GPU name */
    if (gpu_get_name(info->name, sizeof(info->name)) != 0) {
        strncpy(info->name, "None", sizeof(info->name) - 1);
        info->name[sizeof(info->name) - 1] = '\0';
    }

    /* Set count based on whether a real GPU was detected */
    if (strcmp(info->name, "None") == 0) {
        info->count = 0;
    } else {
        info->count = 1;
    }

    /* Try to get driver name from sysfs */
    DIR *dir = opendir(KOMARI_PATH_SYS_DRM);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (!is_card_entry(entry->d_name)) continue;

            char driver_link[640];
            char driver_name[64] = {0};
            snprintf(driver_link, sizeof(driver_link),
                     "%s/%s/device/driver", KOMARI_PATH_SYS_DRM, entry->d_name);

            if (read_driver_name(driver_link, driver_name, sizeof(driver_name)) == 0) {
                if (!is_virtual_driver(driver_name)) {
                    strncpy(info->driver, driver_name, sizeof(info->driver) - 1);
                    info->driver[sizeof(info->driver) - 1] = '\0';
                    break;
                }
            }
        }
        closedir(dir);
    }

    return 0;
}
