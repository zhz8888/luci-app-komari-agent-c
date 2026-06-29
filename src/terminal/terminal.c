/*
 * Web terminal PTY management implementation.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

/* Enable _GNU_SOURCE to ensure musl libc exposes BSD extension function declarations such as forkpty */
#define _GNU_SOURCE
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <pty.h>
#include <utmp.h>
#include <pwd.h>

#include "terminal.h"
#include "utils.h"
#include "logger.h"
#include "motd.h"
#include "idn.h"

#define TERMINAL_BUFFER_SIZE 4096

/* Active terminal session counter and its protecting mutex. The mutex is
 * statically initialised so no explicit init call is required at startup. */
static volatile int active_sessions = 0;
static pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;

int terminal_acquire_session(void) {
    pthread_mutex_lock(&sessions_mutex);
    if (active_sessions >= MAX_TERMINAL_SESSIONS) {
        pthread_mutex_unlock(&sessions_mutex);
        KOMARI_LOG_WARN("Terminal session limit reached (%d), rejecting new session",
                        MAX_TERMINAL_SESSIONS);
        return -1;
    }
    active_sessions++;
    int count = active_sessions;
    pthread_mutex_unlock(&sessions_mutex);
    KOMARI_LOG_DEBUG("Terminal session acquired (%d/%d active)", count, MAX_TERMINAL_SESSIONS);
    return 0;
}

void terminal_release_session(void) {
    pthread_mutex_lock(&sessions_mutex);
    if (active_sessions > 0) {
        active_sessions--;
    }
    int count = active_sessions;
    pthread_mutex_unlock(&sessions_mutex);
    KOMARI_LOG_DEBUG("Terminal session released (%d/%d active)", count, MAX_TERMINAL_SESSIONS);
}

/* Terminal read thread function */
static void *terminal_read_thread(void *arg) {
    terminal_t *term = (terminal_t *)arg;
    char buf[TERMINAL_BUFFER_SIZE];
    fd_set fds;
    struct timeval tv;
    
    while (term->running) {
        FD_ZERO(&fds);
        FD_SET(term->master_fd, &fds);
        
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        
        int ret = select(term->master_fd + 1, &fds, NULL, NULL, &tv);
        if (ret > 0 && FD_ISSET(term->master_fd, &fds)) {
            ssize_t n = read(term->master_fd, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                if (term->output_cb) {
                    term->output_cb(term, buf, (size_t)n);
                }
            } else if (n == 0) {
                break;
            } else if (errno != EAGAIN && errno != EINTR) {
                break;
            }
        }
    }
    
    return NULL;
}

/* Find an available shell */
static const char *find_shell(void) {
    /* Get the user's default shell from /etc/passwd.
     * Use the reentrant getpwuid_r because getpwuid returns a pointer to a
     * shared static buffer that can be clobbered by other threads. The shell
     * path is copied into a stable static buffer so the caller can use it
     * after the getpwuid_r scratch buffer goes out of scope. */
    uid_t uid = getuid();
    struct passwd pwd;
    struct passwd *result = NULL;
    char buf[1024];
    static char shell_path[256];

    if (getpwuid_r(uid, &pwd, buf, sizeof(buf), &result) == 0 && result) {
        if (pwd.pw_shell && pwd.pw_shell[0] != '\0') {
            if (access(pwd.pw_shell, X_OK) == 0) {
                strncpy(shell_path, pwd.pw_shell, sizeof(shell_path) - 1);
                shell_path[sizeof(shell_path) - 1] = '\0';
                return shell_path;
            }
        }
    }

    /* Fall back to a list of common shells */
    const char *shells[] = {"/bin/bash", "/bin/zsh", "/bin/sh", "/bin/ash", NULL};
    for (int i = 0; shells[i]; i++) {
        if (access(shells[i], X_OK) == 0) {
            return shells[i];
        }
    }

    return NULL;
}

terminal_t *terminal_create(int cols, int rows) {
    terminal_t *term = calloc(1, sizeof(terminal_t));
    if (!term) return NULL;
    
    term->cols = cols > 0 ? cols : 80;
    term->rows = rows > 0 ? rows : 24;
    term->master_fd = -1;
    term->pid = -1;
    term->pgid = -1;
    term->running = false;
    
    return term;
}

