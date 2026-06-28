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
#include "cJSON.h"

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
                        size_t new_cap = is->data_capacity * 2;
                        traffic_data_t *new_data = realloc(is->data, new_cap * sizeof(traffic_data_t));
                        if (new_data) {
                            memset(new_data + is->data_capacity, 0, (new_cap - is->data_capacity) * sizeof(traffic_data_t));
                            is->data = new_data;
                            is->data_capacity = new_cap;
                        } else {
                            fprintf(stderr, "Warning: Failed to realloc network stats data\n");
                            break;
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

/* Remove traffic samples older than the configured preserve-days window. */
static void netstatic_cleanup_old_data(netstatic_t *ns) {
    uint64_t now = utils_get_current_timestamp();
    uint64_t cutoff = now - (uint64_t)(ns->data_preserve_days * 86400);
    
    for (size_t i = 0; i < ns->interface_count; i++) {
        interface_stats_t *is = &ns->interfaces[i];
        
        size_t write_idx = 0;
        for (size_t j = 0; j < is->data_count; j++) {
            if (is->data[j].timestamp >= cutoff) {
                if (write_idx != j) {
                    is->data[write_idx] = is->data[j];
                }
                write_idx++;
            }
        }
        is->data_count = write_idx;
    }
}

/* Background worker thread: periodically collects traffic samples and saves to disk. */
static void *netstatic_worker(void *arg) {
    netstatic_t *ns = (netstatic_t *)arg;
    
    useconds_t detect_us = (useconds_t)(ns->detect_interval * 1000000);
    uint64_t last_save = 0;
    
    while (ns->running) {
        pthread_mutex_lock(&ns->mutex);
        netstatic_collect(ns);
        
        uint64_t now = utils_get_current_timestamp();
        if (now - last_save >= (uint64_t)ns->save_interval) {
            netstatic_cleanup_old_data(ns);
            netstatic_save(ns);
            last_save = now;
        }
        pthread_mutex_unlock(&ns->mutex);
        
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
    
    for (size_t i = 0; i < ns->interface_count; i++) {
        if (strcmp(ns->interfaces[i].name, name) == 0) {
            return 0;
        }
    }
    
    interface_stats_t *is = &ns->interfaces[ns->interface_count];
    memset(is, 0, sizeof(interface_stats_t));
    strncpy(is->name, name, NETSTATIC_MAX_NAME_LEN - 1);
    is->data_capacity = 1024;
    is->data = calloc(is->data_capacity, sizeof(traffic_data_t));
    if (!is->data) {
        fprintf(stderr, "Error: Failed to allocate memory for interface stats\n");
        return -1;
    }
    
    ns->interface_count++;
    return 0;
}

int netstatic_remove_interface(netstatic_t *ns, const char *name) {
    if (!ns || !name) return -1;
    
    for (size_t i = 0; i < ns->interface_count; i++) {
        if (strcmp(ns->interfaces[i].name, name) == 0) {
            free(ns->interfaces[i].data);
            
            for (size_t j = i; j < ns->interface_count - 1; j++) {
                ns->interfaces[j] = ns->interfaces[j + 1];
            }
            
            ns->interface_count--;
            return 0;
        }
    }
    
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
                    *tx += is->data[j].tx;
                    *rx += is->data[j].rx;
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

    FILE *fp = fopen(ns->save_path, "w");
    if (!fp) return -1;

    int rc = 0;

    if (fprintf(fp, "{\n") < 0 ||
        fprintf(fp, "  \"config\": {\n") < 0 ||
        fprintf(fp, "    \"data_preserve_day\": %.1f,\n", ns->data_preserve_days) < 0 ||
        fprintf(fp, "    \"detect_interval\": %.1f,\n", ns->detect_interval) < 0 ||
        fprintf(fp, "    \"save_interval\": %.1f\n", ns->save_interval) < 0 ||
        fprintf(fp, "  },\n") < 0 ||
        fprintf(fp, "  \"interfaces\": {\n") < 0) {
        rc = -1;
        goto done;
    }

    for (size_t i = 0; i < ns->interface_count; i++) {
        interface_stats_t *is = &ns->interfaces[i];

        /* Escape the interface name so quotes/backslashes/control chars in
           the name cannot break the surrounding JSON structure. */
        char *escaped_name = utils_json_escape(is->name);
        if (!escaped_name) {
            rc = -1;
            goto done;
        }

        if (fprintf(fp, "    \"%s\": [\n", escaped_name) < 0) {
            free(escaped_name);
            rc = -1;
            goto done;
        }
        free(escaped_name);

        for (size_t j = 0; j < is->data_count; j++) {
            if (fprintf(fp, "      {\"timestamp\": %lu, \"tx\": %lu, \"rx\": %lu}%s\n",
                        (unsigned long)is->data[j].timestamp,
                        (unsigned long)is->data[j].tx,
                        (unsigned long)is->data[j].rx,
                        (j < is->data_count - 1) ? "," : "") < 0) {
                rc = -1;
                goto done;
            }
        }

        if (fprintf(fp, "    ]%s\n", (i < ns->interface_count - 1) ? "," : "") < 0) {
            rc = -1;
            goto done;
        }
    }

    if (fprintf(fp, "  }\n") < 0 ||
        fprintf(fp, "}\n") < 0) {
        rc = -1;
    }

done:
    fclose(fp);
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
