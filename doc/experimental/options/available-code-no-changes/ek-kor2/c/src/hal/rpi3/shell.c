/**
 * @file shell.c
 * @brief UART Terminal Shell for EK-KOR
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#include "shell.h"
#include "uart.h"
#include "timer.h"
#include "mailbox.h"
#include "ekk/ekk_fs.h"
#include "smp.h"
#include "usb_dwc2.h"
#include "usb_hid.h"

#include <string.h>

/* ============================================================================
 * Configuration
 * ============================================================================ */

#define SHELL_MAX_LINE      128     /* Maximum command line length */
#define SHELL_MAX_ARGS      16      /* Maximum arguments per command */
#define SHELL_VERSION       "1.0"

/* ============================================================================
 * Private Types
 * ============================================================================ */

typedef void (*shell_cmd_func_t)(int argc, char **argv);

typedef struct {
    const char *name;
    const char *help;
    shell_cmd_func_t func;
} shell_cmd_t;

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static void cmd_help(int argc, char **argv);
static void cmd_ls(int argc, char **argv);
static void cmd_cat(int argc, char **argv);
static void cmd_write(int argc, char **argv);
static void cmd_rm(int argc, char **argv);
static void cmd_stat(int argc, char **argv);
static void cmd_df(int argc, char **argv);
static void cmd_info(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_echo(int argc, char **argv);

/* ============================================================================
 * Command Table
 * ============================================================================ */

static const shell_cmd_t g_commands[] = {
    { "help",   "List available commands",      cmd_help   },
    { "ls",     "List all files",               cmd_ls     },
    { "cat",    "Display file contents",        cmd_cat    },
    { "write",  "Write text to file",           cmd_write  },
    { "rm",     "Delete file",                  cmd_rm     },
    { "stat",   "Show file information",        cmd_stat   },
    { "df",     "Show filesystem statistics",   cmd_df     },
    { "info",   "Show system information",      cmd_info   },
    { "clear",  "Clear screen",                 cmd_clear  },
    { "echo",   "Echo text",                    cmd_echo   },
    { "reboot", "Reboot system",                cmd_reboot },
    { NULL,     NULL,                           NULL       }
};

/* ============================================================================
 * Private Functions
 * ============================================================================ */

/**
 * @brief Get character from any input source (USB keyboard or UART)
 *
 * @return Character if available, -1 if none
 */
static int shell_getchar(void)
{
    /* Poll USB for new input */
    usb_poll();

    /* Check USB keyboard first */
    if (usb_hid_available()) {
        return usb_hid_getchar();
    }

    /* Check UART */
    if (uart_rx_ready()) {
        return uart_getchar();
    }

    return -1;
}

/**
 * @brief Read a line with echo and backspace support
 *
 * Reads from both USB keyboard and UART serial console.
 *
 * @param buf Buffer to store line (null-terminated)
 * @param max Maximum buffer size
 * @return Length of line (excluding null terminator)
 */
static int shell_read_line(char *buf, int max)
{
    int pos = 0;

    while (pos < max - 1) {
        int c = shell_getchar();

        if (c < 0) {
            /* No input available - yield and retry */
            __asm__ volatile("yield");
            continue;
        }

        if (c == '\r' || c == '\n') {
            /* End of line */
            uart_puts("\r\n");
            break;
        } else if (c == '\b' || c == 0x7F) {
            /* Backspace */
            if (pos > 0) {
                pos--;
                uart_puts("\b \b");  /* Erase character */
            }
        } else if (c == 0x03) {
            /* Ctrl+C - cancel line */
            uart_puts("^C\r\n");
            pos = 0;
            break;
        } else if (c >= ' ' && c < 0x7F) {
            /* Printable character */
            buf[pos++] = c;
            uart_putchar(c);  /* Echo */
        }
        /* Ignore other control characters */
    }

    buf[pos] = '\0';
    return pos;
}

/**
 * @brief Parse command line into arguments
 *
 * @param line Command line (will be modified)
 * @param argv Array to store argument pointers
 * @return Number of arguments
 */
static int shell_parse(char *line, char **argv)
{
    int argc = 0;
    char *p = line;

    while (*p && argc < SHELL_MAX_ARGS) {
        /* Skip whitespace */
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '\0') {
            break;
        }

        /* Check for quoted string */
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            /* Find closing quote */
            while (*p && *p != '"') {
                p++;
            }
            if (*p == '"') {
                *p++ = '\0';
            }
        } else {
            /* Regular argument */
            argv[argc++] = p;
            /* Find end of argument */
            while (*p && *p != ' ' && *p != '\t') {
                p++;
            }
            if (*p) {
                *p++ = '\0';
            }
        }
    }

    return argc;
}

