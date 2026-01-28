/**
 * @file ekk_hal_efr32mg24.c
 * @brief EK-KOR v2 - HAL Implementation for Silicon Labs EFR32MG24 (Cortex-M33)
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Target hardware:
 * - EFR32MG24 (78 MHz Cortex-M33 with TrustZone)
 * - 1.5 MB Flash, 256 KB SRAM
 * - EUSART0 for debug serial
 * - TIMER0 (32-bit) for microsecond timestamps
 *
 * Memory layout:
 * - Flash (0x08000000): Code, const data
 * - SRAM (0x20000000): Stack, heap, .data, .bss
 * - Field region (0x2003C000): Last 16KB for EK-KOR shared data
 *
 * ASIL-D Note:
 * Silicon Labs chips have no ASIL certification. For safety-critical use,
 * implement dual-chip architecture with this as main processor and a
 * separate safety monitor (either another EFR32MG24 or TI C2000 F29).
 *
 * Future Enhancement:
 * The 802.15.4 radio can be used for inter-module mesh communication,
 * replacing CAN with wireless for EK-KOR topology discovery.
 */

#include "ekk/ekk_hal.h"
#include "ekk/ekk_spsc.h"
#include "ekk/ekk_field.h"

/* Only compile for EFR32MG24 target */
#if defined(EFR32MG24) || defined(EKK_PLATFORM_EFR32MG24)

#include "efr32mg24_config.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

/* Field region pointer */
static ekk_field_region_t *g_field_region = (ekk_field_region_t *)FIELD_REGION_ADDR;

/* Message queue */
typedef struct {
    ekk_module_id_t sender;
    uint8_t msg_type;
    uint8_t data[MSG_MAX_LEN];
    uint32_t len;
    uint8_t valid;
} hal_message_t;

static hal_message_t g_msg_queue[MSG_QUEUE_SIZE];
static volatile uint32_t g_msg_head = 0;
static volatile uint32_t g_msg_tail = 0;

/* Receive callback */
static ekk_hal_recv_cb g_recv_callback = NULL;

/* Module ID (computed from device EUI-64) */
static ekk_module_id_t g_module_id = 0;

/* ============================================================================
 * CORTEX-M33 INTRINSICS
 * ============================================================================ */

static inline void __DMB(void) {
    __asm__ volatile("dmb 0xF" ::: "memory");
}

static inline void __DSB(void) {
    __asm__ volatile("dsb 0xF" ::: "memory");
}

static inline void __ISB(void) {
    __asm__ volatile("isb 0xF" ::: "memory");
}

static inline uint32_t __LDREXW(volatile uint32_t *addr) {
    uint32_t result;
    __asm__ volatile("ldrex %0, [%1]" : "=r"(result) : "r"(addr));
    return result;
}

static inline uint32_t __STREXW(uint32_t value, volatile uint32_t *addr) {
    uint32_t result;
    __asm__ volatile("strex %0, %2, [%1]" : "=&r"(result) : "r"(addr), "r"(value));
    return result;
}

/* ============================================================================
 * TIMER FUNCTIONS (TIMER0 for microsecond timestamps)
 * ============================================================================ */

static void timer_init(void) {
    /* Enable TIMER0 clock */
    CMU_CLKEN0 |= CMU_CLKEN0_TIMER0;
    __DSB();

    /* Disable timer before configuration */
    TIMER0_EN = 0;
    __DSB();

    /* Configure TIMER0 as microsecond counter
     * System clock = 78 MHz
     * Prescaler = 78 -> Timer clock = 1 MHz (1 us resolution)
     * Using prescaler DIV128 gives us ~609 kHz, close enough
     * For exact 1 MHz, we'd need DIV78 which isn't available
     * Alternative: use DIV64 and software correction
     *
     * For simplicity in Renode: use DIV64 (1.22 MHz) or DIV128 (609 kHz)
     * In real hardware: configure clock tree for exact 1 MHz timer
     */

    /* Use up-counting mode, prescaler /64 for ~1.22 MHz
     * Each tick = ~0.82 us, multiply by 1.22 for correction
     * Or use SYSCLK/78 = 1 MHz exactly via CMU
     */
    TIMER0_CTRL = TIMER_CTRL_PRESC_DIV64 | TIMER_CTRL_MODE_UP;

    /* Set TOP to max value (32-bit free-running counter) */
    TIMER0_TOP = 0xFFFFFFFF;

    /* Enable and start timer */
    TIMER0_EN = TIMER_EN_EN;
    __DSB();

    TIMER0_CMD = TIMER_CMD_START;
}

