/*
 * Persistent network traffic statistics implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <inttypes.h>

#include "netstatic.h"
#include "utils.h"
#include "logger.h"
#include "cJSON.h"

/* Hard cap on per-interface sample count. At the default detect_interval=2s
 * this covers ~4.6 days of raw samples; older samples are dropped via
 * netstatic_cleanup_old_data and the in-collect overflow path. Prevents the
 * 31-day buffer from growing to ~32MB per interface. */
#define NETSTATIC_MAX_SAMPLES_PER_INTERFACE 200000

/* Collect per-interface traffic deltas from /proc/net/dev for tracked interfaces. */
static void netstatic_collect(netstatic_t *ns) {
    FILE *fp = fopen("/proc/net/dev", "r");
    if (!fp) return;
    
    char line[512];
    fgets(line, sizeof(line), fp);
    fgets(line, sizeof(line), fp);
    
    while (fgets(line, sizeof(line), fp)) {
        char iface[32];
        uint64_t rx_bytes, tx_bytes;
        uint64_t dummy;
        
        if (sscanf(line, "%31[^:]: %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                   " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                   " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64
                   " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                   iface, &rx_bytes, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy,
                   &tx_bytes, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy) >= 10) {
            
            char *p = iface;
            while (*p == ' ') p++;
            
            for (size_t i = 0; i < ns->interface_count; i++) {
                if (strcmp(ns->interfaces[i].name, p) == 0) {
                    interface_stats_t *is = &ns->interfaces[i];
                    
                    uint64_t tx_delta = 0, rx_delta = 0;
                    if (is->last_tx > 0 && tx_bytes >= is->last_tx) {
                        tx_delta = tx_bytes - is->last_tx;
                    }
                    if (is->last_rx > 0 && rx_bytes >= is->last_rx) {
                        rx_delta = rx_bytes - is->last_rx;
                    }
                    
                    is->last_tx = tx_bytes;
                    is->last_rx = rx_bytes;
                    
                    if (is->data_count >= is->data_capacity) {
                        /* Stop growing once we hit the hard cap; instead drop
                         * the oldest sample to make room for the new one. This
                         * bounds memory at cap * sizeof(traffic_data_t)
                         * regardless of how long the worker runs between saves. */
                        if (is->data_capacity >= NETSTATIC_MAX_SAMPLES_PER_INTERFACE) {
                            memmove(is->data, is->data + 1,
                                    (is->data_count - 1) * sizeof(traffic_data_t));
                            is->data_count--;
                        } else {
                            size_t new_cap = is->data_capacity * 2;
                            if (new_cap > NETSTATIC_MAX_SAMPLES_PER_INTERFACE) {
                                new_cap = NETSTATIC_MAX_SAMPLES_PER_INTERFACE;
                            }
                            traffic_data_t *new_data = realloc(is->data, new_cap * sizeof(traffic_data_t));
                            if (new_data) {
                                memset(new_data + is->data_capacity, 0, (new_cap - is->data_capacity) * sizeof(traffic_data_t));
                                is->data = new_data;
                                is->data_capacity = new_cap;
                            } else {
                                KOMARI_LOG_ERROR("netstatic: realloc failed for interface %s", is->name);
                                break;
                            }
                        }
                    }

                    if (is->data_count < is->data_capacity) {
                        is->data[is->data_count].timestamp = utils_get_current_timestamp();
                        is->data[is->data_count].tx = tx_delta;
                        is->data[is->data_count].rx = rx_delta;
                        is->data_count++;
                    }
                    
                    break;
                }
            }
        }
    }
    
    fclose(fp);
}

/* Remove traffic samples older than the configured preserve-days window.
 * Also enforces a hard cap on per-interface sample count to prevent unbounded
 * memory growth when the preserve window is large or the collect interval is
 * short. When the cap is reached, the oldest samples are dropped to make room
 * for new ones. */