/**
 * @brief Execute a command
 */
static void shell_execute(int argc, char **argv)
{
    if (argc == 0) {
        return;
    }

    const char *cmd_name = argv[0];

    /* Search command table */
    for (const shell_cmd_t *cmd = g_commands; cmd->name != NULL; cmd++) {
        if (strcmp(cmd_name, cmd->name) == 0) {
            cmd->func(argc, argv);
            return;
        }
    }

    uart_printf("Unknown command: %s\r\n", cmd_name);
    uart_puts("Type 'help' for available commands.\r\n");
}

/* ============================================================================
 * Command Implementations
 * ============================================================================ */

static void cmd_help(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    uart_puts("\r\nAvailable commands:\r\n");
    uart_puts("-------------------\r\n");

    for (const shell_cmd_t *cmd = g_commands; cmd->name != NULL; cmd++) {
        uart_printf("  %-8s - %s\r\n", cmd->name, cmd->help);
    }

    uart_puts("\r\nUsage examples:\r\n");
    uart_puts("  ls              - List all files\r\n");
    uart_puts("  cat myfile.txt  - Show file contents\r\n");
    uart_puts("  write test.txt Hello World\r\n");
    uart_puts("  rm test.txt     - Delete file\r\n");
    uart_puts("\r\n");
}

static void cmd_ls(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* Get filesystem stats to know total files */
    ekk_fs_statfs_t statfs;
    if (ekk_fs_statfs(&statfs) != EKK_FS_OK) {
        uart_puts("Error: Cannot read filesystem\r\n");
        return;
    }

    if (statfs.used_inodes == 0) {
        uart_puts("(no files)\r\n");
        return;
    }

    uart_puts("\r\n");
    uart_printf("%-15s %8s  %s\r\n", "NAME", "SIZE", "FLAGS");
    uart_puts("-------------------------------\r\n");

    /* List files by trying common names and inode numbers */
    /* Since EKKFS doesn't have directory enumeration, we try stat on files */
    /* This is a workaround - ideally EKKFS would have readdir() */

    /* For now, just show filesystem stats */
    uart_printf("\r\nFiles: %u (no directory enumeration in EKKFS)\r\n",
                statfs.used_inodes);
    uart_puts("Use 'stat <filename>' to check specific files.\r\n");
}

static void cmd_cat(int argc, char **argv)
{
    if (argc < 2) {
        uart_puts("Usage: cat <filename>\r\n");
        return;
    }

    const char *filename = argv[1];
    char buf[256];

    int bytes = ekk_fs_read_file(filename, buf, sizeof(buf) - 1);
    if (bytes < 0) {
        if (bytes == EKK_FS_ERR_NOT_FOUND) {
            uart_printf("File not found: %s\r\n", filename);
        } else {
            uart_printf("Error reading file: %d\r\n", bytes);
        }
        return;
    }

    buf[bytes] = '\0';
    uart_puts(buf);

    /* Add newline if file doesn't end with one */
    if (bytes > 0 && buf[bytes - 1] != '\n') {
        uart_puts("\r\n");
    }
}