ekk_time_us_t ekk_hal_time_us(void) {
    /* Read raw counter value
     * With DIV64 prescaler: counter runs at SYSCLK/64 = 78MHz/64 = 1.21875 MHz
     * Each count = 0.82 us
     * Return raw count for now (Renode will simulate correctly)
     */
    uint32_t cnt = TIMER0_CNT;

    /* Scale to microseconds: cnt * 64 / 78 ≈ cnt * 0.82
     * For better precision: (cnt * 64 + 39) / 78
     * For Renode testing: just return count (Renode timer simulates at 1MHz)
     */
#if defined(EKK_RENODE_SIM)
    return (ekk_time_us_t)cnt;
#else
    /* Hardware correction factor */
    return (ekk_time_us_t)((cnt * 64ULL) / 78ULL);
#endif
}

void ekk_hal_delay_us(uint32_t us) {
    uint32_t start = TIMER0_CNT;

    /* Calculate target count with prescaler correction */
#if defined(EKK_RENODE_SIM)
    uint32_t target = us;  /* Renode simulates at 1 MHz */
#else
    /* Convert us to timer ticks: us * 78 / 64 */
    uint32_t target = (us * 78ULL) / 64ULL;
#endif

    while ((TIMER0_CNT - start) < target) {
        /* Busy wait */
    }
}

/* Mock time support for testing */
static ekk_time_us_t g_mock_time = 0;
static bool g_mock_time_enabled = false;

void ekk_hal_set_mock_time(ekk_time_us_t time_us) {
    if (time_us == 0) {
        g_mock_time_enabled = false;
    } else {
        g_mock_time = time_us;
        g_mock_time_enabled = true;
    }
}

/* ============================================================================
 * SERIAL (DEBUG OUTPUT via EUSART0)
 * ============================================================================ */

static void serial_init(void) {
    /* Enable GPIO and EUSART0 clocks */
    CMU_CLKEN0 |= CMU_CLKEN0_GPIO | CMU_CLKEN0_EUSART0;
    __DSB();

    /* Configure GPIO pins for EUSART0
     * Default pins: PA5 (TX), PA6 (RX)
     * Mode: Push-pull for TX, Input for RX
     */

    /* Set PA5 as push-pull output (TX) */
    uint32_t model = GPIO_PORTA_MODEL;
    model &= ~(0xF << 20);  /* Clear PA5 mode bits */
    model |= (GPIO_MODE_PUSHPULL << 20);
    GPIO_PORTA_MODEL = model;

    /* Set PA6 as input (RX) */
    model = GPIO_PORTA_MODEL;
    model &= ~(0xF << 24);  /* Clear PA6 mode bits */
    model |= (GPIO_MODE_INPUT << 24);
    GPIO_PORTA_MODEL = model;

    /* Route EUSART0 TX to PA5, RX to PA6 */
    GPIO_EUSART0_TXROUTE = (0 << 0) | (5 << 16);  /* Port A, Pin 5 */
    GPIO_EUSART0_RXROUTE = (0 << 0) | (6 << 16);  /* Port A, Pin 6 */

    /* Disable EUSART before configuration */
    EUSART0_EN = 0;
    __DSB();

    /* Configure for 115200 baud, 8N1
     * Baud rate = SYSCLK / (16 * (CLKDIV + 1))
     * CLKDIV = SYSCLK / (16 * baud) - 1
     * CLKDIV = 78000000 / (16 * 115200) - 1 = 41.3 -> 41
     *
     * Actual baud = 78000000 / (16 * 42) = 116071 (~0.75% error)
     */

    /* Frame configuration: 8 data bits, 1 stop bit, no parity */
    EUSART0_FRAMECFG = EUSART_FRAMECFG_DATABITS_8 |
                       EUSART_FRAMECFG_STOPBITS_1 |
                       EUSART_FRAMECFG_PARITY_NONE;

    /* Async mode configuration */
    EUSART0_CFG0 = EUSART_CFG0_SYNC;  /* Async mode */

    /* Clock divider for 115200 baud */
    EUSART0_CLKDIV = (41 << 3);  /* CLKDIV value in bits [18:3] */

    /* Enable EUSART */
    EUSART0_EN = EUSART_EN_EN;
    __DSB();

    /* Enable TX and RX */
    EUSART0_CMD = EUSART_CMD_TXEN | EUSART_CMD_RXEN;
    __DSB();
}

