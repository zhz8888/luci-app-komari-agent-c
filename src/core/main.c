/*
 * Komari Agent entry point: argument parsing, configuration loading,
 * WebSocket connection, reporting/heartbeat threads and Web SSH
 * terminal session handling.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>

#include "config.h"
#include "monitoring.h"
#include "websocket.h"
#include "report.h"
#include "netstatic.h"
#include "terminal.h"
#include "utils.h"
#include "logger.h"
#include "ping.h"
#include "virtual.h"
#include "idn.h"
#include "version.h"
#include "cJSON.h"
#include "autodiscovery.h"
#include "update.h"

#define DEFAULT_INTERVAL 1.0

static volatile sig_atomic_t g_running = 1;
static agent_config_t g_config;
static ws_client_t *g_ws_client = NULL;
static netstatic_t *g_netstatic = NULL;

/**
 * Signal handler for graceful shutdown (SIGINT, SIGTERM).
 *
 * Sets the global running flag to 0 so worker threads can exit their
 * loops. Only async-signal-safe operations are performed here; no
 * logging/printf/syslog calls which are non-reentrant and could
 * deadlock if the signal interrupts the thread currently holding
 * their internal locks.
 *
 * @param sig Received signal number
 */
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/**
 * Print program usage information and available command-line options.
 *
 * @param prog Program name (usually argv[0])
 */
static void print_usage(const char *prog) {
    printf("Komari Agent (C Language Version) v%s\n", KOMARI_AGENT_C_VERSION_STRING);
    printf("\nUsage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  -t, --token <token>       Authentication token\n");
    printf("  -e, --endpoint <url>      Panel server URL\n");
    printf("  -i, --interval <seconds>  Report interval (default: %.1f)\n", DEFAULT_INTERVAL);
    printf("  -d, --dns <server>        Custom DNS server\n");
    printf("  -c, --config <file>       Configuration file path\n");
    printf("  -k, --insecure            Ignore certificate errors\n");
    printf("  -s, --disable-ssh         Disable Web SSH\n");
    printf("  -v, --verbose             Verbose output\n");
    printf("  -h, --help                Show this help message\n");
    printf("\nEnvironment variables:\n");
    printf("  AGENT_TOKEN               Authentication token\n");
    printf("  AGENT_ENDPOINT            Panel server URL\n");
    printf("  AGENT_INTERVAL            Report interval\n");
    printf("  AGENT_CUSTOM_DNS          Custom DNS server\n");
    printf("  AGENT_IGNORE_UNSAFE_CERT  Ignore certificate errors (true/false)\n");
    printf("  AGENT_DISABLE_WEB_SSH     Disable Web SSH (true/false)\n");
}

/* Terminal session context */
typedef struct {
    ws_client_t *ws;           /* Dedicated WebSocket connection for terminal */
    terminal_t *term;          /* Pseudo-terminal */
    volatile bool active;      /* Whether the session is active */
    pthread_t monitor_thread;  /* Monitor thread for cleanup */
    char ws_endpoint[512];     /* WebSocket endpoint URL (must match ws lifetime) */
    char extra_query[80];      /* Extra query parameters (must match ws lifetime) */
} terminal_session_t;

/* Terminal output callback: send pseudo-terminal output via WebSocket (terminal→WS output thread) */
static void on_terminal_output(terminal_t *term, const char *data, size_t len) {
    terminal_session_t *session = (terminal_session_t *)term->user_data;
    if (session && session->active && session->ws) {
        if (ws_client_send_text(session->ws, data, len) != 0) {
            session->active = false;
        }
    }
}

/* Terminal WebSocket raw data callback: handle terminal input and resize messages (WS→terminal input thread) */
static void on_terminal_ws_data(ws_client_t *client, const char *data, size_t len) {
    terminal_session_t *session = (terminal_session_t *)client->user_data;
    if (!session || !session->active || !session->term) return;

    /* Detect and handle resize JSON messages */
    if (len > 0 && data[0] == '{') {
        cJSON *root = cJSON_Parse(data);
        if (root) {
            cJSON *type = cJSON_GetObjectItem(root, "type");
            if (type && cJSON_IsString(type) && strcmp(type->valuestring, "resize") == 0) {
                cJSON *cols = cJSON_GetObjectItem(root, "cols");
                cJSON *rows = cJSON_GetObjectItem(root, "rows");
                if (cols && rows && cJSON_IsNumber(cols) && cJSON_IsNumber(rows)) {
                    terminal_resize(session->term, cols->valueint, rows->valueint);
                    KOMARI_LOG_DEBUG("[Terminal] Resized to cols=%d rows=%d",
                                     cols->valueint, rows->valueint);
                }
                cJSON_Delete(root);
                return;
            }
            cJSON_Delete(root);
        }
    }

    /* Normal terminal input: write to pseudo-terminal */
    terminal_write(session->term, data, len);
}

