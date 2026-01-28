/**
 * @file libc_stubs.c
 * @brief Minimal libc stubs for bare-metal RPi3
 *
 * These are required because we use -nostdlib for bare-metal linking.
 */

#include <stddef.h>
#include <stdint.h>

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;

    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return p - s;
}

char *strcpy(char *dest, const char *src)
{
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n)
{
    char *d = dest;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2)
{
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n)
{
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/* Simple snprintf implementation */
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    if (size == 0) return 0;

    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    char *p = buf;
    char *end = buf + size - 1;
    const char *f = fmt;

    while (*f && p < end) {
        if (*f != '%') {
            *p++ = *f++;
            continue;
        }
        f++;

        /* Handle format specifiers */
        int width = 0;
        char pad = ' ';

        /* Zero padding */
        if (*f == '0') {
            pad = '0';
            f++;
        }

        /* Width */
        while (*f >= '0' && *f <= '9') {
            width = width * 10 + (*f - '0');
            f++;
        }

        switch (*f) {
            case 'd':
            case 'i': {
                int val = __builtin_va_arg(args, int);
                char tmp[12];
                int neg = 0;
                if (val < 0) {
                    neg = 1;
                    val = -val;
                }
                int i = 0;
                do {
                    tmp[i++] = '0' + (val % 10);
                    val /= 10;
                } while (val);
                if (neg) tmp[i++] = '-';
                while (width > i && p < end) {
                    *p++ = pad;
                    width--;
                }
                while (i-- && p < end) {
                    *p++ = tmp[i];
                }
                break;
            }
            case 'u': {
                unsigned int val = __builtin_va_arg(args, unsigned int);
                char tmp[12];
                int i = 0;
                do {
                    tmp[i++] = '0' + (val % 10);
                    val /= 10;
                } while (val);
                while (width > i && p < end) {
                    *p++ = pad;
                    width--;
                }
                while (i-- && p < end) {
                    *p++ = tmp[i];
                }
                break;
            }
            case 'x':
            case 'X': {
                unsigned int val = __builtin_va_arg(args, unsigned int);
                char tmp[12];
                const char *hex = (*f == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                int i = 0;
                do {
                    tmp[i++] = hex[val & 0xF];
                    val >>= 4;
                } while (val);
                while (width > i && p < end) {
                    *p++ = pad;
                    width--;
                }
                while (i-- && p < end) {
                    *p++ = tmp[i];
                }
                break;
            }
            case 's': {
                const char *s = __builtin_va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s && p < end) {
                    *p++ = *s++;
                }
                break;
            }
            case 'c': {
                char c = (char)__builtin_va_arg(args, int);
                if (p < end) *p++ = c;
                break;
            }
            case '%':
                if (p < end) *p++ = '%';
                break;
            default:
                if (p < end) *p++ = *f;
                break;
        }
        f++;
    }

    *p = '\0';
    __builtin_va_end(args);
    return p - buf;
}

/* Stack protector stubs for bare-metal */
uintptr_t __stack_chk_guard = 0xDEADBEEF;

void __stack_chk_fail(void)
{
    while (1) {
        __asm__ volatile("wfe");
    }
}
