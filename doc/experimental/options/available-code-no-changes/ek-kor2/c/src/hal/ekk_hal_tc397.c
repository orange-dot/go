/**
 * @file ekk_hal_tc397.c
 * @brief EK-KOR v2 - Hardware Abstraction Layer for Infineon AURIX TC397
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * HAL implementation for AURIX TC397 gateway platform.
 *
 * HARDWARE SPECIFICATIONS:
 * - CPU: 6× TriCore @ 300 MHz (3 main + 3 checker for lockstep)
 * - Flash: 16 MB PFLASH (Program Flash)
 * - SRAM: 2.9 MB
 * - CAN-FD: 12× MCMCAN modules
 * - Ethernet: Gigabit Ethernet MAC
 * - Timer: STM (System Timer Module), 100MHz resolution
 *
 * SAFETY FEATURES:
 * - Lockstep cores (TC39x B-step)
 * - ECC on SRAM and Flash
 * - SafeTCOM for cross-core communication
 * - ASIL-D capable
 *
 * @note This is a template/stub implementation.
 *       Actual implementation requires AURIX SDK (Infineon MCAL).
 */

#include "ekk/ekk_hal.h"
#include "ekk/ekk_gateway.h"

/* ============================================================================
 * TC397 REGISTER DEFINITIONS (Simplified)
 * ============================================================================ */

#ifdef EKK_TARGET_TC397

/* System Timer Module (STM) */
#define STM0_BASE           0xF0001000
#define STM0_TIM0           (*(volatile uint32_t *)(STM0_BASE + 0x10))
#define STM0_TIM0_SV        (*(volatile uint32_t *)(STM0_BASE + 0x14))

/* Flash (PFLASH) */
#define PFLASH_BASE         0x80000000
#define PFLASH_SIZE         0x01000000  /* 16 MB */

/* PFLASH Controller */
#define DMU_BASE            0xF8002000
#define DMU_HF_STATUS       (*(volatile uint32_t *)(DMU_BASE + 0x00))
#define DMU_HF_CONTROL      (*(volatile uint32_t *)(DMU_BASE + 0x04))
#define DMU_HF_PROCON       (*(volatile uint32_t *)(DMU_BASE + 0x08))

/* MCMCAN (CAN-FD Module) */
#define MCMCAN0_BASE        0xF0200000
#define MCMCAN_CCCR(base)   (*(volatile uint32_t *)((base) + 0x018))
#define MCMCAN_NBTP(base)   (*(volatile uint32_t *)((base) + 0x01C))
#define MCMCAN_DBTP(base)   (*(volatile uint32_t *)((base) + 0x00C))
#define MCMCAN_TXBAR(base)  (*(volatile uint32_t *)((base) + 0x0D0))
#define MCMCAN_IR(base)     (*(volatile uint32_t *)((base) + 0x050))

/* CAN Message RAM */
#define CAN_MSG_RAM_BASE    0xF0210000
#define CAN_MSG_RAM_SIZE    0x10000     /* 64KB per node */

#endif /* EKK_TARGET_TC397 */

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

static struct {
    bool initialized;
    uint64_t time_offset_us;        /* Offset for time synchronization */
    uint32_t can_tx_count[12];      /* TX counters per CAN node */
    uint32_t can_rx_count[12];      /* RX counters per CAN node */
    uint32_t flash_write_count;
    uint32_t flash_erase_count;
} hal_state = {0};

/* ============================================================================
 * TIME
 * ============================================================================ */

ekk_time_us_t ekk_hal_time_us(void) {
#ifdef EKK_TARGET_TC397
    /* STM runs at 100 MHz on TC397 */
    /* Read 64-bit timer value (STM0_TIM0 is lower 32 bits) */
    uint32_t low, high;
    do {
        high = STM0_TIM0_SV;
        low = STM0_TIM0;
    } while (high != STM0_TIM0_SV);  /* Handle rollover */

    uint64_t ticks = ((uint64_t)high << 32) | low;

    /* Convert to microseconds: ticks / 100 (100 MHz clock) */
    return ticks / 100;
#else
    /* Fallback for simulation/testing */
    static uint64_t sim_time = 0;
    return sim_time++;
#endif
}

void ekk_hal_sleep_us(uint32_t us) {
#ifdef EKK_TARGET_TC397
    ekk_time_us_t start = ekk_hal_time_us();
    while ((ekk_hal_time_us() - start) < us) {
        /* Busy wait - could use WFI for power saving */
        __asm__ volatile ("nop");
    }
#else
    EKK_UNUSED(us);
#endif
}

/* ============================================================================
 * CAN-FD MESSAGING
 * ============================================================================ */