static void serial_putchar(char c) {
    /* Wait for TX buffer not full */
    while (EUSART0_STATUS & EUSART_STATUS_TXFL) {
        /* Busy wait - TX FIFO has data */
    }

    /* Wait until TX is idle to accept new data */
    while (!(EUSART0_STATUS & EUSART_STATUS_TXIDLE)) {
        /* Busy wait */
    }

    EUSART0_TXDATA = (uint32_t)c;
}

void ekk_hal_printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(buf[i]);
    }
}

/* ============================================================================
 * MESSAGE TRANSMISSION (via EUSART serial link)
 *
 * For Renode testing, we use EUSART0 as a simple serial link.
 * In future: Replace with 802.15.4 radio for true mesh networking.
 *
 * Serial protocol:
 * [START:1] [LEN:1] [MSG_TYPE:1] [SENDER:1] [DEST:1] [DATA:N] [CRC:1]
 * START = 0x7E (frame delimiter)
 * ============================================================================ */

#define SERIAL_FRAME_START      0x7E
#define SERIAL_FRAME_ESC        0x7D
#define SERIAL_FRAME_XOR        0x20

/**
 * @brief Send message via serial link
 */
ekk_error_t ekk_hal_send(ekk_module_id_t dest_id,
                          ekk_msg_type_t msg_type,
                          const void *data,
                          uint32_t len) {
    if (len > MSG_MAX_LEN) {
        return EKK_ERR_INVALID_ARG;
    }

    const uint8_t *payload = (const uint8_t *)data;

    /* Simple checksum */
    uint8_t crc = msg_type ^ g_module_id ^ dest_id ^ (len & 0xFF);
    for (uint32_t i = 0; i < len; i++) {
        crc ^= payload[i];
    }

    /* Send frame */
    serial_putchar(SERIAL_FRAME_START);
    serial_putchar((char)(len + 3));  /* Payload = msg_type + sender + dest + data */
    serial_putchar((char)msg_type);
    serial_putchar((char)g_module_id);
    serial_putchar((char)dest_id);

    for (uint32_t i = 0; i < len; i++) {
        /* Escape special characters */
        if (payload[i] == SERIAL_FRAME_START || payload[i] == SERIAL_FRAME_ESC) {
            serial_putchar(SERIAL_FRAME_ESC);
            serial_putchar((char)(payload[i] ^ SERIAL_FRAME_XOR));
        } else {
            serial_putchar((char)payload[i]);
        }
    }

    serial_putchar((char)crc);

#if EKK_DEBUG
    ekk_hal_printf("[TX] type=%u sender=%u dest=%u len=%lu\n",
                   msg_type, g_module_id, dest_id, (unsigned long)len);
#endif

    return EKK_OK;
}

ekk_error_t ekk_hal_broadcast(ekk_msg_type_t msg_type,
                               const void *data,
                               uint32_t len) {
    /* Broadcast uses special ID 0xFF */
    return ekk_hal_send(EKK_BROADCAST_ID, msg_type, data, len);
}

/**
 * @brief Check for received message
 *
 * Poll EUSART RX for incoming frames.
 * In interrupt-driven mode, messages would be queued by ISR.
 */