/* Terminal session monitor thread: detect exit and cleanup resources */
static void *terminal_monitor_thread(void *arg) {
    terminal_session_t *session = (terminal_session_t *)arg;

    while (session->active) {
        sleep(1);

        /* Check if terminal has exited (shell closed) */
        if (session->term && !session->term->running) {
            KOMARI_LOG_INFO("[Terminal] Shell exited, closing session");
            break;
        }

        /* Check if WebSocket has disconnected */
        if (session->ws) {
            pthread_mutex_lock(&session->ws->state_mutex);
            bool connected = session->ws->connected;
            pthread_mutex_unlock(&session->ws->state_mutex);
            if (!connected) {
                KOMARI_LOG_INFO("[Terminal] WebSocket disconnected, closing session");
                break;
            }
        }
    }

    session->active = false;

    /* Destroy terminal first (join read thread, ensure output callback no longer accesses ws) */
    if (session->term) {
        terminal_destroy(session->term);
        session->term = NULL;
    }

    /* Then disconnect and destroy WebSocket (join receive thread, ensure raw data callback no longer accesses term) */
    if (session->ws) {
        ws_client_stop(session->ws);
        ws_client_disconnect(session->ws);
        ws_client_destroy(session->ws);
        session->ws = NULL;
    }

    free(session);
    return NULL;
}

/* Establish dedicated WebSocket connection for terminal and start pseudo-terminal session */
static int establish_terminal_connection(const char *token, const char *request_id, const char *endpoint) {
    /* Allocate terminal session context */
    terminal_session_t *session = calloc(1, sizeof(terminal_session_t));
    if (!session) {
        KOMARI_LOG_ERROR("[Terminal] Failed to allocate terminal session");
        return -1;
    }
    session->active = false;

    /* Build WebSocket endpoint URL: convert http(s):// to ws(s):// and append /api/clients/terminal */
    const char *ep = endpoint;
    const char *scheme = "ws://";
    if (strncmp(ep, "http://", 7) == 0) {
        scheme = "ws://";
        ep += 7;
    } else if (strncmp(ep, "https://", 8) == 0) {
        scheme = "wss://";
        ep += 8;
    }

    const char *path_start = strchr(ep, '/');
    if (path_start) {
        size_t host_len = (size_t)(path_start - ep);
        snprintf(session->ws_endpoint, sizeof(session->ws_endpoint),
                 "%s%.*s/api/clients/terminal", scheme, (int)host_len, ep);
    } else {
        snprintf(session->ws_endpoint, sizeof(session->ws_endpoint),
                 "%s%s/api/clients/terminal", scheme, ep);
    }

    /* IDN domain name conversion */
    char ascii_endpoint[512];
    if (idn_convert_url_to_ascii(session->ws_endpoint, ascii_endpoint, sizeof(ascii_endpoint)) == 0) {
        strncpy(session->ws_endpoint, ascii_endpoint, sizeof(session->ws_endpoint) - 1);
        session->ws_endpoint[sizeof(session->ws_endpoint) - 1] = '\0';
    }

    /* Build extra query parameters */
    snprintf(session->extra_query, sizeof(session->extra_query), "&id=%s", request_id);

    /* Create WebSocket client */
    ws_client_config_t ws_config = {0};
    ws_config.endpoint = session->ws_endpoint;
    ws_config.token = (char *)token;
    ws_config.extra_query = session->extra_query;
    ws_config.ignore_cert = g_config.ignore_unsafe_cert;
    ws_config.max_retries = 1;
    ws_config.reconnect_interval = 1;

    session->ws = ws_client_create(&ws_config);
    if (!session->ws) {
        KOMARI_LOG_ERROR("[Terminal] Failed to create WebSocket client");
        free(session);
        return -1;
    }

    /* Set raw data callback and user data */
    ws_client_set_raw_handler(session->ws, on_terminal_ws_data);
    ws_client_set_user_data(session->ws, session);

    /* Connect WebSocket */
    if (ws_client_connect(session->ws) != 0) {
        KOMARI_LOG_ERROR("[Terminal] Failed to connect WebSocket for %s", request_id);
        ws_client_destroy(session->ws);
        free(session);
        return -1;
    }

    /* Create pseudo-terminal (default 80x24, can be adjusted later via resize) */
    session->term = terminal_create(80, 24);
    if (!session->term) {
        KOMARI_LOG_ERROR("[Terminal] Failed to create terminal");
        ws_client_disconnect(session->ws);
        ws_client_destroy(session->ws);
        free(session);
        return -1;
    }

    /* Set terminal output callback and user data */
    terminal_set_user_data(session->term, session);
    terminal_set_output_cb(session->term, on_terminal_output);

    /* Start terminal (forkpty + shell + read thread) */
    if (terminal_start(session->term, NULL) != 0) {
        KOMARI_LOG_ERROR("[Terminal] Failed to start terminal");
        terminal_destroy(session->term);
        ws_client_disconnect(session->ws);
        ws_client_destroy(session->ws);
        free(session);
        return -1;
    }

    session->active = true;

    /* Start monitor thread (detached, self-cleaning) */
    if (pthread_create(&session->monitor_thread, NULL, terminal_monitor_thread, session) != 0) {
        KOMARI_LOG_ERROR("[Terminal] Failed to create monitor thread");
        session->active = false;
        terminal_destroy(session->term);
        ws_client_disconnect(session->ws);
        ws_client_destroy(session->ws);
        free(session);
        return -1;
    }
    pthread_detach(session->monitor_thread);

    KOMARI_LOG_INFO("[Terminal] Session established: %s", request_id);
    return 0;
}