static void netstatic_cleanup_old_data(netstatic_t *ns) {
    uint64_t now = utils_get_current_timestamp();
    uint64_t cutoff = now - (uint64_t)(ns->data_preserve_days * 86400);

    for (size_t i = 0; i < ns->interface_count; i++) {
        interface_stats_t *is = &ns->interfaces[i];

        /* Drop samples older than the preserve window. Samples are appended
         * in chronological order, so we can stop scanning at the first sample
         * that falls inside the window. */
        size_t first_valid = 0;
        while (first_valid < is->data_count && is->data[first_valid].timestamp < cutoff) {
            first_valid++;
        }
        if (first_valid > 0) {
            size_t remaining = is->data_count - first_valid;
            if (remaining > 0) {
                memmove(is->data, is->data + first_valid, remaining * sizeof(traffic_data_t));
            }
            is->data_count = remaining;
        }

        /* Enforce hard cap: if still over the limit, drop the oldest samples. */
        size_t cap = NETSTATIC_MAX_SAMPLES_PER_INTERFACE;
        if (is->data_count > cap) {
            size_t excess = is->data_count - cap;
            memmove(is->data, is->data + excess, cap * sizeof(traffic_data_t));
            is->data_count = cap;
        }
    }
}

/* Serialize the entire netstatic state to a heap-allocated buffer. This runs
 * while the caller holds ns->mutex; the resulting buffer is then written to
 * disk outside the lock so long fprintf/fwrite I/O does not block LuCI
 * queries (netstatic_get_monthly_traffic). Returns NULL on failure. */
static char *netstatic_serialize(netstatic_t *ns, size_t *out_len) {
    char *buf = NULL;
    size_t buf_len = 0;
    FILE *mem = open_memstream(&buf, &buf_len);
    if (!mem) return NULL;

    int rc = 0;

    if (fprintf(mem, "{\n") < 0 ||
        fprintf(mem, "  \"config\": {\n") < 0 ||
        fprintf(mem, "    \"data_preserve_day\": %.1f,\n", ns->data_preserve_days) < 0 ||
        fprintf(mem, "    \"detect_interval\": %.1f,\n", ns->detect_interval) < 0 ||
        fprintf(mem, "    \"save_interval\": %.1f\n", ns->save_interval) < 0 ||
        fprintf(mem, "  },\n") < 0 ||
        fprintf(mem, "  \"interfaces\": {\n") < 0) {
        rc = -1;
        goto done;
    }

    for (size_t i = 0; i < ns->interface_count; i++) {
        interface_stats_t *is = &ns->interfaces[i];

        char *escaped_name = utils_json_escape(is->name);
        if (!escaped_name) {
            rc = -1;
            goto done;
        }

        if (fprintf(mem, "    \"%s\": [\n", escaped_name) < 0) {
            free(escaped_name);
            rc = -1;
            goto done;
        }
        free(escaped_name);

        for (size_t j = 0; j < is->data_count; j++) {
            if (fprintf(mem, "      {\"timestamp\": %" PRIu64 ", \"tx\": %" PRIu64 ", \"rx\": %" PRIu64 "}%s\n",
                        is->data[j].timestamp,
                        is->data[j].tx,
                        is->data[j].rx,
                        (j < is->data_count - 1) ? "," : "") < 0) {
                rc = -1;
                goto done;
            }
        }

        if (fprintf(mem, "    ]%s\n", (i < ns->interface_count - 1) ? "," : "") < 0) {
            rc = -1;
            goto done;
        }
    }

    if (fprintf(mem, "  }\n}\n") < 0) {
        rc = -1;
    }

done:
    fclose(mem);
    if (rc != 0) {
        free(buf);
        return NULL;
    }
    *out_len = buf_len;
    return buf;
}

/* Write a pre-serialized buffer to disk. This runs without holding ns->mutex
 * so slow disk I/O (especially on flash storage) does not block concurrent
 * readers. Returns 0 on success, -1 on failure. */
static int netstatic_write_to_disk(const char *path, const char *buf, size_t buf_len) {
    if (!path || !path[0] || !buf) return -1;

    FILE *fp = fopen(path, "w");
    if (!fp) return -1;

    int rc = 0;
    if (fwrite(buf, 1, buf_len, fp) != buf_len) {
        rc = -1;
    }
    if (fclose(fp) != 0) {
        rc = -1;
    }
    return rc;
}

