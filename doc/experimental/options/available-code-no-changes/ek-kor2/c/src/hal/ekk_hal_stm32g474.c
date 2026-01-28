/**
 * @file ekk_hal_stm32g474.c
 * @brief EK-KOR v2 - HAL Implementation for STM32G474 (Cortex-M4)
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Target hardware:
 * - STM32G474RE (170 MHz Cortex-M4 with FPU and DSP)
 * - 512KB Flash, 128KB SRAM (96KB SRAM1 + 32KB SRAM2 + 32KB CCM)
 * - FDCAN1 for CAN-FD segment bus
 * - TIM2 (32-bit) for microsecond timestamps
 *
 * Memory layout:
 * - CCM (0x10000000): Stack, critical variables
 * - SRAM1 (0x20000000): Heap, general data
 * - SRAM2 (0x20014000): Field region (shared, for multi-core future)
 */

#include "ekk/ekk_hal.h"
#include "ekk/ekk_spsc.h"
#include "ekk/ekk_field.h"

/* Only compile for STM32G4 target */
#if defined(STM32G474xx) || defined(EKK_PLATFORM_STM32G474)

#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* ============================================================================
 * STM32G4 REGISTER DEFINITIONS
 * ============================================================================ */

/* Base addresses */
#define PERIPH_BASE             0x40000000UL
#define APB1PERIPH_BASE         PERIPH_BASE
#define APB2PERIPH_BASE         (PERIPH_BASE + 0x00010000UL)
#define AHB1PERIPH_BASE         (PERIPH_BASE + 0x00020000UL)
#define AHB2PERIPH_BASE         (PERIPH_BASE + 0x08000000UL)

/* RCC (Reset and Clock Control) */
#define RCC_BASE                (AHB1PERIPH_BASE + 0x1000UL)
#define RCC_AHB1ENR             (*(volatile uint32_t *)(RCC_BASE + 0x48))
#define RCC_AHB2ENR             (*(volatile uint32_t *)(RCC_BASE + 0x4C))
#define RCC_APB1ENR1            (*(volatile uint32_t *)(RCC_BASE + 0x58))
#define RCC_APB1ENR2            (*(volatile uint32_t *)(RCC_BASE + 0x5C))
#define RCC_APB2ENR             (*(volatile uint32_t *)(RCC_BASE + 0x60))
#define RCC_CCIPR               (*(volatile uint32_t *)(RCC_BASE + 0x88))

/* TIM2 (32-bit general-purpose timer) */
#define TIM2_BASE               (APB1PERIPH_BASE + 0x0000UL)
#define TIM2_CR1                (*(volatile uint32_t *)(TIM2_BASE + 0x00))
#define TIM2_SR                 (*(volatile uint32_t *)(TIM2_BASE + 0x10))
#define TIM2_CNT                (*(volatile uint32_t *)(TIM2_BASE + 0x24))
#define TIM2_PSC                (*(volatile uint32_t *)(TIM2_BASE + 0x28))
#define TIM2_ARR                (*(volatile uint32_t *)(TIM2_BASE + 0x2C))

/* USART2 (debug serial) */
#define USART2_BASE             (APB1PERIPH_BASE + 0x4400UL)
#define USART2_CR1              (*(volatile uint32_t *)(USART2_BASE + 0x00))
#define USART2_CR2              (*(volatile uint32_t *)(USART2_BASE + 0x04))
#define USART2_CR3              (*(volatile uint32_t *)(USART2_BASE + 0x08))
#define USART2_BRR              (*(volatile uint32_t *)(USART2_BASE + 0x0C))
#define USART2_ISR              (*(volatile uint32_t *)(USART2_BASE + 0x1C))
#define USART2_TDR              (*(volatile uint32_t *)(USART2_BASE + 0x28))
#define USART2_RDR              (*(volatile uint32_t *)(USART2_BASE + 0x24))

