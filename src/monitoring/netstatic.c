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

#include "netstatic.h"
#include "utils.h"

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
        
        if (sscanf(line, "%31[^:]: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu",
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
    useconds_t save_us = (useconds_t)(ns->save_interval * 1000000);
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
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"config\": {\n");
    fprintf(fp, "    \"data_preserve_day\": %.1f,\n", ns->data_preserve_days);
    fprintf(fp, "    \"detect_interval\": %.1f,\n", ns->detect_interval);
    fprintf(fp, "    \"save_interval\": %.1f\n", ns->save_interval);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"interfaces\": {\n");
    
    for (size_t i = 0; i < ns->interface_count; i++) {
        interface_stats_t *is = &ns->interfaces[i];
        
        fprintf(fp, "    \"%s\": [\n", is->name);
        
        for (size_t j = 0; j < is->data_count; j++) {
            fprintf(fp, "      {\"timestamp\": %lu, \"tx\": %lu, \"rx\": %lu}%s\n",
                    (unsigned long)is->data[j].timestamp,
                    (unsigned long)is->data[j].tx,
                    (unsigned long)is->data[j].rx,
                    (j < is->data_count - 1) ? "," : "");
        }
        
        fprintf(fp, "    ]%s\n", (i < ns->interface_count - 1) ? "," : "");
    }
    
    fprintf(fp, "  }\n");
    fprintf(fp, "}\n");
    
    fclose(fp);
    return 0;
}

int netstatic_load(netstatic_t *ns) {
    if (!ns || !ns->save_path[0]) return -1;
    
    FILE *fp = fopen(ns->save_path, "r");
    if (!fp) return -1;
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    char *json = malloc(size + 1);
    if (!json) {
        fclose(fp);
        return -1;
    }

    size_t bytes_read = fread(json, 1, size, fp);
    if (bytes_read != (size_t)size) {
        fprintf(stderr, "Warning: Netstatic data file read incomplete: %zu/%ld bytes\n", bytes_read, size);
    }
    json[size] = '\0';
    fclose(fp);
    
    char *p = json;
    while ((p = strstr(p, "\"config\"")) != NULL) {
        p = strchr(p, '{');
        if (!p) break;
        
        char *end = strchr(p, '}');
        if (!end) break;
        
        char *dpd = strstr(p, "\"data_preserve_day\"");
        if (dpd && dpd < end) {
            char *colon = strchr(dpd, ':');
            if (colon) ns->data_preserve_days = atof(colon + 1);
        }
        
        char *di = strstr(p, "\"detect_interval\"");
        if (di && di < end) {
            char *colon = strchr(di, ':');
            if (colon) ns->detect_interval = atof(colon + 1);
        }
        
        break;
    }
    
    free(json);
    return 0;
}
