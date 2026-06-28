/*
 * Virtualization detection interface declarations.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_VIRTUAL_H
#define KOMARI_AGENT_C_VIRTUAL_H

#include <stdbool.h>

#define VIRT_TYPE_NONE "none"
#define VIRT_TYPE_KVM "kvm"
#define VIRT_TYPE_QEMU "qemu"
#define VIRT_TYPE_VMWARE "vmware"
#define VIRT_TYPE_VIRTUALBOX "virtualbox"
#define VIRT_TYPE_HYPERV "hyperv"
#define VIRT_TYPE_XEN "xen"
#define VIRT_TYPE_LXC "lxc"
#define VIRT_TYPE_DOCKER "docker"
#define VIRT_TYPE_OPENVZ "openvz"
#define VIRT_TYPE_UNKNOWN "unknown"

/**
 * Detect the virtualization environment of the current system.
 * Probes container markers, systemd-detect-virt output, DMI and /proc/cpuinfo.
 *
 * @return Static string identifying the virtualization type (e.g. "kvm", "docker", "none")
 */
const char *virt_detect(void);

/**
 * Check whether the system is running inside a container (docker/lxc/openvz).
 *
 * @return true if running in a container, false otherwise
 */
bool virt_is_container(void);

/**
 * Check whether the system is running inside a virtual machine
 * (kvm/qemu/vmware/virtualbox/hyperv/xen).
 *
 * @return true if running in a VM, false otherwise
 */
bool virt_is_vm(void);

#endif
