/**
 * @file framebuffer.h
 * @brief HDMI Framebuffer Driver for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#ifndef RPI3_FRAMEBUFFER_H
#define RPI3_FRAMEBUFFER_H

#include <stdint.h>
#include <stdarg.h>

/* Default colors (ARGB format) */
#define FB_COLOR_BLACK      0xFF000000
#define FB_COLOR_WHITE      0xFFFFFFFF
#define FB_COLOR_RED        0xFFFF0000
#define FB_COLOR_GREEN      0xFF00FF00
#define FB_COLOR_BLUE       0xFF0000FF
#define FB_COLOR_YELLOW     0xFFFFFF00
#define FB_COLOR_CYAN       0xFF00FFFF
#define FB_COLOR_MAGENTA    0xFFFF00FF
#define FB_COLOR_GRAY       0xFF808080
#define FB_COLOR_DARK_GRAY  0xFF404040

/* Framebuffer info structure */
typedef struct {
    uint32_t *address;      /* Framebuffer base address */
    uint32_t width;         /* Width in pixels */
    uint32_t height;        /* Height in pixels */
    uint32_t pitch;         /* Bytes per row */
    uint32_t depth;         /* Bits per pixel */
    /* Console state */
    uint32_t cursor_x;      /* Cursor X position (character column) */
    uint32_t cursor_y;      /* Cursor Y position (character row) */
    uint32_t cols;          /* Number of character columns */
    uint32_t rows;          /* Number of character rows */
    uint32_t fg_color;      /* Current foreground color */
    uint32_t bg_color;      /* Current background color */
    uint32_t char_scale;    /* Character scaling factor (1 or 2) */
} framebuffer_t;

/**
 * @brief Initialize the framebuffer
 *
 * @param width Desired width in pixels
 * @param height Desired height in pixels
 * @return 0 on success, -1 on failure
 */
int framebuffer_init(uint32_t width, uint32_t height);

/**
 * @brief Get framebuffer info
 * @return Pointer to framebuffer info structure
 */
framebuffer_t* framebuffer_get_info(void);

/**
 * @brief Check if framebuffer is initialized
 * @return 1 if initialized, 0 otherwise
 */
int framebuffer_is_ready(void);

/**
 * @brief Set a single pixel
 *
 * @param x X coordinate
 * @param y Y coordinate
 * @param color ARGB color value
 */
void fb_putpixel(uint32_t x, uint32_t y, uint32_t color);

/**
 * @brief Clear the screen with a color
 *
 * @param color ARGB color value
 */
void fb_clear(uint32_t color);

/**
 * @brief Draw a single character at pixel position
 *
 * @param ch Character to draw
 * @param x X pixel coordinate
 * @param y Y pixel coordinate
 * @param fg Foreground color
 * @param bg Background color
 * @param scale Scaling factor (1 or 2)
 */
void fb_putchar_at(char ch, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg, uint32_t scale);

/**
 * @brief Draw a character at console cursor position and advance cursor
 *
 * @param ch Character to draw
 */
void fb_putchar(char ch);

/**
 * @brief Print a null-terminated string
 *
 * @param str String to print
 */
void fb_puts(const char *str);

/**
 * @brief Printf-like formatted output to framebuffer
 *
 * @param fmt Format string
 */
void fb_printf(const char *fmt, ...);

/**
 * @brief Set console colors
 *
 * @param fg Foreground color
 * @param bg Background color
 */
void fb_set_colors(uint32_t fg, uint32_t bg);

/**
 * @brief Set cursor position
 *
 * @param col Column (0-based)
 * @param row Row (0-based)
 */
void fb_set_cursor(uint32_t col, uint32_t row);

/**
 * @brief Scroll the console up by one line
 */
void fb_scroll(void);

#endif /* RPI3_FRAMEBUFFER_H */
