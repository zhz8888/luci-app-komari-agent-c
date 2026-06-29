/*
 * Web terminal PTY management interface declarations.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_TERMINAL_H
#define KOMARI_AGENT_C_TERMINAL_H

#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>

/* Maximum number of concurrent terminal sessions to prevent resource
 * exhaustion from malicious or runaway panel requests. */
#define MAX_TERMINAL_SESSIONS 8

typedef struct terminal terminal_t;

typedef void (*terminal_output_cb_t)(terminal_t *term, const char *data, size_t len);

struct terminal {
    int master_fd;
    int pid;
    int pgid;
    int cols;
    int rows;
    volatile sig_atomic_t running;  /* cross-thread: read thread reads, owner thread writes */
    pthread_t read_thread;
    terminal_output_cb_t output_cb;
    void *user_data;
};

/**
 * Create a new terminal context with the specified dimensions.
 *
 * @param cols Initial column count (0 defaults to 80)
 * @param rows Initial row count (0 defaults to 24)
 * @return Pointer to new terminal_t, NULL on failure
 */
terminal_t *terminal_create(int cols, int rows);

/**
 * Destroy a terminal context, terminating the shell if still running.
 *
 * The caller's pointer is set to NULL before the underlying memory is freed
 * so that other threads observing the pointer cannot race with the free
 * (use-after-free). Callers should pass the address of the pointer that is
 * shared with other threads, e.g. terminal_destroy(&session->term).
 *
 * @param term Pointer to the terminal pointer (may be NULL, *term may be NULL)
 */
void terminal_destroy(terminal_t **term);

/**
 * Acquire a terminal session slot, enforcing the MAX_TERMINAL_SESSIONS limit.
 *
 * Must be called before creating a new terminal session. Each successful
 * acquire must be paired with a terminal_release_session() call once the
 * session ends (including on error paths).
 *
 * @return 0 on success, -1 if the session limit has been reached
 */
int terminal_acquire_session(void);

/**
 * Release a previously acquired terminal session slot.
 *
 * Safe to call from any thread. Decrements the active session counter.
 */
void terminal_release_session(void);

/**
 * Fork a new shell process attached to a pseudo-terminal.
 *
 * @param term Pointer to terminal context
 * @param shell Shell path to execute (NULL to auto-detect)
 * @return 0 on success, -1 on failure
 */
int terminal_start(terminal_t *term, const char *shell);

/**
 * Write data to the terminal's master PTY.
 *
 * @param term Pointer to terminal context
 * @param data Data buffer to write
 * @param len Length of data
 * @return Number of bytes written, -1 on failure
 */
int terminal_write(terminal_t *term, const char *data, size_t len);

/**
 * Resize the terminal window dimensions.
 *
 * @param term Pointer to terminal context
 * @param cols New column count
 * @param rows New row count
 * @return 0 on success, -1 on failure
 */
int terminal_resize(terminal_t *term, int cols, int rows);

/**
 * Set the callback invoked when output is read from the PTY.
 *
 * @param term Pointer to terminal context
 * @param cb Output callback function
 */
void terminal_set_output_cb(terminal_t *term, terminal_output_cb_t cb);

/**
 * Set user-defined data attached to the terminal context.
 *
 * @param term Pointer to terminal context
 * @param data User data pointer
 */
void terminal_set_user_data(terminal_t *term, void *data);

/**
 * Wait for the shell process to exit and return its exit status.
 *
 * @param term Pointer to terminal context
 * @return Exit status on success, -1 on failure
 */
int terminal_wait(terminal_t *term);

/**
 * Terminate the shell process group and clean up resources.
 *
 * @param term Pointer to terminal context
 */
void terminal_terminate(terminal_t *term);

#endif
