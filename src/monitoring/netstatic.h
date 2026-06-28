/*
 * Persistent network traffic statistics interface declarations.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_NETSTATIC_H
#define KOMARI_AGENT_C_NETSTATIC_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#define NETSTATIC_MAX_INTERFACES 32
#define NETSTATIC_MAX_NAME_LEN 32
#define NETSTATIC_MAX_DATA_DAYS 31

typedef struct {
    uint64_t timestamp;
    uint64_t tx;
    uint64_t rx;
} traffic_data_t;

typedef struct {
    char name[NETSTATIC_MAX_NAME_LEN];
    traffic_data_t *data;
    size_t data_count;
    size_t data_capacity;
    uint64_t last_tx;
    uint64_t last_rx;
} interface_stats_t;

typedef struct {
    interface_stats_t interfaces[NETSTATIC_MAX_INTERFACES];
    size_t interface_count;
    double data_preserve_days;
    double detect_interval;
    double save_interval;
    char save_path[256];
    bool running;
    pthread_t worker_thread;
    pthread_mutex_t mutex;
} netstatic_t;

/**
 * Create a new netstatic statistics context.
 *
 * @param save_path File path used to persist statistics (may be NULL for default)
 * @return Pointer to new netstatic_t, NULL on failure
 */
netstatic_t *netstatic_create(const char *save_path);

/**
 * Destroy a netstatic context, stopping the worker and freeing resources.
 *
 * @param ns Pointer to netstatic context (may be NULL)
 */
void netstatic_destroy(netstatic_t *ns);

/**
 * Start the background worker thread that periodically collects traffic data.
 *
 * @param ns Pointer to netstatic context
 * @return 0 on success, -1 on failure
 */
int netstatic_start(netstatic_t *ns);

/**
 * Stop the background worker thread and persist current statistics.
 *
 * @param ns Pointer to netstatic context
 */
void netstatic_stop(netstatic_t *ns);

/**
 * Add a network interface to the tracked interface list.
 *
 * @param ns Pointer to netstatic context
 * @param name Interface name (e.g. "eth0")
 * @return 0 on success, -1 on failure
 */
int netstatic_add_interface(netstatic_t *ns, const char *name);

/**
 * Remove a network interface from the tracked interface list.
 *
 * @param ns Pointer to netstatic context
 * @param name Interface name to remove
 * @return 0 on success, -1 if interface not found
 */
int netstatic_remove_interface(netstatic_t *ns, const char *name);

/**
 * Get the total monthly traffic (tx and rx) for the specified interface.
 *
 * @param ns Pointer to netstatic context
 * @param iface Interface name
 * @param tx Output total transmitted bytes for current month
 * @param rx Output total received bytes for current month
 * @return 0 on success, -1 on failure
 */
int netstatic_get_monthly_traffic(netstatic_t *ns, const char *iface,
                                   uint64_t *tx, uint64_t *rx);

/**
 * Save current statistics to the configured JSON file.
 *
 * @param ns Pointer to netstatic context
 * @return 0 on success, -1 on failure
 */
int netstatic_save(netstatic_t *ns);

/**
 * Load statistics and configuration from the configured JSON file.
 *
 * @param ns Pointer to netstatic context
 * @return 0 on success, -1 on failure
 */
int netstatic_load(netstatic_t *ns);

#endif
