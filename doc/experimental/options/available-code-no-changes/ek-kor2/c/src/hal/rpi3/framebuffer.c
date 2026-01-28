/**
 * @file framebuffer.c
 * @brief HDMI Framebuffer Driver for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Uses the VideoCore mailbox interface to allocate and configure
 * a framebuffer for HDMI output.
 */

#include "framebuffer.h"
#include "mailbox.h"
#include "font8x8.h"
#include "rpi3_hw.h"
#include <string.h>

/* Framebuffer state */
static framebuffer_t g_fb = {0};
static int g_fb_ready = 0;

/* Mailbox buffer (must be 16-byte aligned) */
static volatile uint32_t __attribute__((aligned(16))) mbox_buffer[36];

/**
 * @brief Initialize the framebuffer
 */
int framebuffer_init(uint32_t width, uint32_t height)
{
    uint32_t idx = 0;

    /* Build mailbox message */
    mbox_buffer[idx++] = 0;                         /* [0] Size (filled later) */
    mbox_buffer[idx++] = MBOX_REQUEST;              /* [1] Request code */

    /* Set physical display size */
    mbox_buffer[idx++] = MBOX_TAG_SET_PHYS_WH;      /* Tag */
    mbox_buffer[idx++] = 8;                         /* Value buffer size */
    mbox_buffer[idx++] = 0;                         /* Request indicator */
    mbox_buffer[idx++] = width;                     /* Width */
    mbox_buffer[idx++] = height;                    /* Height */

    /* Set virtual (buffer) size */
    mbox_buffer[idx++] = MBOX_TAG_SET_VIRT_WH;
    mbox_buffer[idx++] = 8;
    mbox_buffer[idx++] = 0;
    mbox_buffer[idx++] = width;
    mbox_buffer[idx++] = height;

    /* Set depth (32 bits per pixel) */
    mbox_buffer[idx++] = MBOX_TAG_SET_DEPTH;
    mbox_buffer[idx++] = 4;
    mbox_buffer[idx++] = 0;
    mbox_buffer[idx++] = 32;

    /* Set pixel order (RGB) */
    mbox_buffer[idx++] = MBOX_TAG_SET_PIXEL_ORDER;
    mbox_buffer[idx++] = 4;
    mbox_buffer[idx++] = 0;
    mbox_buffer[idx++] = MBOX_PIXEL_ORDER_RGB;

    /* Allocate framebuffer */
    uint32_t fb_alloc_idx = idx + 3;  /* Index where address will be returned (after tag+size+req) */
    mbox_buffer[idx++] = MBOX_TAG_ALLOC_FB;
    mbox_buffer[idx++] = 8;
    mbox_buffer[idx++] = 0;
    mbox_buffer[idx++] = 16;                        /* Alignment -> becomes address */
    mbox_buffer[idx++] = 0;                         /* Size (returned) */

    /* Get pitch (bytes per row) */
    uint32_t pitch_idx = idx + 3;  /* Index where pitch will be returned (after tag+size+req) */
    mbox_buffer[idx++] = MBOX_TAG_GET_PITCH;
    mbox_buffer[idx++] = 4;
    mbox_buffer[idx++] = 0;
    mbox_buffer[idx++] = 0;                         /* Pitch (returned) */

    /* End tag */
    mbox_buffer[idx++] = MBOX_TAG_END;

    /* Set total size */
    mbox_buffer[0] = idx * 4;

    /* Call mailbox */
    if (mbox_call(mbox_buffer) < 0) {
        return -1;
    }

    /* Extract framebuffer address */
    uint32_t fb_addr = mbox_buffer[fb_alloc_idx];
    if (fb_addr == 0) {
        return -1;
    }

    /* Convert GPU address to ARM address */
    /* GPU returns address with 0x40000000 or 0xC0000000 prefix for cached/uncached */
    fb_addr &= 0x3FFFFFFF;

    /* Extract pitch */
    uint32_t pitch = mbox_buffer[pitch_idx];

    /* Store framebuffer info */
    g_fb.address = (uint32_t *)((uintptr_t)fb_addr);
    g_fb.width = width;
    g_fb.height = height;
    g_fb.pitch = pitch;
    g_fb.depth = 32;

    /* Debug: print FB address */
    extern void uart_printf(const char *fmt, ...);
    uart_printf("FB addr: 0x%x, pitch: %d\n", fb_addr, pitch);

    /* Console setup with 2x scaling for readability */
    g_fb.char_scale = 2;
    g_fb.cols = width / (8 * g_fb.char_scale);
    g_fb.rows = height / (8 * g_fb.char_scale);
    g_fb.cursor_x = 0;
    g_fb.cursor_y = 0;
    g_fb.fg_color = FB_COLOR_WHITE;
    g_fb.bg_color = FB_COLOR_BLACK;

    /* Clear screen */
    fb_clear(g_fb.bg_color);

    g_fb_ready = 1;
    return 0;
}