static void cmd_write(int argc, char **argv)
{
    if (argc < 3) {
        uart_puts("Usage: write <filename> <text...>\r\n");
        uart_puts("Example: write test.txt Hello World\r\n");
        return;
    }

    const char *filename = argv[1];

    /* Concatenate remaining arguments with spaces */
    char buf[256];
    int pos = 0;

    for (int i = 2; i < argc && pos < (int)sizeof(buf) - 2; i++) {
        if (i > 2) {
            buf[pos++] = ' ';
        }
        int len = strlen(argv[i]);
        if (pos + len >= (int)sizeof(buf) - 1) {
            len = sizeof(buf) - pos - 2;
        }
        memcpy(buf + pos, argv[i], len);
        pos += len;
    }
    buf[pos++] = '\n';
    buf[pos] = '\0';

    int result = ekk_fs_write_file(filename, buf, pos, 0);
    if (result == EKK_FS_OK) {
        uart_printf("Wrote %d bytes to %s\r\n", pos, filename);
    } else {
        uart_printf("Error writing file: %d\r\n", result);
    }
}

static void cmd_rm(int argc, char **argv)
{
    if (argc < 2) {
        uart_puts("Usage: rm <filename>\r\n");
        return;
    }

    const char *filename = argv[1];

    int result = ekk_fs_delete(filename);
    if (result == EKK_FS_OK) {
        uart_printf("Deleted: %s\r\n", filename);
    } else if (result == EKK_FS_ERR_NOT_FOUND) {
        uart_printf("File not found: %s\r\n", filename);
    } else {
        uart_printf("Error deleting file: %d\r\n", result);
    }
}

static void cmd_stat(int argc, char **argv)
{
    if (argc < 2) {
        uart_puts("Usage: stat <filename>\r\n");
        return;
    }

    const char *filename = argv[1];
    ekk_fs_stat_t stat;

    int result = ekk_fs_stat(filename, &stat);
    if (result != EKK_FS_OK) {
        if (result == EKK_FS_ERR_NOT_FOUND) {
            uart_printf("File not found: %s\r\n", filename);
        } else {
            uart_printf("Error: %d\r\n", result);
        }
        return;
    }

    uart_printf("\r\nFile: %s\r\n", stat.name);
    uart_printf("  Size:     %u bytes\r\n", stat.size);
    uart_printf("  Inode:    %u\r\n", stat.inode_num);
    uart_printf("  Owner:    Module %u\r\n", stat.owner_id);
    uart_printf("  Flags:    0x%02x", stat.flags);
    if (stat.flags & EKK_FS_FLAG_SYSTEM) uart_puts(" SYSTEM");
    if (stat.flags & EKK_FS_FLAG_LOG)    uart_puts(" LOG");
    if (stat.flags & EKK_FS_FLAG_MODULE) uart_puts(" MODULE");
    uart_puts("\r\n");

    /* Display timestamps */
    uint64_t now = timer_get_us();
    if (stat.created > 0) {
        uint64_t age = (now - stat.created) / 1000000;  /* seconds */
        uart_printf("  Created:  %llu seconds ago\r\n", age);
    }
    if (stat.modified > 0) {
        uint64_t age = (now - stat.modified) / 1000000;  /* seconds */
        uart_printf("  Modified: %llu seconds ago\r\n", age);
    }
    uart_puts("\r\n");
}

static void cmd_df(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    ekk_fs_statfs_t statfs;
    int result = ekk_fs_statfs(&statfs);

    if (result != EKK_FS_OK) {
        uart_printf("Error reading filesystem: %d\r\n", result);
        return;
    }

    uart_puts("\r\nFilesystem statistics:\r\n");
    uart_puts("----------------------\r\n");
    uart_printf("  Blocks:  %u total, %u free (%u%% used)\r\n",
                statfs.total_blocks,
                statfs.free_blocks,
                (statfs.total_blocks - statfs.free_blocks) * 100 / statfs.total_blocks);
    uart_printf("  Inodes:  %u total, %u used (%u%% used)\r\n",
                statfs.total_inodes,
                statfs.used_inodes,
                statfs.used_inodes * 100 / statfs.total_inodes);

    /* Calculate approximate sizes */
    uint32_t block_size = 512;  /* EKKFS uses 512-byte blocks */
    uint32_t total_kb = (statfs.total_blocks * block_size) / 1024;
    uint32_t free_kb = (statfs.free_blocks * block_size) / 1024;

    uart_printf("  Space:   %u KB total, %u KB free\r\n", total_kb, free_kb);
    uart_puts("\r\n");
}