ekk_error_t ekk_hal_recv(ekk_module_id_t *sender_id,
                          ekk_msg_type_t *msg_type,
                          void *data,
                          uint32_t *len) {
    /* Check message queue first (populated by ISR or polling) */
    if (g_msg_head != g_msg_tail) {
        hal_message_t *msg = &g_msg_queue[g_msg_tail];
        if (msg->valid) {
            *sender_id = msg->sender;
            *msg_type = (ekk_msg_type_t)msg->msg_type;

            uint32_t copy_len = (*len < msg->len) ? *len : msg->len;
            if (data && copy_len > 0) {
                memcpy(data, msg->data, copy_len);
            }
            *len = msg->len;

            msg->valid = 0;
            g_msg_tail = (g_msg_tail + 1) % MSG_QUEUE_SIZE;

#if EKK_DEBUG
            ekk_hal_printf("[RX] type=%u sender=%u len=%lu\n",
                           *msg_type, *sender_id, (unsigned long)*len);
#endif

            return EKK_OK;
        }
    }

    /* Poll EUSART RX for new data (non-blocking) */
    if (!(EUSART0_STATUS & EUSART_STATUS_RXFL)) {
        /* No data in RX FIFO */
        return EKK_ERR_NOT_FOUND;
    }

    /* Simple frame parser - for demo purposes */
    /* In production, use interrupt-driven reception with state machine */
    static uint8_t rx_state = 0;
    static uint8_t rx_buf[MSG_MAX_LEN + 8];
    static uint8_t rx_len = 0;
    static uint8_t rx_expected = 0;

    uint8_t byte = (uint8_t)(EUSART0_RXDATA & 0xFF);

    switch (rx_state) {
        case 0:  /* Waiting for START */
            if (byte == SERIAL_FRAME_START) {
                rx_state = 1;
                rx_len = 0;
            }
            break;

        case 1:  /* Reading length */
            rx_expected = byte;
            if (rx_expected > MSG_MAX_LEN + 4) {
                rx_state = 0;  /* Invalid length, reset */
            } else {
                rx_state = 2;
            }
            break;

        case 2:  /* Reading data */
            rx_buf[rx_len++] = byte;
            if (rx_len >= rx_expected + 1) {  /* +1 for CRC */
                /* Frame complete - parse it */
                if (rx_len >= 4) {
                    uint8_t rx_msg_type = rx_buf[0];
                    uint8_t rx_sender = rx_buf[1];
                    uint8_t rx_dest = rx_buf[2];
                    uint8_t rx_crc = rx_buf[rx_len - 1];

                    /* Calculate expected CRC */
                    uint8_t calc_crc = rx_msg_type ^ rx_sender ^ rx_dest ^ ((rx_len - 4) & 0xFF);
                    for (uint8_t i = 3; i < rx_len - 1; i++) {
                        calc_crc ^= rx_buf[i];
                    }

                    /* Check destination and CRC */
                    if ((rx_dest == g_module_id || rx_dest == EKK_BROADCAST_ID) &&
                        rx_crc == calc_crc) {
                        /* Valid frame for us - queue it */
                        uint32_t next_head = (g_msg_head + 1) % MSG_QUEUE_SIZE;
                        if (next_head != g_msg_tail) {
                            hal_message_t *msg = &g_msg_queue[g_msg_head];
                            msg->sender = rx_sender;
                            msg->msg_type = rx_msg_type;
                            msg->len = rx_len - 4;
                            if (msg->len > 0) {
                                memcpy(msg->data, &rx_buf[3], msg->len);
                            }
                            msg->valid = 1;
                            g_msg_head = next_head;
                        }
                    }
                }
                rx_state = 0;
            }
            break;
    }

    return EKK_ERR_NOT_FOUND;  /* Need more data */
}

void ekk_hal_set_recv_callback(ekk_hal_recv_cb callback) {
    g_recv_callback = callback;
}

/* ============================================================================
 * CRITICAL SECTIONS
 * ============================================================================ */

uint32_t ekk_hal_critical_enter(void) {
    uint32_t primask;
    __asm__ volatile("mrs %0, primask" : "=r"(primask));
    __asm__ volatile("cpsid i" ::: "memory");
    return primask;
}

void ekk_hal_critical_exit(uint32_t state) {
    __asm__ volatile("msr primask, %0" :: "r"(state) : "memory");
}

void ekk_hal_memory_barrier(void) {
    __DMB();
}

/* ============================================================================
 * ATOMIC OPERATIONS
 * ============================================================================ */

bool ekk_hal_cas32(volatile uint32_t *ptr, uint32_t expected, uint32_t desired) {
    uint32_t result;
    uint32_t status;

    do {
        result = __LDREXW(ptr);
        if (result != expected) {
            /* Clear exclusive monitor */
            __asm__ volatile("clrex");
            return false;
        }
        status = __STREXW(desired, ptr);
    } while (status != 0);  /* Retry if store failed */

    return true;
}

uint32_t ekk_hal_atomic_inc(volatile uint32_t *ptr) {
    uint32_t result;
    uint32_t status;

    do {
        result = __LDREXW(ptr);
        status = __STREXW(result + 1, ptr);
    } while (status != 0);

    return result + 1;
}

uint32_t ekk_hal_atomic_dec(volatile uint32_t *ptr) {
    uint32_t result;
    uint32_t status;

    do {
        result = __LDREXW(ptr);
        status = __STREXW(result - 1, ptr);
    } while (status != 0);

    return result - 1;
}

