/**
 * @file usb_hid.c
 * @brief USB HID Keyboard Driver Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Implements USB HID boot protocol keyboard support.
 * Converts USB HID scancodes to ASCII characters.
 */

#include "usb_hid.h"
#include "usb_dwc2.h"
#include "timer.h"
#include "uart.h"

#include <string.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

#define HID_QUEUE_SIZE      64      /* Character queue size (power of 2) */
#define HID_QUEUE_MASK      (HID_QUEUE_SIZE - 1)

/* HID modifier key bits (first byte of report) */
#define HID_MOD_LEFT_CTRL   (1 << 0)
#define HID_MOD_LEFT_SHIFT  (1 << 1)
#define HID_MOD_LEFT_ALT    (1 << 2)
#define HID_MOD_LEFT_GUI    (1 << 3)
#define HID_MOD_RIGHT_CTRL  (1 << 4)
#define HID_MOD_RIGHT_SHIFT (1 << 5)
#define HID_MOD_RIGHT_ALT   (1 << 6)
#define HID_MOD_RIGHT_GUI   (1 << 7)

#define HID_MOD_CTRL        (HID_MOD_LEFT_CTRL | HID_MOD_RIGHT_CTRL)
#define HID_MOD_SHIFT       (HID_MOD_LEFT_SHIFT | HID_MOD_RIGHT_SHIFT)
#define HID_MOD_ALT         (HID_MOD_LEFT_ALT | HID_MOD_RIGHT_ALT)

/* ============================================================================
 * HID SCANCODE TABLES
 * ============================================================================ */

/*
 * USB HID Usage Tables (Chapter 10: Keyboard/Keypad Page)
 *
 * Boot protocol keyboard report format (8 bytes):
 *   [0] Modifier keys bitmap
 *   [1] Reserved (always 0)
 *   [2-7] Up to 6 simultaneous key scancodes (0 = no key)
 */

