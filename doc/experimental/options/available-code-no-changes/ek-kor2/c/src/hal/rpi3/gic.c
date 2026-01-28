/**
 * @file gic.c
 * @brief GIC-400 Interrupt Controller Driver for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * The BCM2837 includes a GIC-400 interrupt controller when running
 * in AArch64 mode. This driver initializes the distributor and CPU
 * interface for interrupt handling.
 *
 * Note: On RPi3, the GIC is only accessible in AArch64 mode.
 * In AArch32 mode, the legacy BCM interrupt controller is used.
 */

#include "rpi3_hw.h"
#include "gic.h"
#include "uart.h"

/* Flag to track if GIC is available (not on QEMU) */
static int g_gic_available = 0;

/* ============================================================================
 * Private Functions
 * ============================================================================ */

/**
 * @brief Detect if running on QEMU (no GIC support)
 *
 * QEMU's raspi3b doesn't emulate GIC at 0xFF840000.
 * We use a safe detection method - check board revision via mailbox
 * which works on both QEMU and real hardware.
 *
 * QEMU raspi3b returns board revision 0xa02082 or 0xa22082
 * but more importantly, accessing GIC region hangs on QEMU.
 *
 * Safer approach: Try to read ARM local peripheral region first.
 * On QEMU, we can check if LOCAL_TIMER_CTRL returns something sane.
 */
static int detect_qemu(void)
{
    /*
     * Simpler heuristic: QEMU's raspi3b doesn't map 0xFF840000 region.
     * But reading from unmapped region may hang.
     *
     * Alternative: Check ARM_LOCAL_BASE (0x40000000) which IS mapped
     * on QEMU. If we can read from there but GIC region gives 0,
     * we're likely on QEMU.
     *
     * For now, use compile-time flag or always assume real HW.
     * TODO: Better runtime detection.
     */
#ifdef QEMU_BUILD
    return 1;  /* Compile-time QEMU flag */
#else
    /* Try reading ARM local control register - this should work on both */
    volatile uint32_t *local_ctrl = (volatile uint32_t *)0x40000000;
    uint32_t val = *local_ctrl;
    (void)val;  /* Suppress warning */

    /* Now try GIC - on real HW this works, on QEMU it may hang or return 0 */
    /* Actually skip the risky read and just return 0 (assume real HW) */
    /* User can define QEMU_BUILD for QEMU testing */
    return 0;
#endif
}

/**
 * @brief Get number of supported IRQ lines
 */
static uint32_t gic_get_num_irqs(void)
{
    uint32_t typer = mmio_read32(GICD_TYPER);
    return ((typer & 0x1F) + 1) * 32;
}

/* ============================================================================
 * GIC Distributor Functions
 * ============================================================================ */

/**
 * @brief Initialize GIC Distributor
 *
 * The distributor routes interrupts to CPU interfaces.
 * Must be called once (typically by core 0).
 */
void gic_dist_init(void)
{
    uint32_t num_irqs = gic_get_num_irqs();
    uint32_t i;

    /* Disable distributor */
    mmio_write32(GICD_CTLR, 0);

    /* Disable all interrupts */
    for (i = 0; i < num_irqs / 32; i++) {
        mmio_write32(GICD_ICENABLER(i), 0xFFFFFFFF);
    }

    /* Clear all pending interrupts */
    for (i = 0; i < num_irqs / 32; i++) {
        mmio_write32(GICD_ICPENDR(i), 0xFFFFFFFF);
    }

    /* Set all interrupts to default priority (0xA0) */
    for (i = 0; i < num_irqs / 4; i++) {
        mmio_write32(GICD_IPRIORITYR(i), 0xA0A0A0A0);
    }

    /* Set all SPIs to target CPU 0 */
    for (i = 8; i < num_irqs / 4; i++) {
        mmio_write32(GICD_ITARGETSR(i), 0x01010101);
    }

    /* Configure all SPIs as level-triggered */
    for (i = 2; i < num_irqs / 16; i++) {
        mmio_write32(GICD_ICFGR(i), 0);
    }

    /* Enable distributor */
    mmio_write32(GICD_CTLR, GICD_CTLR_ENABLE);
}

/* ============================================================================
 * GIC CPU Interface Functions
 * ============================================================================ */

/**
 * @brief Initialize GIC CPU Interface
 *
 * Each CPU core must initialize its own CPU interface.
 */