/* FDCAN1 base (STM32G4) */
#define FDCAN1_BASE             (APB1PERIPH_BASE + 0x6400UL)

/* bxCAN-style registers (compatible with Renode STMCAN model) */
#define CAN_MCR                 (*(volatile uint32_t *)(FDCAN1_BASE + 0x00))
#define CAN_MSR                 (*(volatile uint32_t *)(FDCAN1_BASE + 0x04))
#define CAN_TSR                 (*(volatile uint32_t *)(FDCAN1_BASE + 0x08))
#define CAN_RF0R                (*(volatile uint32_t *)(FDCAN1_BASE + 0x0C))
#define CAN_RF1R                (*(volatile uint32_t *)(FDCAN1_BASE + 0x10))
#define CAN_IER                 (*(volatile uint32_t *)(FDCAN1_BASE + 0x14))
#define CAN_ESR                 (*(volatile uint32_t *)(FDCAN1_BASE + 0x18))
#define CAN_BTR                 (*(volatile uint32_t *)(FDCAN1_BASE + 0x1C))

/* TX Mailboxes (3 mailboxes) */
#define CAN_TI0R                (*(volatile uint32_t *)(FDCAN1_BASE + 0x180))
#define CAN_TDT0R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x184))
#define CAN_TDL0R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x188))
#define CAN_TDH0R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x18C))

#define CAN_TI1R                (*(volatile uint32_t *)(FDCAN1_BASE + 0x190))
#define CAN_TDT1R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x194))
#define CAN_TDL1R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x198))
#define CAN_TDH1R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x19C))

#define CAN_TI2R                (*(volatile uint32_t *)(FDCAN1_BASE + 0x1A0))
#define CAN_TDT2R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x1A4))
#define CAN_TDL2R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x1A8))
#define CAN_TDH2R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x1AC))

/* RX FIFO 0 */
#define CAN_RI0R                (*(volatile uint32_t *)(FDCAN1_BASE + 0x1B0))
#define CAN_RDT0R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x1B4))
#define CAN_RDL0R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x1B8))
#define CAN_RDH0R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x1BC))

/* RX FIFO 1 */
#define CAN_RI1R                (*(volatile uint32_t *)(FDCAN1_BASE + 0x1C0))
#define CAN_RDT1R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x1C4))
#define CAN_RDL1R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x1C8))
#define CAN_RDH1R               (*(volatile uint32_t *)(FDCAN1_BASE + 0x1CC))

/* CAN register bits */
#define CAN_MCR_INRQ            (1 << 0)   /* Initialization request */
#define CAN_MCR_SLEEP           (1 << 1)   /* Sleep mode request */
#define CAN_MSR_INAK            (1 << 0)   /* Initialization acknowledge */
#define CAN_MSR_SLAK            (1 << 1)   /* Sleep acknowledge */
#define CAN_TSR_TME0            (1 << 26)  /* TX mailbox 0 empty */
#define CAN_TSR_TME1            (1 << 27)  /* TX mailbox 1 empty */
#define CAN_TSR_TME2            (1 << 28)  /* TX mailbox 2 empty */
#define CAN_TSR_RQCP0           (1 << 0)   /* Request completed MB0 */
#define CAN_RF0R_FMP0_MASK      (0x3)      /* FIFO 0 message pending */
#define CAN_RF0R_RFOM0          (1 << 5)   /* Release FIFO 0 output mailbox */
#define CAN_TIxR_TXRQ           (1 << 0)   /* TX request */
#define CAN_TIxR_IDE            (1 << 2)   /* Extended ID */
#define CAN_TIxR_STID_SHIFT     21         /* Standard ID position */

/* CAN ID encoding for EK-KOR messages:
 * Bits [10:8] = Message type (3 bits)
 * Bits [7:0]  = Sender ID (8 bits)
 * CAN_ID = (msg_type << 8) | sender_id
 */
