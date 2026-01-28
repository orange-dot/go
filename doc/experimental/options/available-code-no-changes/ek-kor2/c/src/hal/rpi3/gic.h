/**
 * @file gic.h
 * @brief GIC-400 Interrupt Controller Header for Raspberry Pi 3B+
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#ifndef RPI3_GIC_H
#define RPI3_GIC_H

#include <stdint.h>

/**
 * @brief Initialize GIC Distributor (call once from core 0)
 */
void gic_dist_init(void);

/**
 * @brief Initialize GIC CPU Interface (call from each core)
 */
void gic_cpu_init(void);

/**
 * @brief Full GIC initialization (distributor + CPU interface)
 */
void gic_init(void);

/**
 * @brief Enable an interrupt
 * @param irq IRQ number (0-255)
 */
void gic_enable_irq(uint32_t irq);

/**
 * @brief Disable an interrupt
 * @param irq IRQ number (0-255)
 */
void gic_disable_irq(uint32_t irq);

/**
 * @brief Set interrupt priority
 * @param irq IRQ number
 * @param priority Priority (0 = highest, 255 = lowest)
 */
void gic_set_priority(uint32_t irq, uint8_t priority);

/**
 * @brief Set interrupt target CPUs
 * @param irq IRQ number
 * @param cpu_mask Bitmask of target CPUs
 */
void gic_set_target(uint32_t irq, uint8_t cpu_mask);

/**
 * @brief Acknowledge interrupt (call at start of IRQ handler)
 * @return IRQ number (1023 = spurious)
 */
uint32_t gic_ack_irq(void);

/**
 * @brief End of interrupt (call at end of IRQ handler)
 * @param irq IRQ number from gic_ack_irq()
 */
void gic_end_irq(uint32_t irq);

/**
 * @brief Send software-generated interrupt (IPI)
 * @param target_filter 0=targets, 1=all except self, 2=self only
 * @param cpu_mask Target CPU mask (if target_filter == 0)
 * @param sgi_id SGI number (0-15)
 */
void gic_send_sgi(uint32_t target_filter, uint8_t cpu_mask, uint8_t sgi_id);

/**
 * @brief Check if IRQ is pending
 * @param irq IRQ number
 * @return Non-zero if pending
 */
int gic_is_pending(uint32_t irq);

#endif /* RPI3_GIC_H */
