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
#include <time.h>
#include <errno.h>
#include <pthread.h>
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
#include <inttypes.h>

#include "monitoring.h"
#include "utils.h"
#include "config.h"
#include "logger.h"

static const agent_config_t *g_config = NULL;

/* Cross-call CPU sampling state. Previously monitoring_get_cpu_info blocked
 * for 100ms on every call to measure a CPU usage delta inside the function.
 * Since report_thread already invokes this once per second, we instead sample
 * /proc/stat once per call and compute the delta against the previous call's
 * snapshot. This removes the 100ms blocking and lets the thread respond to
 * shutdown signals promptly. */
static unsigned long long g_last_cpu_total = 0;
static unsigned long long g_last_cpu_used = 0;
static int g_cpu_sample_initialized = 0;

/* Cached invariant CPU fields. cpu_name, cpu_cores and cpu_arch only change
 * on reboot or CPU hotplug; re-reading /proc/cpuinfo every second is pure
 * waste. Populate once on the first call. */
static cpu_info_t g_cpu_invariants;
static int g_cpu_invariants_cached = 0;

/* Tiered sampling caches. disk/connections/process_count change slowly and
 * require scanning multiple /proc files or all of /proc; refresh them every
 * MONITORING_SLOW_TTL seconds instead of every report cycle. */
#define MONITORING_SLOW_TTL_SEC 5
static disk_info_t g_disk_cache;
static conn_info_t g_conn_cache;
static int g_process_count_cache = 0;
static time_t g_disk_cache_ts = 0;
static time_t g_conn_cache_ts = 0;
static time_t g_process_count_ts = 0;

void monitoring_set_config(const agent_config_t *config) {
    g_config = config;
}

int monitoring_get_cpu_info(cpu_info_t *info) {
    if (!info) return -1;

    memset(info, 0, sizeof(cpu_info_t));

    /* Populate invariant fields (cpu_cores, cpu_arch, cpu_name) from cache
     * on subsequent calls; refresh from /proc/cpuinfo only on the first
     * call. These fields do not change at runtime. */
    if (!g_cpu_invariants_cached) {
        g_cpu_invariants.cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
        if (g_cpu_invariants.cpu_cores <= 0) g_cpu_invariants.cpu_cores = 1;

#if defined(__aarch64__)
        strcpy(g_cpu_invariants.cpu_arch, "arm64");
#elif defined(__arm__)
        strcpy(g_cpu_invariants.cpu_arch, "arm");
#elif defined(__x86_64__)
        strcpy(g_cpu_invariants.cpu_arch, "x86_64");
#elif defined(__i386__)
        strcpy(g_cpu_invariants.cpu_arch, "i386");
#else
        strcpy(g_cpu_invariants.cpu_arch, "unknown");
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
                        strncpy(g_cpu_invariants.cpu_name, colon, sizeof(g_cpu_invariants.cpu_name) - 1);
                        /* Explicit NUL termination in case source fills the buffer. */
                        g_cpu_invariants.cpu_name[sizeof(g_cpu_invariants.cpu_name) - 1] = '\0';
                    }
                    break;
                }
            }
            fclose(fp);
        }

        if (g_cpu_invariants.cpu_name[0] == '\0') {
            strcpy(g_cpu_invariants.cpu_name, "Unknown CPU");
        }
        g_cpu_invariants_cached = 1;
    }

    info->cpu_cores = g_cpu_invariants.cpu_cores;
    strncpy(info->cpu_arch, g_cpu_invariants.cpu_arch, sizeof(info->cpu_arch) - 1);
    info->cpu_arch[sizeof(info->cpu_arch) - 1] = '\0';
    strncpy(info->cpu_name, g_cpu_invariants.cpu_name, sizeof(info->cpu_name) - 1);
    info->cpu_name[sizeof(info->cpu_name) - 1] = '\0';

    /* Sample /proc/stat once and compute CPU usage against the previous
     * call's snapshot, instead of blocking for 100ms inside this function.
     * The first call has no baseline and reports 0.0% usage. */
    FILE *stat_fp = fopen("/proc/stat", "r");
    if (stat_fp) {
        unsigned long long user, nice, system, idle, iowait, irq, softirq;
        if (fscanf(stat_fp, "cpu %llu %llu %llu %llu %llu %llu %llu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq) == 7) {
            unsigned long long total = user + nice + system + idle + iowait + irq + softirq;
            unsigned long long used = user + nice + system + irq + softirq;

            if (g_cpu_sample_initialized) {
                /* Guard against counter wraparound or reset to avoid a
                 * bogus spike when /proc/stat counters go backwards. */
                unsigned long long total_diff = (total >= g_last_cpu_total) ? (total - g_last_cpu_total) : 0;
                unsigned long long used_diff = (used >= g_last_cpu_used) ? (used - g_last_cpu_used) : 0;

                if (total_diff > 0) {
                    info->cpu_usage = (double)used_diff / (double)total_diff * 100.0;
                }
            }

            g_last_cpu_total = total;
            g_last_cpu_used = used;
            g_cpu_sample_initialized = 1;
        }
        fclose(stat_fp);
    }

    return 0;
}