/**
 * @brief Get framebuffer info
 */
framebuffer_t* framebuffer_get_info(void)
{
    return &g_fb;
}

/**
 * @brief Check if framebuffer is initialized
 */
int framebuffer_is_ready(void)
{
    return g_fb_ready;
}

/**
 * @brief Set a single pixel
 */
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!g_fb_ready || x >= g_fb.width || y >= g_fb.height) {
        return;
    }

    /* Calculate offset (pitch is in bytes, we're using 32-bit words) */
    uint32_t offset = (y * (g_fb.pitch / 4)) + x;
    g_fb.address[offset] = color;
}

/**
 * @brief Clear the screen with a color
 */
void fb_clear(uint32_t color)
{
    if (!g_fb_ready) {
        return;
    }

    uint32_t pixels = (g_fb.pitch / 4) * g_fb.height;
    for (uint32_t i = 0; i < pixels; i++) {
        g_fb.address[i] = color;
    }

    g_fb.cursor_x = 0;
    g_fb.cursor_y = 0;
}

/**
 * @brief Draw a single character at pixel position
 */
void fb_putchar_at(char ch, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg, uint32_t scale)
{
    if (!g_fb_ready) {
        return;
    }

    /* Get font glyph */
    const uint8_t *glyph;
    if (ch < FONT8X8_FIRST_CHAR || ch > FONT8X8_LAST_CHAR) {
        /* Use space for out-of-range characters */
        glyph = font8x8[0];
    } else {
        glyph = font8x8[ch - FONT8X8_FIRST_CHAR];
    }

    /* Draw character with scaling */
    for (uint32_t row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;

            /* Draw scaled pixel block */
            for (uint32_t sy = 0; sy < scale; sy++) {
                for (uint32_t sx = 0; sx < scale; sx++) {
                    uint32_t px = x + col * scale + sx;
                    uint32_t py = y + row * scale + sy;
                    if (px < g_fb.width && py < g_fb.height) {
                        fb_putpixel(px, py, color);
                    }
                }
            }
        }
    }
}

/**
 * @brief Scroll the console up by one line
 */
void fb_scroll(void)
{
    if (!g_fb_ready) {
        return;
    }

    uint32_t char_height = 8 * g_fb.char_scale;
    uint32_t line_words = (g_fb.pitch / 4) * char_height;
    uint32_t total_words = (g_fb.pitch / 4) * g_fb.height;

    /* Copy lines up */
    for (uint32_t i = 0; i < total_words - line_words; i++) {
        g_fb.address[i] = g_fb.address[i + line_words];
    }

    /* Clear bottom line */
    for (uint32_t i = total_words - line_words; i < total_words; i++) {
        g_fb.address[i] = g_fb.bg_color;
    }
}

/**
 * @brief Draw a character at console cursor position and advance cursor
 */
void fb_putchar(char ch)
{
    if (!g_fb_ready) {
        return;
    }

    uint32_t char_width = 8 * g_fb.char_scale;
    uint32_t char_height = 8 * g_fb.char_scale;

    /* Handle special characters */
    if (ch == '\n') {
        g_fb.cursor_x = 0;
        g_fb.cursor_y++;
        if (g_fb.cursor_y >= g_fb.rows) {
            fb_scroll();
            g_fb.cursor_y = g_fb.rows - 1;
        }
        return;
    } else if (ch == '\r') {
        g_fb.cursor_x = 0;
        return;
    } else if (ch == '\t') {
        /* Tab to next 8-column boundary */
        g_fb.cursor_x = (g_fb.cursor_x + 8) & ~7;
        if (g_fb.cursor_x >= g_fb.cols) {
            g_fb.cursor_x = 0;
            g_fb.cursor_y++;
            if (g_fb.cursor_y >= g_fb.rows) {
                fb_scroll();
                g_fb.cursor_y = g_fb.rows - 1;
            }
        }
        return;
    } else if (ch == '\b') {
        /* Backspace */
        if (g_fb.cursor_x > 0) {
            g_fb.cursor_x--;
            fb_putchar_at(' ', g_fb.cursor_x * char_width, g_fb.cursor_y * char_height,
                          g_fb.fg_color, g_fb.bg_color, g_fb.char_scale);
        }
        return;
    }

    /* Draw the character */
    fb_putchar_at(ch, g_fb.cursor_x * char_width, g_fb.cursor_y * char_height,
                  g_fb.fg_color, g_fb.bg_color, g_fb.char_scale);

    /* Advance cursor */
    g_fb.cursor_x++;
    if (g_fb.cursor_x >= g_fb.cols) {
        g_fb.cursor_x = 0;
        g_fb.cursor_y++;
        if (g_fb.cursor_y >= g_fb.rows) {
            fb_scroll();
            g_fb.cursor_y = g_fb.rows - 1;
        }
    }
}

