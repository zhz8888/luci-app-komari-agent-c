/*
 * Virtualization detection implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "virtual.h"
#include "utils.h"
#include "logger.h"

/* Read a file and return 1 if it contains the given keyword substring. */
static int check_file_contains(const char *path, const char *keyword) {
    char buf[4096];
    int n = utils_read_file_string(path, buf, sizeof(buf));
    if (n <= 0) return 0;
    return (strstr(buf, keyword) != NULL) ? 1 : 0;
}

/* Return 1 if the given path exists on the filesystem. */
static int file_exists(const char *path) {
    return utils_file_exists(path);
}

static int run_command_contains(const char *cmd, const char *keyword) {
    char output[4096];
    int exit_code = 0;
    if (utils_exec_command(cmd, output, sizeof(output), &exit_code) != 0) {
        return 0;
    }
    return (strstr(output, keyword) != NULL) ? 1 : 0;
}

const char *virt_detect(void) {
    if (file_exists("/.dockerenv")) {
        KOMARI_LOG_DEBUG("Virtualization detected: docker (/.dockerenv exists)");
        return VIRT_TYPE_DOCKER;
    }

    if (check_file_contains("/proc/1/cgroup", "docker") ||
        check_file_contains("/proc/1/cgroup", "kubepods")) {
        KOMARI_LOG_DEBUG("Virtualization detected: docker (/proc/1/cgroup)");
        return VIRT_TYPE_DOCKER;
    }

    if (file_exists("/run/.containerenv")) {
        KOMARI_LOG_DEBUG("Virtualization detected: docker (/run/.containerenv)");
        return VIRT_TYPE_DOCKER;
    }

    if (check_file_contains("/proc/1/cgroup", "lxc")) {
        KOMARI_LOG_DEBUG("Virtualization detected: lxc");
        return VIRT_TYPE_LXC;
    }

    if (file_exists("/proc/vz/veinfo")) {
        KOMARI_LOG_DEBUG("Virtualization detected: openvz");
        return VIRT_TYPE_OPENVZ;
    }

    if (run_command_contains("systemd-detect-virt --vm 2>/dev/null", "kvm")) {
        KOMARI_LOG_DEBUG("Virtualization detected: kvm (systemd-detect-virt)");
        return VIRT_TYPE_KVM;
    }

    if (run_command_contains("systemd-detect-virt --vm 2>/dev/null", "qemu")) {
        KOMARI_LOG_DEBUG("Virtualization detected: qemu (systemd-detect-virt)");
        return VIRT_TYPE_QEMU;
    }

    if (run_command_contains("systemd-detect-virt --vm 2>/dev/null", "vmware")) {
        KOMARI_LOG_DEBUG("Virtualization detected: vmware (systemd-detect-virt)");
        return VIRT_TYPE_VMWARE;
    }

    if (run_command_contains("systemd-detect-virt --vm 2>/dev/null", "oracle")) {
        KOMARI_LOG_DEBUG("Virtualization detected: virtualbox (systemd-detect-virt)");
        return VIRT_TYPE_VIRTUALBOX;
    }

    if (run_command_contains("systemd-detect-virt --vm 2>/dev/null", "microsoft")) {
        KOMARI_LOG_DEBUG("Virtualization detected: hyperv (systemd-detect-virt)");
        return VIRT_TYPE_HYPERV;
    }

    if (run_command_contains("systemd-detect-virt --vm 2>/dev/null", "xen")) {
        KOMARI_LOG_DEBUG("Virtualization detected: xen (systemd-detect-virt)");
        return VIRT_TYPE_XEN;
    }

    if (check_file_contains("/proc/cpuinfo", "QEMU") ||
        check_file_contains("/proc/cpuinfo", "KVM")) {
        KOMARI_LOG_DEBUG("Virtualization detected: kvm/qemu (/proc/cpuinfo)");
        return check_file_contains("/proc/cpuinfo", "QEMU") ? VIRT_TYPE_QEMU : VIRT_TYPE_KVM;
    }

    if (file_exists("/sys/class/dmi/id/product_name")) {
        if (check_file_contains("/sys/class/dmi/id/product_name", "VMware")) {
            KOMARI_LOG_DEBUG("Virtualization detected: vmware (DMI)");
            return VIRT_TYPE_VMWARE;
        }
        if (check_file_contains("/sys/class/dmi/id/product_name", "VirtualBox")) {
            KOMARI_LOG_DEBUG("Virtualization detected: virtualbox (DMI)");
            return VIRT_TYPE_VIRTUALBOX;
        }
        if (check_file_contains("/sys/class/dmi/id/product_name", "KVM")) {
            KOMARI_LOG_DEBUG("Virtualization detected: kvm (DMI)");
            return VIRT_TYPE_KVM;
        }
    }

    KOMARI_LOG_DEBUG("No virtualization detected");
    return VIRT_TYPE_NONE;
}

bool virt_is_container(void) {
    const char *type = virt_detect();
    return (strcmp(type, VIRT_TYPE_DOCKER) == 0 ||
            strcmp(type, VIRT_TYPE_LXC) == 0 ||
            strcmp(type, VIRT_TYPE_OPENVZ) == 0);
}

bool virt_is_vm(void) {
    const char *type = virt_detect();
    return (strcmp(type, VIRT_TYPE_KVM) == 0 ||
            strcmp(type, VIRT_TYPE_QEMU) == 0 ||
            strcmp(type, VIRT_TYPE_VMWARE) == 0 ||
            strcmp(type, VIRT_TYPE_VIRTUALBOX) == 0 ||
            strcmp(type, VIRT_TYPE_HYPERV) == 0 ||
            strcmp(type, VIRT_TYPE_XEN) == 0);
}