/**
 * Parse a command string into a NULL-terminated argv array without invoking a
 * shell. Tokens are split on whitespace; runs enclosed in single or double
 * quotes are kept together (the surrounding quotes are removed). No escape
 * sequence processing is performed, so shell metacharacters (|, >, ;, ...) end
 * up as literal argument characters instead of being interpreted.
 *
 * On success returns a malloc'd argv array and stores a malloc'd buffer
 * (holding the tokenized string) in *buf_out. The caller MUST free(argv) and
 * free(*buf_out). Returns NULL on allocation failure or when no tokens are
 * present.
 */
static char **parse_exec_command_argv(const char *cmd, char **buf_out) {
    if (!cmd || !buf_out) return NULL;

    size_t len = strlen(cmd);
    char *buf = malloc(len + 1);
    if (!buf) return NULL;
    memcpy(buf, cmd, len + 1);

    /* Upper bound on tokens: every token needs at least one separator */
    size_t max_argv = len / 2 + 2;
    char **argv = calloc(max_argv, sizeof(char *));
    if (!argv) {
        free(buf);
        return NULL;
    }

    int argc = 0;
    char *p = buf;
    while (*p) {
        /* Skip leading whitespace */
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (*p == '\0') break;

        char *token_start = p;
        char *write = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
            if (*p == '"') {
                p++;
                while (*p && *p != '"') {
                    *write++ = *p++;
                }
                if (*p == '"') p++;
            } else if (*p == '\'') {
                p++;
                while (*p && *p != '\'') {
                    *write++ = *p++;
                }
                if (*p == '\'') p++;
            } else {
                *write++ = *p++;
            }
        }
        *write = '\0';
        argv[argc++] = token_start;
        if (*p) p++;
    }
    argv[argc] = NULL;

    if (argc == 0) {
        free(argv);
        free(buf);
        return NULL;
    }

    *buf_out = buf;
    return argv;
}

/**
 * Handle incoming WebSocket messages from the panel.
 *
 * Dispatches terminal requests (Web SSH), exec task commands and ping
 * tasks to their respective handlers, uploading results back to the
 * panel via the report module.
 *
 * @param client WebSocket client that received the message
 * @param msg    Parsed WebSocket message structure
 */