/* Scancode to ASCII (unshifted) */
static const char scancode_to_ascii[128] = {
    /* 0x00-0x03: Reserved/Error */
    0, 0, 0, 0,

    /* 0x04-0x1D: Letters a-z */
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
    'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
    'u', 'v', 'w', 'x', 'y', 'z',

    /* 0x1E-0x27: Numbers 1-9, 0 */
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',

    /* 0x28-0x2C: Enter, Escape, Backspace, Tab, Space */
    '\r', 0x1B, '\b', '\t', ' ',

    /* 0x2D-0x38: Punctuation */
    '-',    /* 0x2D: - and _ */
    '=',    /* 0x2E: = and + */
    '[',    /* 0x2F: [ and { */
    ']',    /* 0x30: ] and } */
    '\\',   /* 0x31: \ and | */
    '#',    /* 0x32: Non-US # and ~ (international) */
    ';',    /* 0x33: ; and : */
    '\'',   /* 0x34: ' and " */
    '`',    /* 0x35: ` and ~ */
    ',',    /* 0x36: , and < */
    '.',    /* 0x37: . and > */
    '/',    /* 0x38: / and ? */

    /* 0x39: Caps Lock */
    0,

    /* 0x3A-0x45: F1-F12 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    /* 0x46-0x48: PrintScreen, ScrollLock, Pause */
    0, 0, 0,

    /* 0x49-0x4E: Insert, Home, PageUp, Delete, End, PageDown */
    0, 0, 0, 0x7F, 0, 0,

    /* 0x4F-0x52: Arrow keys (Right, Left, Down, Up) */
    0, 0, 0, 0,

    /* 0x53: Num Lock */
    0,

    /* 0x54-0x63: Keypad */
    '/', '*', '-', '+', '\r',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.',

    /* 0x64: Non-US \ and | */
    '\\',

    /* Remaining: reserved/unused */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Scancode to ASCII (shifted) */
static const char scancode_to_ascii_shifted[128] = {
    /* 0x00-0x03: Reserved/Error */
    0, 0, 0, 0,

    /* 0x04-0x1D: Letters A-Z */
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
    'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
    'U', 'V', 'W', 'X', 'Y', 'Z',

    /* 0x1E-0x27: Shifted numbers */
    '!', '@', '#', '$', '%', '^', '&', '*', '(', ')',

    /* 0x28-0x2C: Same as unshifted */
    '\r', 0x1B, '\b', '\t', ' ',

    /* 0x2D-0x38: Shifted punctuation */
    '_',    /* 0x2D */
    '+',    /* 0x2E */
    '{',    /* 0x2F */
    '}',    /* 0x30 */
    '|',    /* 0x31 */
    '~',    /* 0x32 */
    ':',    /* 0x33 */
    '"',    /* 0x34 */
    '~',    /* 0x35 */
    '<',    /* 0x36 */
    '>',    /* 0x37 */
    '?',    /* 0x38 */

    /* 0x39: Caps Lock */
    0,

    /* 0x3A-0x45: F1-F12 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

    /* 0x46-0x48: PrintScreen, ScrollLock, Pause */
    0, 0, 0,

    /* 0x49-0x4E: Insert, Home, PageUp, Delete, End, PageDown */
    0, 0, 0, 0x7F, 0, 0,

    /* 0x4F-0x52: Arrow keys */
    0, 0, 0, 0,

    /* 0x53: Num Lock */
    0,

    /* 0x54-0x63: Keypad (same as unshifted) */
    '/', '*', '-', '+', '\r',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '.',

    /* 0x64: Non-US \ and | */
    '|',

    /* Remaining: reserved/unused */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* ============================================================================
 * DRIVER STATE
 * ============================================================================ */

static struct {
    /* USB device info */
    uint8_t     addr;
    uint8_t     endpoint;
    uint8_t     interval_ms;
    int         initialized;

    /* Polling state */
    uint64_t    last_poll_us;

    /* Previous report (for detecting key changes) */
    uint8_t     prev_report[8];

    /* Keyboard state */
    uint8_t     caps_lock;
    uint8_t     num_lock;

    /* Character queue (ring buffer) */
    char        queue[HID_QUEUE_SIZE];
    uint8_t     queue_head;     /* Next read position */
    uint8_t     queue_tail;     /* Next write position */
} g_hid = {0};

/* ============================================================================
 * PRIVATE FUNCTIONS
 * ============================================================================ */

/**
 * @brief Add character to queue
 */
static void hid_queue_put(char c)
{
    uint8_t next_tail = (g_hid.queue_tail + 1) & HID_QUEUE_MASK;

    if (next_tail != g_hid.queue_head) {
        g_hid.queue[g_hid.queue_tail] = c;
        g_hid.queue_tail = next_tail;
    }
    /* Else: queue full, drop character */
}

/**
 * @brief Get character from queue
 */
static int hid_queue_get(void)
{
    if (g_hid.queue_head == g_hid.queue_tail) {
        return -1;  /* Empty */
    }

    char c = g_hid.queue[g_hid.queue_head];
    g_hid.queue_head = (g_hid.queue_head + 1) & HID_QUEUE_MASK;
    return (unsigned char)c;
}

/**
 * @brief Convert scancode to ASCII
 */
static char hid_scancode_to_char(uint8_t scancode, uint8_t modifiers)
{
    if (scancode >= 128) {
        return 0;
    }

    int shifted = (modifiers & HID_MOD_SHIFT) != 0;

    /* Handle Caps Lock for letters */
    if (scancode >= 0x04 && scancode <= 0x1D) {
        if (g_hid.caps_lock) {
            shifted = !shifted;
        }
    }

    char c;
    if (shifted) {
        c = scancode_to_ascii_shifted[scancode];
    } else {
        c = scancode_to_ascii[scancode];
    }

    /* Handle Ctrl modifier */
    if ((modifiers & HID_MOD_CTRL) && c >= 'a' && c <= 'z') {
        c = c - 'a' + 1;  /* Ctrl+A = 0x01, etc. */
    } else if ((modifiers & HID_MOD_CTRL) && c >= 'A' && c <= 'Z') {
        c = c - 'A' + 1;
    }

    return c;
}

/**
 * @brief Check if scancode is in report
 */
static int hid_key_in_report(uint8_t scancode, const uint8_t *report)
{
    for (int i = 2; i < 8; i++) {
        if (report[i] == scancode) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Process HID report for new key presses
 */
static void hid_process_report(const uint8_t *report)
{
    uint8_t modifiers = report[0];

    /* Check for Caps Lock toggle (scancode 0x39) */
    if (hid_key_in_report(0x39, report) &&
        !hid_key_in_report(0x39, g_hid.prev_report)) {
        g_hid.caps_lock = !g_hid.caps_lock;
    }

    /* Check for Num Lock toggle (scancode 0x53) */
    if (hid_key_in_report(0x53, report) &&
        !hid_key_in_report(0x53, g_hid.prev_report)) {
        g_hid.num_lock = !g_hid.num_lock;
    }

    /* Process each key in current report */
    for (int i = 2; i < 8; i++) {
        uint8_t scancode = report[i];

        if (scancode == 0 || scancode == 1) {
            continue;  /* No key or error rollover */
        }

        /* Check if this is a new key press (not in previous report) */
        if (!hid_key_in_report(scancode, g_hid.prev_report)) {
            char c = hid_scancode_to_char(scancode, modifiers);
            if (c != 0) {
                hid_queue_put(c);
            }
        }
    }

    /* Save report for next comparison */
    memcpy(g_hid.prev_report, report, 8);
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

/**
 * @brief Initialize HID keyboard driver
 */
void usb_hid_init(uint8_t addr, uint8_t ep, uint8_t interval)
{
    g_hid.addr = addr;
    g_hid.endpoint = ep;
    g_hid.interval_ms = interval;
    g_hid.initialized = 1;

    g_hid.last_poll_us = 0;
    g_hid.caps_lock = 0;
    g_hid.num_lock = 0;

    memset(g_hid.prev_report, 0, sizeof(g_hid.prev_report));
    g_hid.queue_head = 0;
    g_hid.queue_tail = 0;

    uart_printf("HID: Keyboard initialized (addr=%d, ep=0x%02X, interval=%dms)\n",
                addr, ep, interval);
}

/**
 * @brief Poll HID keyboard for new keystrokes
 */
void usb_hid_poll(void)
{
    if (!g_hid.initialized) {
        return;
    }

    /* Check if it's time to poll */
    uint64_t now = timer_get_us();
    uint64_t interval_us = (uint64_t)g_hid.interval_ms * 1000;

    /* Use minimum interval of 10ms for reliable polling */
    if (interval_us < 10000) {
        interval_us = 10000;
    }

    if ((now - g_hid.last_poll_us) < interval_us) {
        return;
    }

    g_hid.last_poll_us = now;

    /* Read interrupt endpoint */
    uint8_t report[8];
    int result = usb_interrupt_transfer(g_hid.addr, g_hid.endpoint,
                                         report, sizeof(report));

    if (result > 0) {
        /* New data received */
        hid_process_report(report);
    }
    /* NAK (no data) or error - ignore */
}

/**
 * @brief Check if characters are available
 */
int usb_hid_available(void)
{
    return ((g_hid.queue_tail - g_hid.queue_head) & HID_QUEUE_MASK);
}

/**
 * @brief Get next character from keyboard buffer
 */
int usb_hid_getchar(void)
{
    return hid_queue_get();
}

/**
 * @brief Check if Caps Lock is active
 */
int usb_hid_caps_lock(void)
{
    return g_hid.caps_lock;
}

/**
 * @brief Check if Num Lock is active
 */
int usb_hid_num_lock(void)
{
    return g_hid.num_lock;
}