/* Background worker thread: periodically collects traffic samples and saves
 * to disk. Serialization runs under the lock (needed to read interfaces[]),
 * but the actual disk write happens after unlocking so LuCI queries are not
 * blocked by flash I/O. */
static void *netstatic_worker(void *arg) {
    netstatic_t *ns = (netstatic_t *)arg;

    useconds_t detect_us = (useconds_t)(ns->detect_interval * 1000000);
    uint64_t last_save = 0;

    while (ns->running) {
        pthread_mutex_lock(&ns->mutex);
        netstatic_collect(ns);

        uint64_t now = utils_get_current_timestamp();
        char *snapshot = NULL;
        size_t snapshot_len = 0;
        if (now - last_save >= (uint64_t)ns->save_interval) {
            netstatic_cleanup_old_data(ns);
            /* Serialize under the lock, then write to disk after unlocking. */
            snapshot = netstatic_serialize(ns, &snapshot_len);
            last_save = now;
        }
        pthread_mutex_unlock(&ns->mutex);

        if (snapshot) {
            if (netstatic_write_to_disk(ns->save_path, snapshot, snapshot_len) != 0) {
                KOMARI_LOG_ERROR("netstatic: failed to write %s", ns->save_path);
            }
            free(snapshot);
        }

        usleep(detect_us);
    }

    return NULL;
}

netstatic_t *netstatic_create(const char *save_path) {
    netstatic_t *ns = calloc(1, sizeof(netstatic_t));
    if (!ns) return NULL;
    
    ns->data_preserve_days = 31.0;
    ns->detect_interval = 2.0;
    ns->save_interval = 600.0;
    ns->running = false;
    
    if (save_path) {
        strncpy(ns->save_path, save_path, sizeof(ns->save_path) - 1);
        /* Explicit NUL termination in case save_path fills the buffer. */
        ns->save_path[sizeof(ns->save_path) - 1] = '\0';
    } else {
        strcpy(ns->save_path, "/tmp/komari-netstatic.json");
    }
    
    pthread_mutex_init(&ns->mutex, NULL);
    
    return ns;
}

void netstatic_destroy(netstatic_t *ns) {
    if (!ns) return;
    
    if (ns->running) {
        netstatic_stop(ns);
    }
    
    for (size_t i = 0; i < ns->interface_count; i++) {
        free(ns->interfaces[i].data);
    }
    
    pthread_mutex_destroy(&ns->mutex);
    free(ns);
}

int netstatic_start(netstatic_t *ns) {
    if (!ns || ns->running) return -1;
    
    netstatic_load(ns);
    
    ns->running = true;
    
    if (pthread_create(&ns->worker_thread, NULL, netstatic_worker, ns) != 0) {
        ns->running = false;
        return -1;
    }
    
    return 0;
}

void netstatic_stop(netstatic_t *ns) {
    if (!ns || !ns->running) return;
    
    ns->running = false;
    pthread_join(ns->worker_thread, NULL);
    
    pthread_mutex_lock(&ns->mutex);
    netstatic_save(ns);
    pthread_mutex_unlock(&ns->mutex);
}

int netstatic_add_interface(netstatic_t *ns, const char *name) {
    if (!ns || !name || ns->interface_count >= NETSTATIC_MAX_INTERFACES) return -1;

    /* Hold the mutex while mutating the shared interface list so the
       worker thread cannot observe a half-initialized entry (MIN-16). */
    pthread_mutex_lock(&ns->mutex);

    for (size_t i = 0; i < ns->interface_count; i++) {
        if (strcmp(ns->interfaces[i].name, name) == 0) {
            pthread_mutex_unlock(&ns->mutex);
            return 0;
        }
    }

    interface_stats_t *is = &ns->interfaces[ns->interface_count];
    memset(is, 0, sizeof(interface_stats_t));
    strncpy(is->name, name, NETSTATIC_MAX_NAME_LEN - 1);
    /* Explicit NUL termination in case the interface name fills the buffer. */
    is->name[NETSTATIC_MAX_NAME_LEN - 1] = '\0';
    is->data_capacity = 1024;
    is->data = calloc(is->data_capacity, sizeof(traffic_data_t));
    if (!is->data) {
        pthread_mutex_unlock(&ns->mutex);
        fprintf(stderr, "Error: Failed to allocate memory for interface stats\n");
        return -1;
    }

    ns->interface_count++;
    pthread_mutex_unlock(&ns->mutex);
    return 0;
}