static void cmd_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    uart_puts("\r\nSystem Information:\r\n");
    uart_puts("-------------------\r\n");

    /* Platform */
    uart_puts("  Platform: Raspberry Pi 3B+ (BCM2837B0)\r\n");
    uart_printf("  CPU:      Cortex-A53 x 4 (Core %d active)\r\n", smp_get_core_id());

    /* Temperature */
    int32_t temp = mbox_get_temperature();
    if (temp > 0) {
        uart_printf("  Temp:     %d.%d C\r\n", temp / 1000, (temp % 1000) / 100);
    } else {
        uart_puts("  Temp:     N/A\r\n");
    }

    /* Clock */
    uint32_t clock = mbox_get_arm_clock();
    if (clock > 0) {
        uart_printf("  Clock:    %d MHz\r\n", clock / 1000000);
    } else {
        uart_puts("  Clock:    N/A\r\n");
    }

    /* Uptime */
    uint64_t uptime_us = timer_get_us();
    uint32_t uptime_s = uptime_us / 1000000;
    uint32_t hours = uptime_s / 3600;
    uint32_t mins = (uptime_s % 3600) / 60;
    uint32_t secs = uptime_s % 60;
    uart_printf("  Uptime:   %02d:%02d:%02d\r\n", hours, mins, secs);

    /* Memory (placeholder - would need mailbox query) */
    uart_puts("  Memory:   1 GB (shared with GPU)\r\n");

    /* USB status */
    if (usb_keyboard_ready()) {
        uart_puts("  USB:      Keyboard connected\r\n");
    } else if (usb_device_connected()) {
        uart_puts("  USB:      Device connected (not keyboard)\r\n");
    } else {
        uart_puts("  USB:      No device\r\n");
    }

    uart_puts("\r\n");
}

static void cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    /* ANSI escape sequence to clear screen and move cursor home */
    uart_puts("\033[2J\033[H");
}

static void cmd_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (i > 1) uart_putchar(' ');
        uart_puts(argv[i]);
    }
    uart_puts("\r\n");
}

static void cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    uart_puts("Rebooting...\r\n");
    timer_delay_ms(100);

    /* Use watchdog to trigger reboot */
    /* PM_WDOG and PM_RSTC registers */
    #define PM_BASE         0x3F100000
    #define PM_RSTC         (PM_BASE + 0x1C)
    #define PM_WDOG         (PM_BASE + 0x24)
    #define PM_PASSWORD     0x5A000000
    #define PM_RSTC_WRCFG_FULL_RESET 0x00000020

    volatile uint32_t *wdog = (volatile uint32_t *)PM_WDOG;
    volatile uint32_t *rstc = (volatile uint32_t *)PM_RSTC;

    *wdog = PM_PASSWORD | 1;  /* Set timeout to minimum */
    *rstc = PM_PASSWORD | PM_RSTC_WRCFG_FULL_RESET;

    /* Wait for watchdog */
    while (1) {
        __asm__ volatile("wfe");
    }
}

/* ============================================================================
 * Public Functions
 * ============================================================================ */

void shell_init(void)
{
    uart_puts("\r\n");
    uart_puts("EK-KOR Shell v" SHELL_VERSION "\r\n");
    uart_puts("Type 'help' for commands.\r\n");

    /* Wait for filesystem to be ready */
    uart_puts("Waiting for filesystem...");

    int timeout = 50;  /* 5 seconds max */
    while (!ekk_fs_is_ready() && timeout > 0) {
        timer_delay_ms(100);
        uart_putchar('.');
        timeout--;
    }

    if (ekk_fs_is_ready()) {
        uart_puts(" OK\r\n");
    } else {
        uart_puts(" TIMEOUT (filesystem not available)\r\n");
    }

    uart_puts("\r\n");
}

void shell_run(void)
{
    static char line[SHELL_MAX_LINE];
    static char *argv[SHELL_MAX_ARGS];

    while (1) {
        /* Print prompt */
        uart_puts("> ");

        /* Read command line */
        int len = shell_read_line(line, sizeof(line));

        if (len == 0) {
            continue;
        }

        /* Parse and execute */
        int argc = shell_parse(line, argv);
        shell_execute(argc, argv);
    }
}
