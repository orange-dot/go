/**
 * @file shell.h
 * @brief UART Terminal Shell for EK-KOR
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Simple command-line shell with filesystem commands.
 * Runs on Core 0, communicates with FS server on Core 3 via IPC.
 */

#ifndef RPI3_SHELL_H
#define RPI3_SHELL_H

/**
 * @brief Initialize the shell
 *
 * Waits for filesystem to be ready before returning.
 */
void shell_init(void);

/**
 * @brief Run the shell main loop
 *
 * Blocking call that reads commands from UART and executes them.
 * Never returns.
 */
void shell_run(void);

#endif /* RPI3_SHELL_H */