int netstatic_remove_interface(netstatic_t *ns, const char *name) {
    if (!ns || !name) return -1;

    /* Hold the mutex while removing from the shared interface list so
       the worker thread cannot dereference a freed data pointer (MIN-16). */
    pthread_mutex_lock(&ns->mutex);

    for (size_t i = 0; i < ns->interface_count; i++) {
        if (strcmp(ns->interfaces[i].name, name) == 0) {
            free(ns->interfaces[i].data);

            for (size_t j = i; j < ns->interface_count - 1; j++) {
                ns->interfaces[j] = ns->interfaces[j + 1];
            }

            ns->interface_count--;
            pthread_mutex_unlock(&ns->mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&ns->mutex);
    return -1;
}

int netstatic_get_monthly_traffic(netstatic_t *ns, const char *iface,
                                   uint64_t *tx, uint64_t *rx) {
    if (!ns || !iface || !tx || !rx) return -1;
    
    *tx = 0;
    *rx = 0;
    
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    int current_month = tm_now->tm_mon;
    int current_year = tm_now->tm_year;
    
    pthread_mutex_lock(&ns->mutex);
    
    for (size_t i = 0; i < ns->interface_count; i++) {
        if (strcmp(ns->interfaces[i].name, iface) == 0) {
            interface_stats_t *is = &ns->interfaces[i];
            
            for (size_t j = 0; j < is->data_count; j++) {
                time_t t = (time_t)is->data[j].timestamp;
                struct tm *tm_data = localtime(&t);

                if (tm_data->tm_mon == current_month && tm_data->tm_year == current_year) {
                    /* Guard against uint64_t accumulation overflow so a
                       long-running counter cannot wrap back to a small
                       value (MIN-19). Clamp at UINT64_MAX instead. */
                    if (is->data[j].tx > UINT64_MAX - *tx) {
                        *tx = UINT64_MAX;
                    } else {
                        *tx += is->data[j].tx;
                    }
                    if (is->data[j].rx > UINT64_MAX - *rx) {
                        *rx = UINT64_MAX;
                    } else {
                        *rx += is->data[j].rx;
                    }
                }
            }
            
            break;
        }
    }
    
    pthread_mutex_unlock(&ns->mutex);
    
    return 0;
}

int netstatic_save(netstatic_t *ns) {
    if (!ns || !ns->save_path[0]) return -1;

    /* Reuse the serialize + write_to_disk split so external callers
     * (netstatic_stop, etc.) benefit from the same code path. Callers of
     * netstatic_save must still hold ns->mutex. */
    size_t buf_len = 0;
    char *buf = netstatic_serialize(ns, &buf_len);
    if (!buf) return -1;

    int rc = netstatic_write_to_disk(ns->save_path, buf, buf_len);
    free(buf);
    return rc;
}

int netstatic_load(netstatic_t *ns) {
    if (!ns || !ns->save_path[0]) return -1;

    FILE *fp = fopen(ns->save_path, "r");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return -1;
    }
    fseek(fp, 0, SEEK_SET);

    char *json = malloc((size_t)size + 1);
    if (!json) {
        fclose(fp);
        return -1;
    }

    size_t bytes_read = fread(json, 1, (size_t)size, fp);
    json[bytes_read] = '\0';
    fclose(fp);

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        return -1;
    }

    /* Parse config section: sampling intervals and preserve window */
    cJSON *config = cJSON_GetObjectItem(root, "config");
    if (config && cJSON_IsObject(config)) {
        cJSON *item;
        if ((item = cJSON_GetObjectItem(config, "data_preserve_day")) && cJSON_IsNumber(item)) {
            ns->data_preserve_days = item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(config, "detect_interval")) && cJSON_IsNumber(item)) {
            ns->detect_interval = item->valuedouble;
        }
        if ((item = cJSON_GetObjectItem(config, "save_interval")) && cJSON_IsNumber(item)) {
            ns->save_interval = item->valuedouble;
        }
    }

    /* MAJ-22 / T12.3: validate parsed config values. A non-positive
     * detect_interval would make the worker loop spin as fast as possible
     * (useconds_t cast of 0 yields no sleep), and a non-positive
     * data_preserve_days would purge all historical samples immediately.
     * Fall back to the defaults set by netstatic_create() and log the
     * problem so the operator can fix the persisted file. */
    if (ns->data_preserve_days <= 0) {
        KOMARI_LOG_ERROR("netstatic: data_preserve_days (%.2f) must be > 0, using default 31.0",
                         ns->data_preserve_days);
        ns->data_preserve_days = 31.0;
    }
    if (ns->detect_interval <= 0) {
        KOMARI_LOG_ERROR("netstatic: detect_interval (%.2f) must be > 0, using default 2.0",
                         ns->detect_interval);
        ns->detect_interval = 2.0;
    }
    if (ns->save_interval <= 0) {
        KOMARI_LOG_ERROR("netstatic: save_interval (%.2f) must be > 0, using default 600.0",
                         ns->save_interval);
        ns->save_interval = 600.0;
    }

    /* Parse interfaces section: each key is an interface name mapped to an array of records */
    cJSON *interfaces = cJSON_GetObjectItem(root, "interfaces");
    if (interfaces && cJSON_IsObject(interfaces)) {
        cJSON *iface_entry;
        cJSON_ArrayForEach(iface_entry, interfaces) {
            if (!iface_entry->string || !cJSON_IsArray(iface_entry)) continue;
            if (ns->interface_count >= NETSTATIC_MAX_INTERFACES) break;

            const char *iface_name = iface_entry->string;

            /* Locate an existing tracked interface, or register a new one */
            interface_stats_t *is = NULL;
            for (size_t i = 0; i < ns->interface_count; i++) {
                if (strcmp(ns->interfaces[i].name, iface_name) == 0) {
                    is = &ns->interfaces[i];
                    break;
                }
            }
            if (!is) {
                if (netstatic_add_interface(ns, iface_name) != 0) continue;
                is = &ns->interfaces[ns->interface_count - 1];
            }

            int record_count = cJSON_GetArraySize(iface_entry);
            if (record_count <= 0) continue;
            size_t count = (size_t)record_count;

            /* Grow the sample buffer if the persisted history exceeds current capacity */
            if (count > is->data_capacity) {
                traffic_data_t *new_data = realloc(is->data, count * sizeof(traffic_data_t));
                if (!new_data) continue;
                memset(new_data + is->data_capacity, 0,
                       (count - is->data_capacity) * sizeof(traffic_data_t));
                is->data = new_data;
                is->data_capacity = count;
            }

            /* Restore traffic samples: timestamp, tx, rx */
            size_t idx = 0;
            cJSON *record;
            cJSON_ArrayForEach(record, iface_entry) {
                if (idx >= is->data_capacity) break;
                cJSON *ts = cJSON_GetObjectItem(record, "timestamp");
                cJSON *tx = cJSON_GetObjectItem(record, "tx");
                cJSON *rx = cJSON_GetObjectItem(record, "rx");
                if (ts && cJSON_IsNumber(ts)) {
                    is->data[idx].timestamp = (uint64_t)ts->valuedouble;
                }
                if (tx && cJSON_IsNumber(tx)) {
                    is->data[idx].tx = (uint64_t)tx->valuedouble;
                }
                if (rx && cJSON_IsNumber(rx)) {
                    is->data[idx].rx = (uint64_t)rx->valuedouble;
                }
                idx++;
            }
            is->data_count = idx;
        }
    }

    cJSON_Delete(root);
    return 0;
}