int monitoring_get_mem_swap_info(mem_info_t *mem, mem_info_t *swap) {
    /* Parse /proc/meminfo once for both memory and swap fields. Previously
     * monitoring_get_mem_info and monitoring_get_swap_info each opened and
     * parsed the file independently, doubling the per-cycle
     * fopen/fgets/sscanf cost. Either output pointer may be NULL to skip
     * that portion. */
    if (!mem && !swap) return -1;

    if (mem) memset(mem, 0, sizeof(mem_info_t));
    if (swap) memset(swap, 0, sizeof(mem_info_t));

    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1;

    char line[256];
    unsigned long mem_total = 0, mem_free = 0, mem_available = 0;
    unsigned long buffers = 0, cached = 0, shmem = 0, sreclaimable = 0;
    unsigned long swap_total = 0, swap_free = 0;

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
            } else if (strcmp(key, "SwapTotal") == 0) {
                swap_total = value * 1024;
            } else if (strcmp(key, "SwapFree") == 0) {
                swap_free = value * 1024;
            }
        }
    }
    fclose(fp);

    if (mem) {
        mem->total = mem_total;
        mem->free = mem_free;
        mem->available = mem_available;
        mem->buffers = buffers;
        mem->cached = cached + sreclaimable;

        if (g_config && g_config->memory_include_cache) {
            mem->used = mem_total - mem_free;
        } else if (mem_available > 0) {
            mem->used = mem_total - mem_available;
        } else {
            mem->used = mem_total - mem_free - buffers - cached;
        }
    }

    if (swap) {
        swap->total = swap_total;
        swap->free = swap_free;
        swap->used = swap_total - swap_free;
    }

    return 0;
}

int monitoring_get_mem_info(mem_info_t *info) {
    /* Thin wrapper for backward compatibility; delegates to the combined
     * parser so /proc/meminfo is read once per cycle when callers invoke
     * monitoring_get_mem_info and monitoring_get_swap_info back-to-back. */
    return monitoring_get_mem_swap_info(info, NULL);
}

int monitoring_get_swap_info(mem_info_t *info) {
    return monitoring_get_mem_swap_info(NULL, info);
}

int monitoring_get_disk_info(disk_info_t *info) {
    if (!info) return -1;

    /* Serve cached result if fresh, to avoid scanning /proc/mounts and
     * statvfs on every mount point every report cycle. */
    time_t now = time(NULL);
    if (g_disk_cache_ts != 0 && now - g_disk_cache_ts < MONITORING_SLOW_TTL_SEC) {
        *info = g_disk_cache;
        return 0;
    }

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
                /* Cast to uint64_t before multiplication to avoid 32-bit
                   overflow on platforms where `unsigned long` is 32 bits,
                   and defensively clamp on uint64_t overflow (MIN-27/28). */
                uint64_t frsize = (uint64_t)st.f_frsize;
                uint64_t blocks = (uint64_t)st.f_blocks;
                uint64_t bfree  = (uint64_t)st.f_bfree;

                uint64_t total = (frsize != 0 && blocks > UINT64_MAX / frsize)
                                 ? UINT64_MAX : blocks * frsize;
                uint64_t free  = (frsize != 0 && bfree  > UINT64_MAX / frsize)
                                 ? UINT64_MAX : bfree  * frsize;

                /* Clamp running totals to UINT64_MAX to prevent addition
                   overflow when aggregating across multiple mounts. */
                if (total > UINT64_MAX - info->total) {
                    info->total = UINT64_MAX;
                } else {
                    info->total += total;
                }
                if (free > UINT64_MAX - info->free) {
                    info->free = UINT64_MAX;
                } else {
                    info->free += free;
                }
            }
        }
    }
    fclose(fp);

    /* Guard against unsigned underflow: in abnormal filesystem states (e.g.
     * statvfs returning f_bfree > f_blocks, or aggregated mounts where free
     * exceeds total), info->free could be larger than info->total, which
     * would wrap to ~UINT64_MAX and report a nonsensical ~16 EB usage. */
    info->used = (info->free > info->total) ? 0 : (info->total - info->free);

    /* Update cache so subsequent calls within the TTL skip the scan. */
    g_disk_cache = *info;
    g_disk_cache_ts = now;

    return 0;
}

static uint64_t g_last_rx = 0, g_last_tx = 0;
static uint64_t g_last_time = 0;
/* Protects g_last_rx/g_last_tx/g_last_time against concurrent access.
 * monitoring_get_net_info may be called from multiple threads (e.g. the
 * report thread and a status API handler); without a lock the read-modify-
 * write cycle on these globals could interleave and produce incorrect
 * rx_speed/tx_speed values. Mirrors the pattern used in logger.c. */