#define CAN_ID_ENCODE(msg_type, sender_id) (((msg_type) << 8) | (sender_id))
#define CAN_ID_GET_TYPE(can_id)            (((can_id) >> 8) & 0x07)
#define CAN_ID_GET_SENDER(can_id)          ((can_id) & 0xFF)

/* Module ID override location (written by Renode before start) */
#define MODULE_ID_OVERRIDE_ADDR 0x20017FFCUL
#define MODULE_ID_OVERRIDE      (*(volatile uint32_t *)MODULE_ID_OVERRIDE_ADDR)
#define MODULE_ID_MAGIC         0xECC00000UL  /* Magic pattern in upper bits */

/* CAN debug logging flag */
#ifndef EKK_CAN_DEBUG
#define EKK_CAN_DEBUG 1
#endif

/* GPIO */
#define GPIOA_BASE              (AHB2PERIPH_BASE + 0x0000UL)
#define GPIOA_MODER             (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_AFRL              (*(volatile uint32_t *)(GPIOA_BASE + 0x20))
#define GPIOA_AFRH              (*(volatile uint32_t *)(GPIOA_BASE + 0x24))

/* Unique device ID (96 bits) */
#define UID_BASE                0x1FFF7590UL
#define UID0                    (*(volatile uint32_t *)(UID_BASE + 0x00))
#define UID1                    (*(volatile uint32_t *)(UID_BASE + 0x04))
#define UID2                    (*(volatile uint32_t *)(UID_BASE + 0x08))

/* System clock (assuming 170 MHz) */
#define SYSCLK_FREQ             170000000UL
#define APB1_FREQ               170000000UL
#define APB2_FREQ               170000000UL

/* ============================================================================
 * MEMORY SECTIONS
 * ============================================================================ */

/* Field region in SRAM2 (16KB available, use 12KB for fields) */
#define FIELD_REGION_ADDR       0x20014000UL
#define FIELD_REGION_SIZE       (12 * 1024)

/* Message queues in SRAM1 */
#define MSG_QUEUE_ADDR          0x20010000UL
#define MSG_QUEUE_SIZE          64
#define MSG_MAX_LEN             64

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

static hal_message_t g_msg_queue[MSG_QUEUE_SIZE] __attribute__((section(".sram1")));
static volatile uint32_t g_msg_head = 0;
static volatile uint32_t g_msg_tail = 0;

/* Receive callback */
static ekk_hal_recv_cb g_recv_callback = NULL;

/* Module ID (computed from UID) */
static ekk_module_id_t g_module_id = 0;

/* ============================================================================
 * CORTEX-M4 INTRINSICS
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
 * TIMER FUNCTIONS
 * ============================================================================ */

static void timer_init(void) {
    /* Enable TIM2 clock */
    RCC_APB1ENR1 |= (1 << 0);  /* TIM2EN */
    __DSB();

    /* Configure TIM2 as microsecond counter */
    TIM2_CR1 = 0;                       /* Disable timer */
    TIM2_PSC = (SYSCLK_FREQ / 1000000) - 1;  /* Prescaler for 1 MHz (1 us ticks) */
    TIM2_ARR = 0xFFFFFFFF;              /* Max count (32-bit) */
    TIM2_CNT = 0;                       /* Reset counter */
    TIM2_CR1 = 1;                       /* Enable timer */
}

ekk_time_us_t ekk_hal_time_us(void) {
    return (ekk_time_us_t)TIM2_CNT;
}

void ekk_hal_delay_us(uint32_t us) {
    uint32_t start = TIM2_CNT;
    while ((TIM2_CNT - start) < us) {
        /* Busy wait */
    }
}

/* ============================================================================
 * SERIAL (DEBUG OUTPUT)
 * ============================================================================ */

