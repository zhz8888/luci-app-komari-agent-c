/*
 * MOTD (Message of the Day) interface declarations.
 *
 * Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
 * Licensed under MIT License
 */

#ifndef KOMARI_AGENT_C_MOTD_H
#define KOMARI_AGENT_C_MOTD_H

#include <stddef.h>

#define MOTD_MAX_OUTPUT_LEN 8192
#define MOTD_MAX_SCRIPTS 64
#define MOTD_SCRIPT_PATH "/etc/update-motd.d"
#define MOTD_FILE_PATH "/etc/motd"

/*
 * Get MOTD (Message of the Day) content
 * Executes in order:
 *   1. Executable scripts under /etc/update-motd.d/
 *   2. Contents of the /etc/motd file
 *
 * Parameters:
 *   output: Output buffer
 *   output_size: Output buffer size
 *
 * Returns:
 *   0: Success
 *   -1: Invalid argument
 *   -2: Read failed
 */
int motd_get_content(char *output, size_t output_size);

/*
 * Execute all executable scripts under /etc/update-motd.d/
 *
 * Parameters:
 *   output: Output buffer
 *   output_size: Output buffer size
 *
 * Returns:
 *   0: Success
 *   -1: Invalid argument
 */
int motd_execute_scripts(char *output, size_t output_size);

/*
 * Read the contents of the /etc/motd file
 *
 * Parameters:
 *   output: Output buffer
 *   output_size: Output buffer size
 *
 * Returns:
 *   0: Success
 *   -1: Invalid argument
 *   -2: File does not exist or read failed
 */
int motd_read_file(char *output, size_t output_size);

#endif