ekk_error_t ekk_hal_send(ekk_module_id_t dest, uint8_t msg_type,
                          const uint8_t *data, uint32_t len) {
#ifdef EKK_TARGET_TC397
    if (len > 64) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Select CAN node based on destination (round-robin across 12 nodes) */
    uint32_t node = dest % 12;
    uint32_t base = MCMCAN0_BASE + (node * 0x1000);

    /* Build CAN-FD arbitration ID */
    /* Format: [28:26]=priority, [25:20]=msg_type, [19:12]=src, [11:4]=dest, [3:0]=frag */
    uint32_t arb_id = (0 << 26) |                    /* Priority 0 (highest) */
                      ((msg_type & 0x3F) << 20) |    /* Message type */
                      ((hal_state.initialized ? 0xFF : 0) << 12) | /* Source (gateway=0xFF) */
                      ((dest & 0xFF) << 4) |         /* Destination */
                      (0);                           /* Fragment 0 */

    /* Get TX buffer in message RAM */
    uint32_t tx_buf = CAN_MSG_RAM_BASE + (node * CAN_MSG_RAM_SIZE) + 0x8000;

    /* Write TX buffer element */
    volatile uint32_t *tx = (volatile uint32_t *)tx_buf;
    tx[0] = arb_id | (1 << 30);     /* Extended ID, XTD=1 */
    tx[1] = (1 << 21) |             /* FDF=1 (CAN-FD) */
            (1 << 20) |             /* BRS=1 (Bit rate switch) */
            ((len > 8 ? 15 : (len > 0 ? len - 1 : 0)) << 16); /* DLC */

    /* Copy data */
    memcpy((void *)&tx[2], data, len);

    /* Request transmission */
    MCMCAN_TXBAR(base) = 1;  /* Request TX buffer 0 */

    hal_state.can_tx_count[node]++;

    return EKK_OK;
#else
    /* Simulation stub */
    EKK_UNUSED(dest);
    EKK_UNUSED(msg_type);
    EKK_UNUSED(data);
    EKK_UNUSED(len);
    return EKK_OK;
#endif
}

ekk_error_t ekk_hal_broadcast(uint8_t msg_type, const uint8_t *data, uint32_t len) {
    return ekk_hal_send(EKK_BROADCAST_ID, msg_type, data, len);
}

/* ============================================================================
 * FLASH OPERATIONS
 * ============================================================================ */

ekk_error_t ekk_hal_flash_read(uint32_t address, void *buffer, uint32_t len) {
#ifdef EKK_TARGET_TC397
    /* TC397 Flash is memory-mapped, direct read */
    if (address < PFLASH_BASE || address + len > PFLASH_BASE + PFLASH_SIZE) {
        return EKK_ERR_INVALID_ARG;
    }

    memcpy(buffer, (const void *)address, len);
    return EKK_OK;
#else
    /* Simulation: assume memory-mapped */
    memcpy(buffer, (const void *)(uintptr_t)address, len);
    return EKK_OK;
#endif
}

ekk_error_t ekk_hal_flash_write(uint32_t address, const void *data, uint32_t len) {
#ifdef EKK_TARGET_TC397
    if (address < PFLASH_BASE || address + len > PFLASH_BASE + PFLASH_SIZE) {
        return EKK_ERR_INVALID_ARG;
    }

    /* TC397 Flash programming requires:
     * 1. Unlock flash
     * 2. Issue program command
     * 3. Wait for completion
     * 4. Lock flash
     *
     * This is a simplified implementation - real code would use MCAL drivers.
     */

    /* Check alignment (must be 256-byte aligned for page program) */
    if ((address & 0xFF) != 0 || (len & 0xFF) != 0) {
        /* For partial writes, need to read-modify-write */
        return EKK_ERR_INVALID_ARG;
    }

    /* Wait for flash ready */
    while (DMU_HF_STATUS & 0x01) {
        __asm__ volatile ("nop");
    }

    /* Program pages (256 bytes each) */
    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t offset = 0; offset < len; offset += 256) {
        /* Issue page program command */
        /* ... (platform-specific sequence) ... */

        /* Wait for completion */
        while (DMU_HF_STATUS & 0x01) {
            __asm__ volatile ("nop");
        }

        hal_state.flash_write_count++;
    }

    return EKK_OK;
#else
    EKK_UNUSED(address);
    EKK_UNUSED(data);
    EKK_UNUSED(len);
    return EKK_OK;
#endif
}

ekk_error_t ekk_hal_flash_erase_sector(uint32_t address) {
#ifdef EKK_TARGET_TC397
    if (address < PFLASH_BASE || address >= PFLASH_BASE + PFLASH_SIZE) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Align to sector boundary (16KB) */
    address &= ~(EKK_FLASH_SECTOR_SIZE - 1);

    /* Wait for flash ready */
    while (DMU_HF_STATUS & 0x01) {
        __asm__ volatile ("nop");
    }

    /* Issue sector erase command */
    /* ... (platform-specific sequence) ... */

    /* Wait for completion */
    while (DMU_HF_STATUS & 0x01) {
        __asm__ volatile ("nop");
    }

    hal_state.flash_erase_count++;

    return EKK_OK;
#else
    EKK_UNUSED(address);
    return EKK_OK;
#endif
}

/* ============================================================================
 * ETHERNET (for upstream sync)
 * ============================================================================ */