/**
 * @brief Print a null-terminated string
 */
void fb_puts(const char *str)
{
    while (*str) {
        fb_putchar(*str++);
    }
}

/**
 * @brief Set console colors
 */
void fb_set_colors(uint32_t fg, uint32_t bg)
{
    g_fb.fg_color = fg;
    g_fb.bg_color = bg;
}

/**
 * @brief Set cursor position
 */
void fb_set_cursor(uint32_t col, uint32_t row)
{
    if (col < g_fb.cols) {
        g_fb.cursor_x = col;
    }
    if (row < g_fb.rows) {
        g_fb.cursor_y = row;
    }
}

/* Helper functions for fb_printf */
static void fb_print_uint(uint64_t val, int base, int width, char pad, int uppercase)
{
    char buf[24];
    int i = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val > 0) {
            buf[i++] = digits[val % base];
            val /= base;
        }
    }

    /* Padding */
    while (i < width) {
        fb_putchar(pad);
        width--;
    }

    /* Print in reverse */
    while (i > 0) {
        fb_putchar(buf[--i]);
    }
}

static void fb_print_int(int64_t val, int width, char pad)
{
    if (val < 0) {
        fb_putchar('-');
        if (width > 0) width--;
        val = -val;
    }
    fb_print_uint((uint64_t)val, 10, width, pad, 0);
}

/**
 * @brief Printf-like formatted output to framebuffer
 */
void fb_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            fb_putchar(*fmt++);
            continue;
        }

        fmt++;  /* Skip '%' */

        /* Parse width and padding */
        char pad = ' ';
        int width = 0;
        int long_mod = 0;

        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }

        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Length modifier */
        while (*fmt == 'l') {
            long_mod++;
            fmt++;
        }

        /* Format specifier */
        switch (*fmt) {
            case 'd':
            case 'i':
                if (long_mod >= 2) {
                    fb_print_int(va_arg(args, int64_t), width, pad);
                } else if (long_mod == 1) {
                    fb_print_int(va_arg(args, long), width, pad);
                } else {
                    fb_print_int(va_arg(args, int), width, pad);
                }
                break;

            case 'u':
                if (long_mod >= 2) {
                    fb_print_uint(va_arg(args, uint64_t), 10, width, pad, 0);
                } else if (long_mod == 1) {
                    fb_print_uint(va_arg(args, unsigned long), 10, width, pad, 0);
                } else {
                    fb_print_uint(va_arg(args, unsigned int), 10, width, pad, 0);
                }
                break;

            case 'x':
                if (long_mod >= 2) {
                    fb_print_uint(va_arg(args, uint64_t), 16, width, pad, 0);
                } else if (long_mod == 1) {
                    fb_print_uint(va_arg(args, unsigned long), 16, width, pad, 0);
                } else {
                    fb_print_uint(va_arg(args, unsigned int), 16, width, pad, 0);
                }
                break;

            case 'X':
                if (long_mod >= 2) {
                    fb_print_uint(va_arg(args, uint64_t), 16, width, pad, 1);
                } else if (long_mod == 1) {
                    fb_print_uint(va_arg(args, unsigned long), 16, width, pad, 1);
                } else {
                    fb_print_uint(va_arg(args, unsigned int), 16, width, pad, 1);
                }
                break;

            case 'p':
                fb_puts("0x");
                fb_print_uint((uintptr_t)va_arg(args, void *), 16, sizeof(void *) * 2, '0', 0);
                break;

            case 's': {
                const char *s = va_arg(args, const char *);
                if (s == NULL) s = "(null)";
                fb_puts(s);
                break;
            }

            case 'c':
                fb_putchar((char)va_arg(args, int));
                break;

            case '%':
                fb_putchar('%');
                break;

            default:
                fb_putchar('%');
                fb_putchar(*fmt);
                break;
        }
        fmt++;
    }

    va_end(args);
}