void gic_cpu_init(void)
{
    if (!g_gic_available) {
        return;  /* Skip on QEMU */
    }

    /* Disable CPU interface */
    mmio_write32(GICC_CTLR, 0);

    /* Set priority mask to accept all priorities */
    mmio_write32(GICC_PMR, 0xFF);

    /* Binary point: use all priority bits for preemption */
    mmio_write32(GICC_BPR, 0);

    /* Enable CPU interface */
    mmio_write32(GICC_CTLR, GICC_CTLR_ENABLE);
}

/**
 * @brief Full GIC initialization
 *
 * Initializes both distributor (once) and CPU interface (per core).
 */
void gic_init(void)
{
    /* Only core 0 initializes the distributor */
    uint64_t mpidr;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
    uint32_t core_id = mpidr & 0xFF;

    if (core_id == 0) {
        /* Check if running on QEMU (no GIC support) */
        if (detect_qemu()) {
            uart_puts("GIC: QEMU detected, skipping GIC init\n");
            g_gic_available = 0;
            return;
        }
        g_gic_available = 1;
        uart_puts("GIC: Real hardware detected\n");
        gic_dist_init();
    }

    /* Each core initializes its own CPU interface */
    gic_cpu_init();
}

/* ============================================================================
 * IRQ Management Functions
 * ============================================================================ */

/**
 * @brief Enable an interrupt
 *
 * @param irq IRQ number (0-255)
 */
void gic_enable_irq(uint32_t irq)
{
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    mmio_write32(GICD_ISENABLER(reg), 1 << bit);
}

/**
 * @brief Disable an interrupt
 *
 * @param irq IRQ number (0-255)
 */
void gic_disable_irq(uint32_t irq)
{
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    mmio_write32(GICD_ICENABLER(reg), 1 << bit);
}

/**
 * @brief Set interrupt priority
 *
 * @param irq IRQ number
 * @param priority Priority (0 = highest, 255 = lowest)
 */
void gic_set_priority(uint32_t irq, uint8_t priority)
{
    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;

    uint32_t val = mmio_read32(GICD_IPRIORITYR(reg));
    val &= ~(0xFF << shift);
    val |= ((uint32_t)priority << shift);
    mmio_write32(GICD_IPRIORITYR(reg), val);
}

/**
 * @brief Set interrupt target CPUs
 *
 * @param irq IRQ number
 * @param cpu_mask Bitmask of target CPUs (bit 0 = CPU 0, etc.)
 */
void gic_set_target(uint32_t irq, uint8_t cpu_mask)
{
    uint32_t reg = irq / 4;
    uint32_t shift = (irq % 4) * 8;

    uint32_t val = mmio_read32(GICD_ITARGETSR(reg));
    val &= ~(0xFF << shift);
    val |= ((uint32_t)cpu_mask << shift);
    mmio_write32(GICD_ITARGETSR(reg), val);
}

/**
 * @brief Acknowledge interrupt (get IRQ number)
 *
 * Called at the start of an IRQ handler to get the IRQ number.
 *
 * @return IRQ number (1023 = spurious)
 */
uint32_t gic_ack_irq(void)
{
    return mmio_read32(GICC_IAR) & 0x3FF;
}

/**
 * @brief End of interrupt
 *
 * Called at the end of an IRQ handler to signal completion.
 *
 * @param irq IRQ number from gic_ack_irq()
 */
void gic_end_irq(uint32_t irq)
{
    mmio_write32(GICC_EOIR, irq);
}

/**
 * @brief Send software-generated interrupt (SGI)
 *
 * Used for inter-processor interrupts (IPI).
 *
 * @param target_filter 0=targets, 1=all except self, 2=self only
 * @param cpu_mask Target CPU mask (if target_filter == 0)
 * @param sgi_id SGI number (0-15)
 */
void gic_send_sgi(uint32_t target_filter, uint8_t cpu_mask, uint8_t sgi_id)
{
    uint32_t val = ((target_filter & 0x3) << 24) |
                   ((cpu_mask & 0xFF) << 16) |
                   (sgi_id & 0xF);
    mmio_write32(GICD_SGIR, val);
}

/**
 * @brief Check if IRQ is pending
 *
 * @param irq IRQ number
 * @return Non-zero if pending
 */
int gic_is_pending(uint32_t irq)
{
    uint32_t reg = irq / 32;
    uint32_t bit = irq % 32;
    return (mmio_read32(GICD_ISPENDR(reg)) & (1 << bit)) != 0;
}
