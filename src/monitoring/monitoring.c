/*
 * System monitoring data collection implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <ctype.h>
#include <netdb.h>

#include "monitoring.h"
#include "utils.h"
#include "config.h"

static const agent_config_t *g_config = NULL;

void monitoring_set_config(const agent_config_t *config) {
    g_config = config;
}

int monitoring_get_cpu_info(cpu_info_t *info) {
    if (!info) return -1;
    
    memset(info, 0, sizeof(cpu_info_t));
    
    info->cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (info->cpu_cores <= 0) info->cpu_cores = 1;
    
#if defined(__aarch64__)
    strcpy(info->cpu_arch, "arm64");
#elif defined(__arm__)
    strcpy(info->cpu_arch, "arm");
#elif defined(__x86_64__)
    strcpy(info->cpu_arch, "x86_64");
#elif defined(__i386__)
    strcpy(info->cpu_arch, "i386");
#else
    strcpy(info->cpu_arch, "unknown");
#endif

    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "model name", 10) == 0 ||
                strncmp(line, "Model", 5) == 0 ||
                strncmp(line, "cpu model", 9) == 0) {
                char *colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ') colon++;
                    char *nl = strchr(colon, '\n');
                    if (nl) *nl = '\0';
                    strncpy(info->cpu_name, colon, sizeof(info->cpu_name) - 1);
                }
                break;
            }
        }
        fclose(fp);
    }
    
    if (info->cpu_name[0] == '\0') {
        strcpy(info->cpu_name, "Unknown CPU");
    }
    
    FILE *stat_fp = fopen("/proc/stat", "r");
    if (stat_fp) {
        unsigned long long user, nice, system, idle, iowait, irq, softirq;
        if (fscanf(stat_fp, "cpu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq) == 7) {
            unsigned long long total = user + nice + system + idle + iowait + irq + softirq;
            unsigned long long used = user + nice + system + irq + softirq;
            
            usleep(100000);
            
            fseek(stat_fp, 0, SEEK_SET);
            unsigned long long user2, nice2, system2, idle2, iowait2, irq2, softirq2;
            if (fscanf(stat_fp, "cpu %llu %llu %llu %llu %llu %llu %llu",
                       &user2, &nice2, &system2, &idle2, &iowait2, &irq2, &softirq2) == 7) {
                unsigned long long total2 = user2 + nice2 + system2 + idle2 + iowait2 + irq2 + softirq2;
                unsigned long long used2 = user2 + nice2 + system2 + irq2 + softirq2;
                
                unsigned long long total_diff = total2 - total;
                unsigned long long used_diff = used2 - used;
                
                if (total_diff > 0) {
                    info->cpu_usage = (double)used_diff / total_diff * 100.0;
                }
            }
        }
        fclose(stat_fp);
    }
    
    return 0;
}

int monitoring_get_mem_info(mem_info_t *info) {
    if (!info) return -1;
    
    memset(info, 0, sizeof(mem_info_t));
    
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1;
    
    char line[256];
    unsigned long mem_total = 0, mem_free = 0, mem_available = 0;
    unsigned long buffers = 0, cached = 0, shmem = 0, sreclaimable = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        unsigned long value;
        char key[64];
        
        if (sscanf(line, "%63[^:]: %lu", key, &value) == 2) {
            if (strcmp(key, "MemTotal") == 0) {
                mem_total = value * 1024;
            } else if (strcmp(key, "MemFree") == 0) {
                mem_free = value * 1024;
            } else if (strcmp(key, "MemAvailable") == 0) {
                mem_available = value * 1024;
            } else if (strcmp(key, "Buffers") == 0) {
                buffers = value * 1024;
            } else if (strcmp(key, "Cached") == 0) {
                cached = value * 1024;
            } else if (strcmp(key, "Shmem") == 0) {
                shmem = value * 1024;
            } else if (strcmp(key, "SReclaimable") == 0) {
                sreclaimable = value * 1024;
            }
        }
    }
    fclose(fp);
    
    info->total = mem_total;
    info->free = mem_free;
    info->available = mem_available;
    info->buffers = buffers;
    info->cached = cached + sreclaimable;
    
    if (g_config && g_config->memory_include_cache) {
        info->used = mem_total - mem_free;
    } else if (mem_available > 0) {
        info->used = mem_total - mem_available;
    } else {
        info->used = mem_total - mem_free - buffers - cached;
    }
    
    return 0;
}

int monitoring_get_swap_info(mem_info_t *info) {
    if (!info) return -1;
    
    memset(info, 0, sizeof(mem_info_t));
    
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1;
    
    char line[256];
    unsigned long swap_total = 0, swap_free = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        unsigned long value;
        char key[64];
        
        if (sscanf(line, "%63[^:]: %lu", key, &value) == 2) {
            if (strcmp(key, "SwapTotal") == 0) {
                swap_total = value * 1024;
            } else if (strcmp(key, "SwapFree") == 0) {
                swap_free = value * 1024;
            }
        }
    }
    fclose(fp);
    
    info->total = swap_total;
    info->free = swap_free;
    info->used = swap_total - swap_free;
    
    return 0;
}

int monitoring_get_disk_info(disk_info_t *info) {
    if (!info) return -1;
    
    memset(info, 0, sizeof(disk_info_t));
    
    FILE *fp = fopen("/proc/mounts", "r");
    if (!fp) return -1;
    
    char line[512];
    char device[256], mountpoint[256], fstype[64];
    
    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%255s %255s %63s", device, mountpoint, fstype) == 3) {
            if (strncmp(device, "/dev/", 5) != 0 &&
                strncmp(device, "/dev/mapper/", 12) != 0 &&
                strncmp(device, "ubi", 3) != 0 &&
                strncmp(device, "jffs2", 5) != 0 &&
                strncmp(device, "overlay", 7) != 0) {
                continue;
            }
            
            if (strcmp(fstype, "tmpfs") == 0 ||
                strcmp(fstype, "devtmpfs") == 0 ||
                strcmp(fstype, "debugfs") == 0 ||
                strcmp(fstype, "tracefs") == 0 ||
                strcmp(fstype, "securityfs") == 0 ||
                strcmp(fstype, "pstore") == 0 ||
                strcmp(fstype, "cgroup") == 0 ||
                strcmp(fstype, "cgroup2") == 0) {
                continue;
            }
            
            struct statvfs st;
            if (statvfs(mountpoint, &st) == 0) {
                uint64_t total = st.f_blocks * st.f_frsize;
                uint64_t free = st.f_bfree * st.f_frsize;
                
                info->total += total;
                info->free += free;
            }
        }
    }
    fclose(fp);
    
    info->used = info->total - info->free;
    
    return 0;
}

static uint64_t g_last_rx = 0, g_last_tx = 0;
static uint64_t g_last_time = 0;

int monitoring_get_net_info(net_info_t *info) {
    if (!info) return -1;
    
    memset(info, 0, sizeof(net_info_t));
    
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) return -1;
    
    char line[512];
    uint64_t total_rx = 0, total_tx = 0;
    uint64_t total_rx_packets = 0, total_tx_packets = 0;
    
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);
    
    while (fgets(line, sizeof(line), fp)) {
        char iface[32];
        uint64_t rx_bytes, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
        uint64_t tx_bytes, tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;
        
        if (sscanf(line, "%31[^:]: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
                   iface, &rx_bytes, &rx_packets, &rx_errs, &rx_drop, &rx_fifo, &rx_frame,
                   &rx_compressed, &rx_multicast, &tx_bytes, &tx_packets, &tx_errs, &tx_drop,
                   &tx_fifo, &tx_colls, &tx_carrier, &tx_compressed) >= 10) {
            
            char *p = iface;
            while (*p == ' ') p++;
            
            if (strcmp(p, "lo") == 0) continue;
            
            total_rx += rx_bytes;
            total_tx += tx_bytes;
            total_rx_packets += rx_packets;
            total_tx_packets += tx_packets;
        }
    }
    fclose(fp);
    
    info->rx_bytes = total_rx;
    info->tx_bytes = total_tx;
    info->rx_packets = total_rx_packets;
    info->tx_packets = total_tx_packets;
    
    uint64_t now = utils_get_current_timestamp();
    if (g_last_time > 0) {
        uint64_t time_diff = now - g_last_time;
        if (time_diff > 0) {
            info->rx_speed = (total_rx - g_last_rx) / time_diff;
            info->tx_speed = (total_tx - g_last_tx) / time_diff;
        }
    }
    
    g_last_rx = total_rx;
    g_last_tx = total_tx;
    g_last_time = now;
    
    return 0;
}

int monitoring_get_load_info(load_info_t *info) {
    if (!info) return -1;
    
    memset(info, 0, sizeof(load_info_t));
    
    FILE *fp = fopen("/proc/loadavg", "r");
    if (!fp) return -1;
    
    double load1, load5, load15;
    if (fscanf(fp, "%lf %lf %lf", &load1, &load5, &load15) == 3) {
        info->load1 = load1;
        info->load5 = load5;
        info->load15 = load15;
    }
    fclose(fp);
    
    return 0;
}

int monitoring_get_conn_info(conn_info_t *info) {
    if (!info) return -1;
    
    memset(info, 0, sizeof(conn_info_t));
    
    FILE *fp = fopen("/proc/net/tcp", "r");
    if (fp) {
        char line[256];
        fgets(line, sizeof(line), fp);
        while (fgets(line, sizeof(line), fp)) {
            info->tcp_count++;
        }
        fclose(fp);
    }
    
    fp = fopen("/proc/net/tcp6", "r");
    if (fp) {
        char line[256];
        fgets(line, sizeof(line), fp);
        while (fgets(line, sizeof(line), fp)) {
            info->tcp_count++;
        }
        fclose(fp);
    }
    
    fp = fopen("/proc/net/udp", "r");
    if (fp) {
        char line[256];
        fgets(line, sizeof(line), fp);
        while (fgets(line, sizeof(line), fp)) {
            info->udp_count++;
        }
        fclose(fp);
    }
    
    fp = fopen("/proc/net/udp6", "r");
    if (fp) {
        char line[256];
        fgets(line, sizeof(line), fp);
        while (fgets(line, sizeof(line), fp)) {
            info->udp_count++;
        }
        fclose(fp);
    }
    
    return 0;
}

int monitoring_get_system_info(system_info_t *info) {
    if (!info) return -1;
    
    memset(info, 0, sizeof(system_info_t));
    
    FILE *fp = fopen("/etc/openwrt_release", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "DISTRIB_DESCRIPTION", 19) == 0) {
                char *eq = strchr(line, '=');
                if (eq) {
                    eq++;
                    while (*eq == '\'' || *eq == '"') eq++;
                    char *end = eq + strlen(eq) - 1;
                    while (end > eq && (*end == '\'' || *end == '"' || *end == '\n')) {
                        *end = '\0';
                        end--;
                    }
                    strncpy(info->os_name, eq, sizeof(info->os_name) - 1);
                }
                break;
            }
        }
        fclose(fp);
    }
    
    if (info->os_name[0] == '\0') {
        strcpy(info->os_name, "OpenWrt");
    }
    
    struct utsname uts;
    if (uname(&uts) == 0) {
        strncpy(info->kernel_version, uts.release, sizeof(info->kernel_version) - 1);
        strncpy(info->arch, uts.machine, sizeof(info->arch) - 1);
        strncpy(info->hostname, uts.nodename, sizeof(info->hostname) - 1);
    }
    
    info->uptime = monitoring_get_uptime();
    
    return 0;
}

uint64_t monitoring_get_uptime(void) {
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) return 0;
    
    double uptime;
    if (fscanf(fp, "%lf", &uptime) == 1) {
        fclose(fp);
        return (uint64_t)uptime;
    }
    fclose(fp);
    return 0;
}

int monitoring_get_process_count(void) {
    int count = 0;
    DIR *dir = opendir("/proc");
    if (!dir) return 0;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            int is_pid = 1;
            for (int i = 0; entry->d_name[i]; i++) {
                if (!isdigit(entry->d_name[i])) {
                    is_pid = 0;
                    break;
                }
            }
            if (is_pid && entry->d_name[0] != '\0') {
                count++;
            }
        }
    }
    closedir(dir);
    
    return count;
}

int monitoring_get_ip_address(char *ipv4, size_t ipv4_len,
                               char *ipv6, size_t ipv6_len) {
    if (ipv4) ipv4[0] = '\0';
    if (ipv6) ipv6[0] = '\0';
    
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        return -1;
    }
    
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;
        if (strncmp(ifa->ifa_name, "docker", 6) == 0) continue;
        if (strncmp(ifa->ifa_name, "veth", 4) == 0) continue;
        if (strncmp(ifa->ifa_name, "br-", 3) == 0) continue;
        
        int family = ifa->ifa_addr->sa_family;
        
        if (family == AF_INET && ipv4 && ipv4[0] == '\0') {
            char host[NI_MAXHOST];
            if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                           host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0) {
                strncpy(ipv4, host, ipv4_len - 1);
            }
        } else if (family == AF_INET6 && ipv6 && ipv6[0] == '\0') {
            char host[NI_MAXHOST];
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            if (!IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr)) {
                if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in6),
                               host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0) {
                    strncpy(ipv6, host, ipv6_len - 1);
                }
            }
        }
    }
    
    freeifaddrs(ifaddr);
    return 0;
}

void monitoring_net_speed_update(void) {
    net_info_t info;
    monitoring_get_net_info(&info);
}