static void handle_ws_message(ws_client_t *client, const ws_message_t *msg) {
    (void)client;
    if (strcmp(msg->message, "terminal") == 0 || msg->terminal_id[0] != '\0') {
        if (!g_config.disable_web_ssh) {
            KOMARI_LOG_INFO("[Terminal] Received terminal request: %s", msg->terminal_id);
            establish_terminal_connection(g_config.token, msg->terminal_id, g_config.endpoint);
        } else {
            KOMARI_LOG_INFO("[Terminal] Web SSH is disabled");
        }
    } else if (strcmp(msg->message, "exec") == 0) {
        KOMARI_LOG_INFO("[Task] Executing command: %s (Task ID: %s)", msg->exec_command, msg->exec_task_id);

        char output[8192] = "";
        int exit_code = 0;

        /* Parse the command into an argv array and execute it directly via
         * execvp() to avoid shell injection (msg->exec_command originates from
         * the WebSocket panel and is untrusted input). */
        char *argv_buf = NULL;
        char **argv = parse_exec_command_argv(msg->exec_command, &argv_buf);
        if (argv) {
            if (utils_exec_command_argv(argv, output, sizeof(output), &exit_code) == 0) {
                report_upload_task_result(&g_config, msg->exec_task_id, output, exit_code,
                                           utils_get_current_timestamp());
            }
            free(argv);
            free(argv_buf);
        } else {
            KOMARI_LOG_WARN("[Task] Failed to parse exec command (Task ID: %s)", msg->exec_task_id);
        }
    } else if (strcmp(msg->message, "ping") == 0 || msg->ping_type[0] != '\0') {
        KOMARI_LOG_INFO("[Ping] Received ping request: %s (%s) Task ID: %u",
                        msg->ping_target, msg->ping_type, msg->ping_task_id);
        
        if (msg->ping_task_id > 0 && msg->ping_type[0] != '\0' && msg->ping_target[0] != '\0') {
            ping_task_config_t ping_config;
            memset(&ping_config, 0, sizeof(ping_config));
            ping_config.timeout_ms = PING_DEFAULT_TIMEOUT_MS;
            ping_config.high_latency_threshold_ms = PING_HIGH_LATENCY_THRESHOLD_MS;
            ping_config.high_latency_retries = PING_HIGH_LATENCY_RETRIES;
            if (g_config.custom_dns[0] != '\0') {
                strncpy(ping_config.custom_dns, g_config.custom_dns, sizeof(ping_config.custom_dns) - 1);
            }
            
            ping_task_result_t ping_result;
            int ret = ping_task_execute(msg->ping_target, msg->ping_type, &ping_config, &ping_result);
            if (ret == 0) {
                ping_result.task_id = msg->ping_task_id;
                report_upload_ping_result(&g_config, ping_result.task_id,
                                          ping_result.ping_type, ping_result.result,
                                          (uint64_t)ping_result.finished_at);
            } else {
                report_upload_ping_result(&g_config, msg->ping_task_id,
                                          msg->ping_type, -1,
                                          (uint64_t)time(NULL));
            }
        }
    }
}

/**
 * Reporting thread: maintains the WebSocket connection and periodically
 * sends status reports and basic info to the panel.
 *
 * @param arg Unused thread argument
 * @return NULL
 */
static void *report_thread(void *arg) {
    (void)arg;
    
    char report_buf[4096];
    time_t last_basic_info = 0;
    
    while (g_running) {
        pthread_mutex_lock(&g_ws_client->state_mutex);
        bool connected = g_ws_client->connected;
        pthread_mutex_unlock(&g_ws_client->state_mutex);
        
        if (!connected) {
            KOMARI_LOG_INFO("[WebSocket] Connecting to %s...", g_config.endpoint);
            
            int retries = 0;
            while (retries < g_config.max_retries && g_running) {
                if (ws_client_connect(g_ws_client) == 0) {
                    KOMARI_LOG_INFO("[WebSocket] Connected successfully");
                    
                    int len = report_generate_basic_info(&g_config, report_buf, sizeof(report_buf));
                    if (len > 0) {
                        ws_client_send_text(g_ws_client, report_buf, len);
                    }
                    break;
                }
                
                retries++;
                KOMARI_LOG_WARN("[WebSocket] Connection failed, retry %d/%d", retries, g_config.max_retries);
                sleep(g_config.reconnect_interval);
            }
            
            if (retries >= g_config.max_retries) {
                KOMARI_LOG_ERROR("[WebSocket] Max retries reached");
                break;
            }
        }
        
        int len = report_generate(&g_config, report_buf, sizeof(report_buf));
        
        pthread_mutex_lock(&g_ws_client->state_mutex);
        bool is_connected = g_ws_client->connected;
        pthread_mutex_unlock(&g_ws_client->state_mutex);
        
        if (len > 0 && is_connected) {
            if (ws_client_send_text(g_ws_client, report_buf, len) != 0) {
                KOMARI_LOG_ERROR("[WebSocket] Failed to send data");
                ws_client_disconnect(g_ws_client);
            }
        }
        
        time_t now = time(NULL);
        if (now - last_basic_info >= g_config.info_report_interval * 60) {
            len = report_generate_basic_info(&g_config, report_buf, sizeof(report_buf));
            
            pthread_mutex_lock(&g_ws_client->state_mutex);
            is_connected = g_ws_client->connected;
            pthread_mutex_unlock(&g_ws_client->state_mutex);
            
            if (len > 0 && is_connected) {
                ws_client_send_text(g_ws_client, report_buf, len);
            }
            last_basic_info = now;
        }
        
        sleep((unsigned int)g_config.interval);
    }
    
    return NULL;
}