/**
 * @brief Initialize Ethernet MAC
 */
ekk_error_t ekk_hal_eth_init(void) {
#ifdef EKK_TARGET_TC397
    /* Initialize Gigabit Ethernet MAC */
    /* ... (platform-specific) ... */
    return EKK_OK;
#else
    return EKK_OK;
#endif
}

/**
 * @brief Send Ethernet frame
 */
ekk_error_t ekk_hal_eth_send(const uint8_t *frame, uint32_t len) {
#ifdef EKK_TARGET_TC397
    /* Queue frame for transmission */
    /* ... (platform-specific) ... */
    EKK_UNUSED(frame);
    EKK_UNUSED(len);
    return EKK_OK;
#else
    EKK_UNUSED(frame);
    EKK_UNUSED(len);
    return EKK_OK;
#endif
}

/**
 * @brief Receive Ethernet frame
 */
ekk_error_t ekk_hal_eth_recv(uint8_t *frame, uint32_t *len, uint32_t timeout_ms) {
#ifdef EKK_TARGET_TC397
    /* Wait for frame or timeout */
    /* ... (platform-specific) ... */
    EKK_UNUSED(frame);
    EKK_UNUSED(len);
    EKK_UNUSED(timeout_ms);
    return EKK_ERR_TIMEOUT;
#else
    EKK_UNUSED(frame);
    EKK_UNUSED(len);
    EKK_UNUSED(timeout_ms);
    return EKK_ERR_TIMEOUT;
#endif
}

/* ============================================================================
 * CRITICAL SECTIONS
 * ============================================================================ */

static volatile uint32_t critical_nesting = 0;

void ekk_hal_enter_critical(void) {
#ifdef EKK_TARGET_TC397
    /* Disable interrupts and save ICR */
    __asm__ volatile (
        "disable"
    );
#endif
    critical_nesting++;
}

void ekk_hal_exit_critical(void) {
    critical_nesting--;
    if (critical_nesting == 0) {
#ifdef EKK_TARGET_TC397
        /* Restore interrupts */
        __asm__ volatile (
            "enable"
        );
#endif
    }
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

ekk_error_t ekk_hal_init(void) {
#ifdef EKK_TARGET_TC397
    /* Initialize STM (System Timer) */
    /* STM is typically enabled by default on TC397 */

    /* Initialize CAN-FD modules */
    for (int i = 0; i < 12; i++) {
        uint32_t base = MCMCAN0_BASE + (i * 0x1000);

        /* Enter configuration mode */
        MCMCAN_CCCR(base) |= (1 << 0);  /* INIT */
        while (!(MCMCAN_CCCR(base) & (1 << 0)));

        MCMCAN_CCCR(base) |= (1 << 1);  /* CCE (Configuration Change Enable) */

        /* Configure bit timing for 5 Mbps CAN-FD */
        /* Nominal: 500 kbps, Data: 5 Mbps */
        MCMCAN_NBTP(base) = (9 << 25) |  /* NSJW */
                           (9 << 16) |   /* NBRP */
                           (12 << 8) |   /* NTSEG1 */
                           (3 << 0);     /* NTSEG2 */

        MCMCAN_DBTP(base) = (3 << 20) |  /* DSJW */
                           (0 << 16) |   /* DBRP */
                           (4 << 8) |    /* DTSEG1 */
                           (1 << 0);     /* DTSEG2 */

        /* Enable CAN-FD */
        MCMCAN_CCCR(base) |= (1 << 8);   /* FDOE (FD Operation Enable) */
        MCMCAN_CCCR(base) |= (1 << 9);   /* BRSE (Bit Rate Switch Enable) */

        /* Leave configuration mode */
        MCMCAN_CCCR(base) &= ~(1 << 1);  /* Clear CCE */
        MCMCAN_CCCR(base) &= ~(1 << 0);  /* Clear INIT */
        while (MCMCAN_CCCR(base) & (1 << 0));
    }

    /* Initialize Ethernet */
    ekk_hal_eth_init();

#endif /* EKK_TARGET_TC397 */

    hal_state.initialized = true;
    return EKK_OK;
}

/* ============================================================================
 * DIAGNOSTICS
 * ============================================================================ */

/**
 * @brief Get HAL statistics
 */
void ekk_hal_tc397_get_stats(uint32_t *can_tx_total, uint32_t *can_rx_total,
                              uint32_t *flash_writes, uint32_t *flash_erases) {
    if (can_tx_total) {
        *can_tx_total = 0;
        for (int i = 0; i < 12; i++) {
            *can_tx_total += hal_state.can_tx_count[i];
        }
    }

    if (can_rx_total) {
        *can_rx_total = 0;
        for (int i = 0; i < 12; i++) {
            *can_rx_total += hal_state.can_rx_count[i];
        }
    }

    if (flash_writes) *flash_writes = hal_state.flash_write_count;
    if (flash_erases) *flash_erases = hal_state.flash_erase_count;
}