static pthread_mutex_t g_net_info_mutex = PTHREAD_MUTEX_INITIALIZER;

int monitoring_get_net_info(net_info_t *info) {
    if (!info) return -1;

    memset(info, 0, sizeof(net_info_t));

    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) return -1;

    char line[512];
    uint64_t total_rx = 0, total_tx = 0;
    uint64_t total_rx_packets = 0, total_tx_packets = 0;

    /* /proc/net/dev starts with two header lines. Check fgets return values
     * so an empty or unreadable file (e.g. /proc not mounted) is reported as
     * an error rather than silently returning zero statistics. */
    if (!fgets(line, sizeof(line), fp) || !fgets(line, sizeof(line), fp)) {
        fclose(fp);
        return -1;
    }

    while (fgets(line, sizeof(line), fp)) {
        char iface[32];
        uint64_t rx_bytes, rx_packets, rx_errs, rx_drop, rx_fifo, rx_frame, rx_compressed, rx_multicast;
        uint64_t tx_bytes, tx_packets, tx_errs, tx_drop, tx_fifo, tx_colls, tx_carrier, tx_compressed;

        if (sscanf(line, "%31[^:]: %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                   " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                   " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                   " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
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

    pthread_mutex_lock(&g_net_info_mutex);
    uint64_t now = utils_get_current_timestamp();
    if (g_last_time > 0) {
        uint64_t time_diff = now - g_last_time;
        if (time_diff > 0) {
            /* Guard against counter wraparound or reset to avoid underflow */
            uint64_t rx_diff = (total_rx >= g_last_rx) ? (total_rx - g_last_rx) : 0;
            uint64_t tx_diff = (total_tx >= g_last_tx) ? (total_tx - g_last_tx) : 0;
            /* Use floating-point division and round to nearest to avoid
               integer truncation that systematically under-reports the
               transfer rate (MIN-29/30). */
            info->rx_speed = (uint64_t)((double)rx_diff / (double)time_diff + 0.5);
            info->tx_speed = (uint64_t)((double)tx_diff / (double)time_diff + 0.5);
        }
    }

    g_last_rx = total_rx;
    g_last_tx = total_tx;
    g_last_time = now;
    pthread_mutex_unlock(&g_net_info_mutex);

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

    /* Serve cached result if fresh, to avoid opening 4 /proc/net/* files
     * and counting lines on every report cycle. */
    time_t now = time(NULL);
    if (g_conn_cache_ts != 0 && now - g_conn_cache_ts < MONITORING_SLOW_TTL_SEC) {
        *info = g_conn_cache;
        return 0;
    }

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

    /* Update cache. */
    g_conn_cache = *info;
    g_conn_cache_ts = now;

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
                    /* Explicit NUL termination in case source fills the buffer. */
                    info->os_name[sizeof(info->os_name) - 1] = '\0';
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
        /* Explicit NUL termination in case source fills the buffer. */
        info->kernel_version[sizeof(info->kernel_version) - 1] = '\0';
        strncpy(info->arch, uts.machine, sizeof(info->arch) - 1);
        info->arch[sizeof(info->arch) - 1] = '\0';
        strncpy(info->hostname, uts.nodename, sizeof(info->hostname) - 1);
        info->hostname[sizeof(info->hostname) - 1] = '\0';
    }
    
    info->uptime = monitoring_get_uptime();
    
    return 0;
}

uint64_t monitoring_get_uptime(void) {
    /* Delegate to utils_get_uptime_seconds to avoid duplicating the
     * /proc/uptime parsing logic. Both functions historically had identical
     * implementations; keeping a single source of truth makes future
     * changes (e.g. a fallback to /sys/class/rtc) apply everywhere. */
    return utils_get_uptime_seconds();
}

int monitoring_get_process_count(void) {
    /* Serve cached result if fresh, to avoid a full /proc readdir scan on
     * every report cycle. On busy systems /proc can have thousands of
     * entries. */
    time_t now = time(NULL);
    if (g_process_count_ts != 0 && now - g_process_count_ts < MONITORING_SLOW_TTL_SEC) {
        return g_process_count_cache;
    }

    int count = 0;
    DIR *dir = opendir("/proc");
    if (!dir) return g_process_count_cache;  /* fall back to last known value */

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

    g_process_count_cache = count;
    g_process_count_ts = now;

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
                /* Explicit NUL termination in case source fills the buffer. */
                ipv4[ipv4_len - 1] = '\0';
            }
        } else if (family == AF_INET6 && ipv6 && ipv6[0] == '\0') {
            char host[NI_MAXHOST];
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            if (!IN6_IS_ADDR_LINKLOCAL(&addr6->sin6_addr)) {
                if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in6),
                               host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0) {
                    strncpy(ipv6, host, ipv6_len - 1);
                    ipv6[ipv6_len - 1] = '\0';
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