/**
 * Heartbeat thread: sends a WebSocket ping every 30 seconds to keep the
 * connection alive.
 *
 * @param arg Unused thread argument
 * @return NULL
 */
static void *heartbeat_thread(void *arg) {
    (void)arg;

    while (g_running && g_ws_client) {
        /* Protect read of connected flag with state_mutex to avoid torn
         * reads on architectures without atomic word loads and to
         * synchronize with writers in websocket.c. */
        pthread_mutex_lock(&g_ws_client->state_mutex);
        bool connected = g_ws_client->connected;
        pthread_mutex_unlock(&g_ws_client->state_mutex);

        if (connected) {
            if (ws_client_send_ping(g_ws_client) != 0) {
                printf("[WebSocket] Heartbeat failed\n");
            }
        }
        sleep(30);
    }

    return NULL;
}

/**
 * Program entry point.
 *
 * Parses command-line arguments, loads configuration from environment,
 * file and UCI in order, performs auto-discovery registration if a key
 * is provided, then creates the WebSocket client and starts the
 * reporting, heartbeat and update-checker threads.
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, non-zero on failure
 */
int main(int argc, char *argv[]) {
    /* OpenSSL 1.0.2 requires manual library initialization; 1.1.0+ auto-initializes */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
#endif

    config_init(&g_config);
    
    static struct option long_options[] = {
        {"token",       required_argument, 0, 't'},
        {"endpoint",    required_argument, 0, 'e'},
        {"interval",    required_argument, 0, 'i'},
        {"dns",         required_argument, 0, 'd'},
        {"config",      required_argument, 0, 'c'},
        {"insecure",    no_argument,       0, 'k'},
        {"disable-ssh", no_argument,       0, 's'},
        {"verbose",     no_argument,       0, 'v'},
        {"help",        no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    
    /* Command-line values are saved to separate buffers and applied after all
     * other configuration sources are loaded. This guarantees command-line
     * arguments always win over UCI/JSON/env, matching the priority order:
     * command-line > env > UCI > JSON > defaults. Without this, later loaders
     * (e.g. UCI emitting an empty token) would silently overwrite values
     * explicitly passed on the command line. */
    char cli_token[MAX_TOKEN_LEN] = {0};
    char cli_endpoint[MAX_ENDPOINT_LEN] = {0};
    char cli_custom_dns[MAX_DNS_LEN] = {0};
    char cli_config_file[MAX_CONFIG_FILE_LEN] = {0};
    double cli_interval = 0.0;
    bool cli_set_token = false;
    bool cli_set_endpoint = false;
    bool cli_set_interval = false;
    bool cli_set_custom_dns = false;
    bool cli_set_config_file = false;
    bool cli_set_insecure = false;
    bool cli_set_disable_ssh = false;

    int opt;
    while ((opt = getopt_long(argc, argv, "t:e:i:d:c:ksvh", long_options, NULL)) != -1) {
        switch (opt) {
            case 't':
                strncpy(cli_token, optarg, sizeof(cli_token) - 1);
                cli_token[sizeof(cli_token) - 1] = '\0';
                cli_set_token = true;
                break;
            case 'e':
                strncpy(cli_endpoint, optarg, sizeof(cli_endpoint) - 1);
                cli_endpoint[sizeof(cli_endpoint) - 1] = '\0';
                cli_set_endpoint = true;
                break;
            case 'i':
                cli_interval = atof(optarg);
                cli_set_interval = true;
                break;
            case 'd':
                strncpy(cli_custom_dns, optarg, sizeof(cli_custom_dns) - 1);
                cli_custom_dns[sizeof(cli_custom_dns) - 1] = '\0';
                cli_set_custom_dns = true;
                break;
            case 'c':
                strncpy(cli_config_file, optarg, sizeof(cli_config_file) - 1);
                cli_config_file[sizeof(cli_config_file) - 1] = '\0';
                cli_set_config_file = true;
                break;
            case 'k':
                cli_set_insecure = true;
                break;
            case 's':
                cli_set_disable_ssh = true;
                break;
            case 'v':
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Resolve the JSON config file path: command-line -c takes priority over
     * the AGENT_CONFIG_FILE environment variable. Both must be resolved before
     * loading JSON so the correct file is read. */
    if (cli_set_config_file) {
        strncpy(g_config.config_file, cli_config_file, sizeof(g_config.config_file) - 1);
        g_config.config_file[sizeof(g_config.config_file) - 1] = '\0';
    } else {
        char *env_cfg = getenv("AGENT_CONFIG_FILE");
        if (env_cfg && env_cfg[0] != '\0') {
            strncpy(g_config.config_file, env_cfg, sizeof(g_config.config_file) - 1);
            g_config.config_file[sizeof(g_config.config_file) - 1] = '\0';
        }
    }

    /* Load configuration sources in priority order from lowest to highest:
     * JSON file -> UCI -> env. Defaults are already set by config_init.
     * Command-line values are applied last to guarantee they win. */
    if (g_config.config_file[0] != '\0') {
        config_load_from_file(&g_config, g_config.config_file);
    }

    config_load_from_uci(&g_config);

    config_load_from_env(&g_config);

    /* Apply command-line arguments last so they take precedence over every
     * other source. Also restore the config_file field, which
     * config_load_from_env may have overwritten with AGENT_CONFIG_FILE even
     * though JSON was already loaded from the command-line path. */
    if (cli_set_token) {
        strncpy(g_config.token, cli_token, sizeof(g_config.token) - 1);
        g_config.token[sizeof(g_config.token) - 1] = '\0';
    }
    if (cli_set_endpoint) {
        strncpy(g_config.endpoint, cli_endpoint, sizeof(g_config.endpoint) - 1);
        g_config.endpoint[sizeof(g_config.endpoint) - 1] = '\0';
    }
    if (cli_set_interval) {
        g_config.interval = cli_interval;
    }
    if (cli_set_custom_dns) {
        strncpy(g_config.custom_dns, cli_custom_dns, sizeof(g_config.custom_dns) - 1);
        g_config.custom_dns[sizeof(g_config.custom_dns) - 1] = '\0';
    }
    if (cli_set_insecure) {
        g_config.ignore_unsafe_cert = true;
    }
    if (cli_set_disable_ssh) {
        g_config.disable_web_ssh = true;
    }
    if (cli_set_config_file) {
        strncpy(g_config.config_file, cli_config_file, sizeof(g_config.config_file) - 1);
        g_config.config_file[sizeof(g_config.config_file) - 1] = '\0';
    }

    /* Auto-discovery: if auto_discovery_key is configured and token is empty, attempt auto-registration */
    if (g_config.auto_discovery_key[0] != '\0') {
        KOMARI_LOG_INFO("[AutoDiscovery] Auto-discovery key detected, attempting registration");
        if (autodiscovery_handle(g_config.endpoint,
                                  g_config.auto_discovery_key,
                                  g_config.token,
                                  sizeof(g_config.token)) == 0) {
            KOMARI_LOG_INFO("[AutoDiscovery] Auto-discovery succeeded, token acquired");
        } else {
            KOMARI_LOG_WARN("[AutoDiscovery] Auto-discovery failed");
        }
    }

    /* Pass configuration to monitoring module to activate options like memory_include_cache */
    monitoring_set_config(&g_config);

    if (g_config.token[0] == '\0' || g_config.endpoint[0] == '\0') {
        fprintf(stderr, "Error: token and endpoint are required\n");
        print_usage(argv[0]);
        return 1;
    }
    
    if (g_config.interval <= 0) {
        g_config.interval = DEFAULT_INTERVAL;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("Komari Agent (C Language Version) v%s\n", KOMARI_AGENT_C_VERSION_STRING);
    printf("Connecting to: %s\n", g_config.endpoint);
    printf("Report interval: %.1f seconds\n", g_config.interval);
    
    char ws_endpoint[512];
    const char *ep = g_config.endpoint;
    if (strncmp(ep, "http://", 7) == 0) {
        snprintf(ws_endpoint, sizeof(ws_endpoint), "ws://%s", ep + 7);
    } else if (strncmp(ep, "https://", 8) == 0) {
        snprintf(ws_endpoint, sizeof(ws_endpoint), "wss://%s", ep + 8);
    } else {
        snprintf(ws_endpoint, sizeof(ws_endpoint), "ws://%s", ep);
    }
    
    /* IDN domain name conversion */
    char ascii_endpoint[512];
    if (idn_convert_url_to_ascii(ws_endpoint, ascii_endpoint, sizeof(ascii_endpoint)) == 0) {
        KOMARI_LOG_DEBUG("IDN converted: %s -> %s", ws_endpoint, ascii_endpoint);
        strncpy(ws_endpoint, ascii_endpoint, sizeof(ws_endpoint) - 1);
        ws_endpoint[sizeof(ws_endpoint) - 1] = '\0';
    } else {
        KOMARI_LOG_DEBUG("IDN conversion failed, using original URL");
    }
    
    ws_client_config_t ws_config = {
        .endpoint = ws_endpoint,
        .token = g_config.token,
        .ignore_cert = g_config.ignore_unsafe_cert,
        .max_retries = g_config.max_retries,
        .reconnect_interval = g_config.reconnect_interval,
        .report_interval = g_config.interval
    };
    
    g_ws_client = ws_client_create(&ws_config);
    if (!g_ws_client) {
        fprintf(stderr, "Error: Failed to create WebSocket client\n");
        return 1;
    }
    
    ws_client_set_handler(g_ws_client, handle_ws_message);
    
    if (g_config.month_rotate > 0) {
        g_netstatic = netstatic_create("/tmp/komari-netstatic.json");
        if (g_netstatic) {
            netstatic_start(g_netstatic);
        }
    }

    /* Start background update checker thread (if auto-update not disabled) */
    pthread_t update_tid;
    if (!g_config.disable_auto_update) {
        if (pthread_create(&update_tid, NULL, update_do_check_works, NULL) == 0) {
            pthread_detach(update_tid);
            KOMARI_LOG_INFO("[Update] Background update checker started");
        } else {
            KOMARI_LOG_WARN("[Update] Failed to start update checker thread");
        }
    }

    pthread_t report_tid, heartbeat_tid;

    if (pthread_create(&report_tid, NULL, report_thread, NULL) != 0) {
        KOMARI_LOG_ERROR("Failed to create report thread");
        if (g_netstatic) {
            netstatic_stop(g_netstatic);
            netstatic_destroy(g_netstatic);
        }
        ws_client_stop(g_ws_client);
        ws_client_disconnect(g_ws_client);
        ws_client_destroy(g_ws_client);
        logger_cleanup();
        return 1;
    }

    if (pthread_create(&heartbeat_tid, NULL, heartbeat_thread, NULL) != 0) {
        KOMARI_LOG_ERROR("Failed to create heartbeat thread");
        /* Signal report thread to exit and join it before cleanup so it
         * does not keep accessing the ws client we are about to destroy. */
        g_running = 0;
        pthread_join(report_tid, NULL);
        if (g_netstatic) {
            netstatic_stop(g_netstatic);
            netstatic_destroy(g_netstatic);
        }
        ws_client_stop(g_ws_client);
        ws_client_disconnect(g_ws_client);
        ws_client_destroy(g_ws_client);
        logger_cleanup();
        return 1;
    }
    
    while (g_running) {
        sleep(1);
    }

    printf("Stopping service...\n");

    /* Signal the detached update checker thread to exit its loop promptly
     * (within ~1 second). It cannot be joined because it was created with
     * pthread_detach, so we just flip its running flag and let it wind down
     * on its own while we tear down the rest of the resources below. */
    update_stop();

    if (g_netstatic) {
        netstatic_stop(g_netstatic);
        netstatic_destroy(g_netstatic);
    }
    
    if (g_ws_client) {
        ws_client_stop(g_ws_client);
        ws_client_disconnect(g_ws_client);
        ws_client_destroy(g_ws_client);
    }
    
    pthread_join(report_tid, NULL);
    pthread_join(heartbeat_tid, NULL);
    
    logger_cleanup();

    printf("Service stopped\n");

    /* OpenSSL 1.0.2 requires manual cleanup; 1.1.0+ auto-cleans */
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    EVP_cleanup();
    ERR_remove_state(0);
#endif

    return 0;
}