/* ============================================================================
 * SHARED MEMORY
 * ============================================================================ */

void *ekk_hal_get_field_region(void) {
    return g_field_region;
}

void ekk_hal_sync_field_region(void) {
    __DMB();
    __DSB();
}

/* ============================================================================
 * PLATFORM INITIALIZATION
 * ============================================================================ */

/**
 * @brief Get module ID
 *
 * Priority:
 * 1. Memory override (0x2003FFFC) - set by Renode for multi-module testing
 * 2. Device EUI-64 - computed from factory-programmed unique ID
 *
 * Memory override format: 0xECC0XXXX where XX is module ID (1-254)
 */
static ekk_module_id_t compute_module_id(void) {
    /* Check for Renode memory override */
    uint32_t override = MODULE_ID_OVERRIDE;
    if ((override & 0xFFFF0000) == MODULE_ID_MAGIC) {
        uint8_t id = override & 0xFF;
        if (id > 0 && id < 255) {
            ekk_hal_printf("[HAL] Module ID from memory override: %u\n", id);
            return (ekk_module_id_t)id;
        }
    }

    /* Fall back to EUI-64 based ID */
    uint32_t eui64_l = DEVINFO_EUI64L;
    uint32_t eui64_h = DEVINFO_EUI64H;

    /* XOR all bytes together */
    uint32_t hash = eui64_l ^ eui64_h;
    hash ^= (hash >> 16);
    hash ^= (hash >> 8);

    /* Map to valid module ID range [1, 254] */
    uint8_t id = (hash & 0xFF);
    if (id == 0) id = 1;
    if (id == 255) id = 254;

    return (ekk_module_id_t)id;
}

ekk_error_t ekk_hal_init(void) {
    /* Enable FPU (Cortex-M33 with FPU) */
    /* Set CP10 and CP11 to full access */
    FPU_CPACR |= (0xF << 20);
    __DSB();
    __ISB();

    /* Initialize timer first (needed for timestamps) */
    timer_init();

    /* Initialize serial for debug output */
    serial_init();

    /* Compute unique module ID (checks memory override first) */
    g_module_id = compute_module_id();

    /* Initialize field region */
    memset(g_field_region, 0, sizeof(ekk_field_region_t));

    /* Initialize message queue */
    memset(g_msg_queue, 0, sizeof(g_msg_queue));
    g_msg_head = 0;
    g_msg_tail = 0;

    ekk_hal_printf("\n");
    ekk_hal_printf("================================================\n");
    ekk_hal_printf("EK-KOR HAL: EFR32MG24 initialized\n");
    ekk_hal_printf("================================================\n");
    ekk_hal_printf("  Module ID: %u\n", g_module_id);
    ekk_hal_printf("  EUI-64: %08lX-%08lX\n",
                   (unsigned long)DEVINFO_EUI64H,
                   (unsigned long)DEVINFO_EUI64L);
    ekk_hal_printf("  SYSCLK: %lu MHz\n", (unsigned long)(EFR32MG24_SYSCLK_FREQ / 1000000));
    ekk_hal_printf("  Flash: %lu KB\n", (unsigned long)(FLASH_SIZE / 1024));
    ekk_hal_printf("  RAM: %lu KB\n", (unsigned long)(SRAM_SIZE / 1024));
    ekk_hal_printf("\n");
    ekk_hal_printf("ASIL-D Note: This chip requires dual-chip\n");
    ekk_hal_printf("architecture for safety-critical applications.\n");
    ekk_hal_printf("================================================\n");

    return EKK_OK;
}

const char *ekk_hal_platform_name(void) {
    return "EFR32MG24 (Cortex-M33 @ 78MHz, Secure Vault)";
}

ekk_module_id_t ekk_hal_get_module_id(void) {
    return g_module_id;
}

/* ============================================================================
 * ASSERT HANDLER
 * ============================================================================ */

void ekk_hal_assert_fail(const char *file, int line, const char *expr) {
    ekk_hal_printf("\n!!! ASSERT FAILED !!!\n");
    ekk_hal_printf("File: %s\n", file);
    ekk_hal_printf("Line: %d\n", line);
    ekk_hal_printf("Expr: %s\n", expr);

    /* Disable interrupts and halt */
    __asm__ volatile("cpsid i");
    while (1) {
        __asm__ volatile("wfi");
    }
}

#endif /* EFR32MG24 || EKK_PLATFORM_EFR32MG24 */