int terminal_start(terminal_t *term, const char *shell) {
    if (!term) return -1;
    
    const char *sh = shell ? shell : find_shell();
    if (!sh) {
        KOMARI_LOG_ERROR("No available shell found");
        return -1;
    }
    
    struct winsize win;
    memset(&win, 0, sizeof(win));
    win.ws_col = term->cols;
    win.ws_row = term->rows;
    
    /* Get MOTD content */
    char motd_content[MOTD_MAX_OUTPUT_LEN];
    int has_motd = motd_get_content(motd_content, sizeof(motd_content)) == 0;
    
    term->pid = forkpty(&term->master_fd, NULL, NULL, &win);
    if (term->pid < 0) {
        KOMARI_LOG_ERROR("forkpty failed: %s", strerror(errno));
        return -1;
    }
    
    if (term->pid == 0) {
        /* Child process */
        setenv("TERM", "xterm-256color", 1);
        setenv("LANG", "C.UTF-8", 1);
        setenv("LC_ALL", "C.UTF-8", 1);

        /* Create a new process group */
        setpgid(0, 0);

        /* If MOTD content is available, display it first */
        if (has_motd) {
            ssize_t motd_len = (ssize_t)strlen(motd_content);
            if (write(STDOUT_FILENO, motd_content, (size_t)motd_len) < 0) {
                /* Ignore write errors */
            }
            if (write(STDOUT_FILENO, "\n", 1) < 0) {
                /* Ignore write errors */
            }
        }

        /* Execute the shell */
        execl(sh, sh, (char *)NULL);
        _exit(127);
    }

    /* Parent process */
    setpgid(term->pid, term->pid);
    term->pgid = term->pid;
    term->running = true;

    /* Create the read thread */
    if (pthread_create(&term->read_thread, NULL, terminal_read_thread, term) != 0) {
        KOMARI_LOG_ERROR("Failed to create terminal read thread");
        kill(term->pid, SIGKILL);
        waitpid(term->pid, NULL, 0);
        close(term->master_fd);
        term->master_fd = -1;
        term->running = false;
        return -1;
    }
    
    KOMARI_LOG_INFO("Terminal started with PID %d (PGID %d)", term->pid, term->pgid);
    return 0;
}

int terminal_write(terminal_t *term, const char *data, size_t len) {
    if (!term || !data || len == 0) return -1;

    if (term->master_fd < 0 || !term->running) {
        return -1;
    }

    /* Loop until all bytes are written; write() may return a short count on
     * a PTY, and EINTR must be retried. */
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(term->master_fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break;
        written += (size_t)n;
    }
    return (int)written;
}

int terminal_resize(terminal_t *term, int cols, int rows) {
    if (!term) return -1;
    
    term->cols = cols;
    term->rows = rows;
    
    if (term->master_fd < 0) return -1;
    
    struct winsize win;
    memset(&win, 0, sizeof(win));
    win.ws_col = cols;
    win.ws_row = rows;
    
    return ioctl(term->master_fd, TIOCSWINSZ, &win);
}

void terminal_set_output_cb(terminal_t *term, terminal_output_cb_t cb) {
    if (term) term->output_cb = cb;
}

void terminal_set_user_data(terminal_t *term, void *data) {
    if (term) term->user_data = data;
}

int terminal_wait(terminal_t *term) {
    if (!term || term->pid <= 0) return -1;

    int status;
    pid_t ret = waitpid(term->pid, &status, 0);
    if (ret > 0) {
        term->pid = -1;  /* mark as reaped to prevent double-reap */
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return -1;
}

void terminal_terminate(terminal_t *term) {
    if (!term) return;

    if (term->running) {
        term->running = false;

        if (term->pid > 0) {
            pid_t pid = term->pid;
            int pgid = term->pgid > 0 ? term->pgid : pid;
            term->pid = -1;  /* mark as reaped to prevent pid reuse issues */

            KOMARI_LOG_INFO("Sending SIGTERM to process group %d...", pgid);
            kill(-pgid, SIGTERM);

            int retries = 50;
            while (retries > 0) {
                int status;
                pid_t result = waitpid(pid, &status, WNOHANG);
                if (result == pid) {
                    KOMARI_LOG_INFO("Process group %d exited gracefully", pgid);
                    break;
                }
                usleep(100000);
                retries--;
            }

            if (retries == 0) {
                KOMARI_LOG_WARN("Process group %d did not exit, sending SIGKILL", pgid);
                kill(-pgid, SIGKILL);
                waitpid(pid, NULL, 0);
            }
        }

        pthread_join(term->read_thread, NULL);
    }

    if (term->master_fd >= 0) {
        close(term->master_fd);
        term->master_fd = -1;
    }
}

void terminal_destroy(terminal_t **term) {
    if (!term || !*term) return;

    /* NULL the caller's pointer BEFORE freeing the underlying memory so that
     * other threads polling the pointer cannot observe a freed address. The
     * local copy is used for the actual teardown. This mirrors the Go
     * ws.SafeConn.Close pattern of signalling (NULLing) first, then cleaning
     * up resources. */
    terminal_t *t = *term;
    *term = NULL;

    terminal_terminate(t);
    free(t);
}