static void serial_init(void) {
    /* Enable GPIOA and USART2 clocks */
    RCC_AHB2ENR |= (1 << 0);   /* GPIOAEN */
    RCC_APB1ENR1 |= (1 << 17); /* USART2EN */
    __DSB();

    /* Configure PA2 (TX) and PA3 (RX) as alternate function 7 (USART2) */
    GPIOA_MODER &= ~((3 << 4) | (3 << 6));  /* Clear mode bits */
    GPIOA_MODER |= (2 << 4) | (2 << 6);     /* Alternate function */
    GPIOA_AFRL &= ~((0xF << 8) | (0xF << 12));
    GPIOA_AFRL |= (7 << 8) | (7 << 12);     /* AF7 = USART2 */

    /* Configure USART2: 115200 baud, 8N1 */
    USART2_CR1 = 0;  /* Disable USART */
    USART2_BRR = APB1_FREQ / 115200;  /* Baud rate */
    USART2_CR1 = (1 << 3) | (1 << 2); /* TE | RE */
    USART2_CR1 |= (1 << 0);           /* UE (enable) */
}

static void serial_putchar(char c) {
    while (!(USART2_ISR & (1 << 7)));  /* Wait for TXE */
    USART2_TDR = c;
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
 * CAN FUNCTIONS (bxCAN-style for Renode STMCAN compatibility)
 * ============================================================================ */

static void can_init(void) {
    /* Enable CAN clock */
    RCC_APB1ENR1 |= (1 << 25);  /* FDCAN1EN */
    __DSB();

    /* Exit sleep mode */
    CAN_MCR &= ~CAN_MCR_SLEEP;
    while (CAN_MSR & CAN_MSR_SLAK);

    /* Enter initialization mode */
    CAN_MCR |= CAN_MCR_INRQ;
    while (!(CAN_MSR & CAN_MSR_INAK));

    /* Configure bit timing for 1 Mbps
     * Assuming 170 MHz APB1 clock
     * BRP = 9 (prescaler 10), TS1 = 13, TS2 = 2, SJW = 1
     * Bit time = (1 + 13 + 2) * 10 / 170 MHz = 941 ns ~ 1.06 Mbps
     */
    CAN_BTR = (9 << 0) |           /* BRP = 9 (prescaler 10) */
              (12 << 16) |         /* TS1 = 12 (13 time quanta) */
              (1 << 20) |          /* TS2 = 1 (2 time quanta) */
              (0 << 24);           /* SJW = 0 (1 time quantum) */

    /* Enable FIFO 0 message pending interrupt */
    CAN_IER = (1 << 1);            /* FMPIE0 */

    /* Leave initialization mode */
    CAN_MCR &= ~CAN_MCR_INRQ;
    while (CAN_MSR & CAN_MSR_INAK);

#if EKK_CAN_DEBUG
    ekk_hal_printf("[CAN] Initialized (bxCAN mode)\n");
#endif
}

/**
 * @brief Find empty TX mailbox
 * @return Mailbox index (0-2) or -1 if all full
 */
static int can_find_empty_mailbox(void) {
    uint32_t tsr = CAN_TSR;
    if (tsr & CAN_TSR_TME0) return 0;
    if (tsr & CAN_TSR_TME1) return 1;
    if (tsr & CAN_TSR_TME2) return 2;
    return -1;
}

/**
 * @brief Transmit CAN frame
 *
 * @param std_id Standard 11-bit identifier
 * @param data Pointer to data (up to 8 bytes)
 * @param len Data length (0-8)
 * @return EKK_OK on success
 */
static ekk_error_t can_transmit(uint32_t std_id, const uint8_t *data, uint8_t len) {
    int mailbox = can_find_empty_mailbox();
    if (mailbox < 0) {
        return EKK_ERR_NO_MEMORY;
    }

    /* Limit to 8 bytes (standard CAN) */
    if (len > 8) len = 8;

    /* Prepare data words */
    uint32_t data_low = 0, data_high = 0;
    if (len > 0) data_low |= data[0];
    if (len > 1) data_low |= (uint32_t)data[1] << 8;
    if (len > 2) data_low |= (uint32_t)data[2] << 16;
    if (len > 3) data_low |= (uint32_t)data[3] << 24;
    if (len > 4) data_high |= data[4];
    if (len > 5) data_high |= (uint32_t)data[5] << 8;
    if (len > 6) data_high |= (uint32_t)data[6] << 16;
    if (len > 7) data_high |= (uint32_t)data[7] << 24;

    /* Write to appropriate mailbox */
    volatile uint32_t *tir, *tdtr, *tdlr, *tdhr;
    switch (mailbox) {
        case 0:
            tir = &CAN_TI0R; tdtr = &CAN_TDT0R;
            tdlr = &CAN_TDL0R; tdhr = &CAN_TDH0R;
            break;
        case 1:
            tir = &CAN_TI1R; tdtr = &CAN_TDT1R;
            tdlr = &CAN_TDL1R; tdhr = &CAN_TDH1R;
            break;
        default:
            tir = &CAN_TI2R; tdtr = &CAN_TDT2R;
            tdlr = &CAN_TDL2R; tdhr = &CAN_TDH2R;
            break;
    }

    /* Setup ID register (standard ID, no RTR) */
    *tir = (std_id << CAN_TIxR_STID_SHIFT);

    /* Setup data length */
    *tdtr = len;

    /* Write data */
    *tdlr = data_low;
    *tdhr = data_high;

    /* Request transmission */
    *tir |= CAN_TIxR_TXRQ;

    return EKK_OK;
}

/**
 * @brief Check for received CAN frame in FIFO 0
 *
 * @param[out] std_id Received standard ID
 * @param[out] data Buffer for data (at least 8 bytes)
 * @param[out] len Received data length
 * @return EKK_OK if message received, EKK_ERR_NOT_FOUND if FIFO empty
 */
static ekk_error_t can_receive(uint32_t *std_id, uint8_t *data, uint8_t *len) {
    /* Check if messages pending in FIFO 0 */
    uint32_t rf0r = CAN_RF0R;
    uint32_t fmp = rf0r & CAN_RF0R_FMP0_MASK;
    if (fmp == 0) {
        return EKK_ERR_NOT_FOUND;
    }

    /* Read ID register */
    uint32_t rir = CAN_RI0R;
    *std_id = (rir >> CAN_TIxR_STID_SHIFT) & 0x7FF;

    /* Read data length */
    uint32_t rdt = CAN_RDT0R;
    *len = rdt & 0x0F;
    if (*len > 8) *len = 8;

    /* Read data */
    uint32_t data_low = CAN_RDL0R;
    uint32_t data_high = CAN_RDH0R;

    if (*len > 0) data[0] = data_low & 0xFF;
    if (*len > 1) data[1] = (data_low >> 8) & 0xFF;
    if (*len > 2) data[2] = (data_low >> 16) & 0xFF;
    if (*len > 3) data[3] = (data_low >> 24) & 0xFF;
    if (*len > 4) data[4] = data_high & 0xFF;
    if (*len > 5) data[5] = (data_high >> 8) & 0xFF;
    if (*len > 6) data[6] = (data_high >> 16) & 0xFF;
    if (*len > 7) data[7] = (data_high >> 24) & 0xFF;

    /* Release FIFO */
    CAN_RF0R = CAN_RF0R_RFOM0;

    return EKK_OK;
}

/* ============================================================================
 * MESSAGE TRANSMISSION (via CAN bus)
 * ============================================================================ */

/**
 * @brief Send message via CAN bus
 *
 * CAN ID encoding: (msg_type << 8) | sender_id
 * For messages > 8 bytes, uses fragmentation (first byte = fragment info)
 */
ekk_error_t ekk_hal_send(ekk_module_id_t dest_id,
                          ekk_msg_type_t msg_type,
                          const void *data,
                          uint32_t len) {
    if (len > MSG_MAX_LEN) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Encode CAN ID: msg_type in bits [10:8], sender in bits [7:0] */
    uint32_t can_id = CAN_ID_ENCODE(msg_type, g_module_id);
    const uint8_t *payload = (const uint8_t *)data;

#if EKK_CAN_DEBUG
    ekk_hal_printf("[CAN TX] type=%u sender=%u dest=%u len=%lu id=0x%03lX\n",
                   msg_type, g_module_id, dest_id, (unsigned long)len,
                   (unsigned long)can_id);
#endif

    /* For messages <= 8 bytes, send single frame */
    if (len <= 8) {
        return can_transmit(can_id, payload, (uint8_t)len);
    }

    /* For larger messages, use fragmentation
     * Frame format: [frag_info, data...]
     * frag_info: bit 7 = more fragments, bits 6:0 = fragment index
     */
    uint32_t offset = 0;
    uint8_t frag_idx = 0;
    uint8_t frag_buf[8];

    while (offset < len) {
        uint32_t remaining = len - offset;
        uint32_t chunk = (remaining > 7) ? 7 : remaining;
        bool more = (offset + chunk) < len;

        /* Fragment header */
        frag_buf[0] = frag_idx | (more ? 0x80 : 0);

        /* Copy data */
        memcpy(&frag_buf[1], &payload[offset], chunk);

        ekk_error_t err = can_transmit(can_id, frag_buf, (uint8_t)(chunk + 1));
        if (err != EKK_OK) {
            return err;
        }

        offset += chunk;
        frag_idx++;
    }

    return EKK_OK;
}

ekk_error_t ekk_hal_broadcast(ekk_msg_type_t msg_type,
                               const void *data,
                               uint32_t len) {
    /* Broadcast is same as send - all modules receive on CAN bus */
    return ekk_hal_send(EKK_BROADCAST_ID, msg_type, data, len);
}

/* Reassembly buffer for fragmented messages */
static struct {
    uint8_t data[MSG_MAX_LEN];
    uint32_t len;
    uint8_t sender_id;
    uint8_t msg_type;
    uint8_t expected_frag;
    bool active;
} g_reassembly = {0};

/**
 * @brief Receive message from CAN bus
 *
 * Handles both single-frame and fragmented messages.
 */
ekk_error_t ekk_hal_recv(ekk_module_id_t *sender_id,
                          ekk_msg_type_t *msg_type,
                          void *data,
                          uint32_t *len) {
    uint32_t can_id;
    uint8_t can_data[8];
    uint8_t can_len;

    /* Try to receive a CAN frame */
    ekk_error_t err = can_receive(&can_id, can_data, &can_len);
    if (err != EKK_OK) {
        return err;
    }

    /* Decode CAN ID */
    uint8_t rx_msg_type = CAN_ID_GET_TYPE(can_id);
    uint8_t rx_sender = CAN_ID_GET_SENDER(can_id);

#if EKK_CAN_DEBUG
    ekk_hal_printf("[CAN RX] type=%u sender=%u len=%u id=0x%03lX\n",
                   rx_msg_type, rx_sender, can_len, (unsigned long)can_id);
#endif

    /* Check if this is a fragmented message (first byte has fragment info) */
    /* Heuristic: if len > 0 and first byte has high bit set or is 0x00/0x01,
     * and message type suggests fragmentation, treat as fragment.
     * For simplicity, only use fragmentation for DISCOVERY and PROPOSAL */
    bool is_fragment = false;
    if (can_len > 1 &&
        (rx_msg_type == EKK_MSG_DISCOVERY || rx_msg_type == EKK_MSG_PROPOSAL)) {
        uint8_t frag_info = can_data[0];
        uint8_t frag_idx = frag_info & 0x7F;
        bool more = (frag_info & 0x80) != 0;

        /* Check if this looks like fragment header */
        if (frag_idx < 16) {  /* Reasonable fragment index */
            is_fragment = true;

            if (frag_idx == 0) {
                /* Start new reassembly */
                g_reassembly.active = true;
                g_reassembly.sender_id = rx_sender;
                g_reassembly.msg_type = rx_msg_type;
                g_reassembly.len = 0;
                g_reassembly.expected_frag = 0;
            }

            if (g_reassembly.active &&
                g_reassembly.sender_id == rx_sender &&
                g_reassembly.msg_type == rx_msg_type &&
                frag_idx == g_reassembly.expected_frag) {

                /* Copy fragment data (skip header byte) */
                uint32_t chunk = can_len - 1;
                if (g_reassembly.len + chunk <= MSG_MAX_LEN) {
                    memcpy(&g_reassembly.data[g_reassembly.len],
                           &can_data[1], chunk);
                    g_reassembly.len += chunk;
                }
                g_reassembly.expected_frag++;

                if (!more) {
                    /* Last fragment - deliver complete message */
                    *sender_id = g_reassembly.sender_id;
                    *msg_type = (ekk_msg_type_t)g_reassembly.msg_type;
                    uint32_t copy_len = (*len < g_reassembly.len) ?
                                        *len : g_reassembly.len;
                    if (data) {
                        memcpy(data, g_reassembly.data, copy_len);
                    }
                    *len = g_reassembly.len;
                    g_reassembly.active = false;
                    return EKK_OK;
                }

                /* More fragments coming - need to receive them */
                return EKK_ERR_NOT_FOUND;
            }
        }
    }

    if (!is_fragment) {
        /* Single frame message */
        *sender_id = rx_sender;
        *msg_type = (ekk_msg_type_t)rx_msg_type;
        uint32_t copy_len = (*len < can_len) ? *len : can_len;
        if (data && copy_len > 0) {
            memcpy(data, can_data, copy_len);
        }
        *len = can_len;
        return EKK_OK;
    }

    return EKK_ERR_NOT_FOUND;
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
 * 1. Memory override (0x20017FFC) - set by Renode for multi-module testing
 * 2. Unique device ID - computed from MCU's 96-bit UID
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

    /* Fall back to UID-based ID */
    uint32_t uid0 = UID0;
    uint32_t uid1 = UID1;
    uint32_t uid2 = UID2;

    /* XOR all bytes together */
    uint32_t hash = uid0 ^ uid1 ^ uid2;
    hash ^= (hash >> 16);
    hash ^= (hash >> 8);

    /* Map to valid module ID range [1, 254] */
    uint8_t id = (hash & 0xFF);
    if (id == 0) id = 1;
    if (id == 255) id = 254;

    return (ekk_module_id_t)id;
}

ekk_error_t ekk_hal_init(void) {
    /* Initialize timer first (needed for timestamps) */
    timer_init();

    /* Initialize serial for debug output */
    serial_init();

    /* Compute unique module ID (checks memory override first) */
    g_module_id = compute_module_id();

    /* Initialize field region */
    memset(g_field_region, 0, sizeof(ekk_field_region_t));

    /* Initialize reassembly buffer */
    memset(&g_reassembly, 0, sizeof(g_reassembly));

    /* Initialize CAN bus (bxCAN-style for Renode compatibility) */
    can_init();

    ekk_hal_printf("EK-KOR HAL: STM32G474 initialized\n");
    ekk_hal_printf("  Module ID: %u\n", g_module_id);
    ekk_hal_printf("  UID: %08lX-%08lX-%08lX\n", (unsigned long)UID0,
                   (unsigned long)UID1, (unsigned long)UID2);

    return EKK_OK;
}

const char *ekk_hal_platform_name(void) {
    return "STM32G474 (Cortex-M4 @ 170MHz)";
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

#endif /* STM32G474xx || EKK_PLATFORM_STM32G474 */
