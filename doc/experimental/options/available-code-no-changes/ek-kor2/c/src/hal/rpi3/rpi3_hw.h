/**
 * @file rpi3_hw.h
 * @brief BCM2837B0 (Raspberry Pi 3B+) Hardware Register Definitions
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#ifndef RPI3_HW_H
#define RPI3_HW_H

#include <stdint.h>

/* ============================================================================
 * PERIPHERAL BASE ADDRESSES
 * ============================================================================ */

/* BCM2837 peripheral base (32-bit legacy view) */
#define PERIPHERAL_BASE         0x3F000000UL

/* ARM local peripherals base (for GIC, timers) */
#define ARM_LOCAL_BASE          0x40000000UL

/* GIC-400 base (AArch64 mode) */
#define GIC_BASE                0xFF840000UL

/* ============================================================================
 * GPIO REGISTERS
 * ============================================================================ */

#define GPIO_BASE               (PERIPHERAL_BASE + 0x200000UL)

#define GPFSEL0                 (GPIO_BASE + 0x00)  /* GPIO 0-9 function select */
#define GPFSEL1                 (GPIO_BASE + 0x04)  /* GPIO 10-19 function select */
#define GPFSEL2                 (GPIO_BASE + 0x08)  /* GPIO 20-29 function select */
#define GPFSEL3                 (GPIO_BASE + 0x0C)  /* GPIO 30-39 function select */
#define GPFSEL4                 (GPIO_BASE + 0x10)  /* GPIO 40-49 function select */
#define GPFSEL5                 (GPIO_BASE + 0x14)  /* GPIO 50-53 function select */

#define GPSET0                  (GPIO_BASE + 0x1C)  /* GPIO 0-31 output set */
#define GPSET1                  (GPIO_BASE + 0x20)  /* GPIO 32-53 output set */

#define GPCLR0                  (GPIO_BASE + 0x28)  /* GPIO 0-31 output clear */
#define GPCLR1                  (GPIO_BASE + 0x2C)  /* GPIO 32-53 output clear */

#define GPLEV0                  (GPIO_BASE + 0x34)  /* GPIO 0-31 level */
#define GPLEV1                  (GPIO_BASE + 0x38)  /* GPIO 32-53 level */

#define GPEDS0                  (GPIO_BASE + 0x40)  /* GPIO 0-31 event detect status */
#define GPEDS1                  (GPIO_BASE + 0x44)  /* GPIO 32-53 event detect status */

#define GPHEN0                  (GPIO_BASE + 0x64)  /* GPIO 0-31 high detect enable */
#define GPHEN1                  (GPIO_BASE + 0x68)  /* GPIO 32-53 high detect enable */

#define GPPUD                   (GPIO_BASE + 0x94)  /* GPIO pull-up/down enable */
#define GPPUDCLK0               (GPIO_BASE + 0x98)  /* GPIO pull-up/down clock 0 */
#define GPPUDCLK1               (GPIO_BASE + 0x9C)  /* GPIO pull-up/down clock 1 */

/* GPIO function select values */
#define GPIO_FUNC_INPUT         0
#define GPIO_FUNC_OUTPUT        1
#define GPIO_FUNC_ALT0          4
#define GPIO_FUNC_ALT1          5
#define GPIO_FUNC_ALT2          6
#define GPIO_FUNC_ALT3          7
#define GPIO_FUNC_ALT4          3
#define GPIO_FUNC_ALT5          2

/* ============================================================================
 * MINI-UART (AUX) REGISTERS
 * ============================================================================ */

#define AUX_BASE                (PERIPHERAL_BASE + 0x215000UL)

#define AUX_IRQ                 (AUX_BASE + 0x00)   /* Auxiliary interrupt status */
#define AUX_ENABLES             (AUX_BASE + 0x04)   /* Auxiliary enables */

#define AUX_MU_IO               (AUX_BASE + 0x40)   /* Mini UART I/O data */
#define AUX_MU_IER              (AUX_BASE + 0x44)   /* Mini UART interrupt enable */
#define AUX_MU_IIR              (AUX_BASE + 0x48)   /* Mini UART interrupt identify */
#define AUX_MU_LCR              (AUX_BASE + 0x4C)   /* Mini UART line control */
#define AUX_MU_MCR              (AUX_BASE + 0x50)   /* Mini UART modem control */
#define AUX_MU_LSR              (AUX_BASE + 0x54)   /* Mini UART line status */
#define AUX_MU_MSR              (AUX_BASE + 0x58)   /* Mini UART modem status */
#define AUX_MU_SCRATCH          (AUX_BASE + 0x5C)   /* Mini UART scratch */
#define AUX_MU_CNTL             (AUX_BASE + 0x60)   /* Mini UART extra control */
#define AUX_MU_STAT             (AUX_BASE + 0x64)   /* Mini UART extra status */
#define AUX_MU_BAUD             (AUX_BASE + 0x68)   /* Mini UART baudrate */

/* AUX_ENABLES bits */
#define AUX_ENABLE_MINIUART     (1 << 0)
#define AUX_ENABLE_SPI1         (1 << 1)
#define AUX_ENABLE_SPI2         (1 << 2)

/* AUX_MU_LSR bits */
#define AUX_MU_LSR_DATA_READY   (1 << 0)
#define AUX_MU_LSR_TX_EMPTY     (1 << 5)

/* AUX_MU_LCR bits */
#define AUX_MU_LCR_8BIT         0x03

/* ============================================================================
 * PL011 UART REGISTERS (Alternative to Mini-UART)
 * ============================================================================ */

#define UART0_BASE              (PERIPHERAL_BASE + 0x201000UL)

#define UART0_DR                (UART0_BASE + 0x00)
#define UART0_FR                (UART0_BASE + 0x18)
#define UART0_IBRD              (UART0_BASE + 0x24)
#define UART0_FBRD              (UART0_BASE + 0x28)
#define UART0_LCRH              (UART0_BASE + 0x2C)
#define UART0_CR                (UART0_BASE + 0x30)
#define UART0_ICR               (UART0_BASE + 0x44)

/* ============================================================================
 * SYSTEM TIMER REGISTERS (VideoCore timer, 1 MHz)
 * ============================================================================ */

#define SYSTIMER_BASE           (PERIPHERAL_BASE + 0x003000UL)

#define SYSTIMER_CS             (SYSTIMER_BASE + 0x00)  /* Control/status */
#define SYSTIMER_CLO            (SYSTIMER_BASE + 0x04)  /* Counter lower 32 bits */
#define SYSTIMER_CHI            (SYSTIMER_BASE + 0x08)  /* Counter upper 32 bits */
#define SYSTIMER_C0             (SYSTIMER_BASE + 0x0C)  /* Compare 0 (GPU) */
#define SYSTIMER_C1             (SYSTIMER_BASE + 0x10)  /* Compare 1 */
#define SYSTIMER_C2             (SYSTIMER_BASE + 0x14)  /* Compare 2 (GPU) */
#define SYSTIMER_C3             (SYSTIMER_BASE + 0x18)  /* Compare 3 */

/* ============================================================================
 * ARM LOCAL TIMER CONTROL (per-core)
 * ============================================================================ */

#define LOCAL_TIMER_CTRL        (ARM_LOCAL_BASE + 0x34)
#define LOCAL_TIMER_IRQ_CLR     (ARM_LOCAL_BASE + 0x38)

/* Core timer interrupt control (per core) */
#define CORE0_TIMER_IRQCNTL     (ARM_LOCAL_BASE + 0x40)
#define CORE1_TIMER_IRQCNTL     (ARM_LOCAL_BASE + 0x44)
#define CORE2_TIMER_IRQCNTL     (ARM_LOCAL_BASE + 0x48)
#define CORE3_TIMER_IRQCNTL     (ARM_LOCAL_BASE + 0x4C)

/* Core IRQ source (per core) */
#define CORE0_IRQ_SOURCE        (ARM_LOCAL_BASE + 0x60)
#define CORE1_IRQ_SOURCE        (ARM_LOCAL_BASE + 0x64)
#define CORE2_IRQ_SOURCE        (ARM_LOCAL_BASE + 0x68)
#define CORE3_IRQ_SOURCE        (ARM_LOCAL_BASE + 0x6C)

/* ============================================================================
 * GIC-400 DISTRIBUTOR REGISTERS
 * ============================================================================ */

#define GICD_BASE               (GIC_BASE + 0x1000UL)

#define GICD_CTLR               (GICD_BASE + 0x000)  /* Distributor control */
#define GICD_TYPER              (GICD_BASE + 0x004)  /* Interrupt type */
#define GICD_IIDR               (GICD_BASE + 0x008)  /* Implementer ID */

#define GICD_IGROUPR(n)         (GICD_BASE + 0x080 + (n) * 4)   /* Interrupt group */
#define GICD_ISENABLER(n)       (GICD_BASE + 0x100 + (n) * 4)   /* Set enable */
#define GICD_ICENABLER(n)       (GICD_BASE + 0x180 + (n) * 4)   /* Clear enable */
#define GICD_ISPENDR(n)         (GICD_BASE + 0x200 + (n) * 4)   /* Set pending */
#define GICD_ICPENDR(n)         (GICD_BASE + 0x280 + (n) * 4)   /* Clear pending */
#define GICD_ISACTIVER(n)       (GICD_BASE + 0x300 + (n) * 4)   /* Set active */
#define GICD_ICACTIVER(n)       (GICD_BASE + 0x380 + (n) * 4)   /* Clear active */
#define GICD_IPRIORITYR(n)      (GICD_BASE + 0x400 + (n) * 4)   /* Priority */
#define GICD_ITARGETSR(n)       (GICD_BASE + 0x800 + (n) * 4)   /* Target CPU */
#define GICD_ICFGR(n)           (GICD_BASE + 0xC00 + (n) * 4)   /* Config */
#define GICD_SGIR               (GICD_BASE + 0xF00)              /* Software generated */

/* GICD_CTLR bits */
#define GICD_CTLR_ENABLE        (1 << 0)

/* ============================================================================
 * GIC-400 CPU INTERFACE REGISTERS
 * ============================================================================ */

#define GICC_BASE               (GIC_BASE + 0x2000UL)

#define GICC_CTLR               (GICC_BASE + 0x000)  /* CPU interface control */
#define GICC_PMR                (GICC_BASE + 0x004)  /* Priority mask */
#define GICC_BPR                (GICC_BASE + 0x008)  /* Binary point */
#define GICC_IAR                (GICC_BASE + 0x00C)  /* Interrupt acknowledge */
#define GICC_EOIR               (GICC_BASE + 0x010)  /* End of interrupt */
#define GICC_RPR                (GICC_BASE + 0x014)  /* Running priority */
#define GICC_HPPIR              (GICC_BASE + 0x018)  /* Highest pending */
#define GICC_AIAR               (GICC_BASE + 0x020)  /* Alias interrupt ack */
#define GICC_AEOIR              (GICC_BASE + 0x024)  /* Alias end of interrupt */
#define GICC_AHPPIR             (GICC_BASE + 0x028)  /* Alias highest pending */

/* GICC_CTLR bits */
#define GICC_CTLR_ENABLE        (1 << 0)

/* ============================================================================
 * MAILBOX REGISTERS (GPU/ARM communication)
 * ============================================================================ */

#define MBOX_BASE               (PERIPHERAL_BASE + 0xB880UL)

#define MBOX_READ               (MBOX_BASE + 0x00)
#define MBOX_POLL               (MBOX_BASE + 0x10)
#define MBOX_SENDER             (MBOX_BASE + 0x14)
#define MBOX_STATUS             (MBOX_BASE + 0x18)
#define MBOX_CONFIG             (MBOX_BASE + 0x1C)
#define MBOX_WRITE              (MBOX_BASE + 0x20)

/* Mailbox status bits */
#define MBOX_FULL               (1 << 31)
#define MBOX_EMPTY              (1 << 30)

/* Mailbox channels */
#define MBOX_CH_POWER           0
#define MBOX_CH_FB              1
#define MBOX_CH_VUART           2
#define MBOX_CH_VCHIQ           3
#define MBOX_CH_LEDS            4
#define MBOX_CH_BTNS            5
#define MBOX_CH_TOUCH           6
#define MBOX_CH_COUNT           7
#define MBOX_CH_PROP            8   /* Property tags (ARM -> VC) */

/* ============================================================================
 * INTERRUPT NUMBERS
 * ============================================================================ */

/* SGI (Software Generated Interrupts) 0-15 */
#define IRQ_SGI_BASE            0

/* PPI (Private Peripheral Interrupts) 16-31 */
#define IRQ_PPI_BASE            16
#define IRQ_VTIMER              27  /* Virtual timer */
#define IRQ_PTIMER_NS           30  /* Non-secure physical timer */

/* SPI (Shared Peripheral Interrupts) 32+ */
#define IRQ_SPI_BASE            32
#define IRQ_SYSTIMER_1          (IRQ_SPI_BASE + 1)
#define IRQ_SYSTIMER_3          (IRQ_SPI_BASE + 3)
#define IRQ_USB                 (IRQ_SPI_BASE + 9)
#define IRQ_AUX                 (IRQ_SPI_BASE + 29)   /* Mini-UART, SPI1, SPI2 */
#define IRQ_GPIO0               (IRQ_SPI_BASE + 49)
#define IRQ_GPIO1               (IRQ_SPI_BASE + 50)
#define IRQ_GPIO2               (IRQ_SPI_BASE + 51)
#define IRQ_GPIO3               (IRQ_SPI_BASE + 52)
#define IRQ_UART                (IRQ_SPI_BASE + 57)   /* PL011 UART */

/* ============================================================================
 * EMMC (SD CARD) REGISTERS
 * ============================================================================ */

#define EMMC_BASE               (PERIPHERAL_BASE + 0x300000UL)

/* EMMC registers */
#define EMMC_ARG2               (EMMC_BASE + 0x00)  /* ACMD23 argument */
#define EMMC_BLKSIZECNT         (EMMC_BASE + 0x04)  /* Block size and count */
#define EMMC_ARG1               (EMMC_BASE + 0x08)  /* Command argument */
#define EMMC_CMDTM              (EMMC_BASE + 0x0C)  /* Command and transfer mode */
#define EMMC_RESP0              (EMMC_BASE + 0x10)  /* Response bits 31:0 */
#define EMMC_RESP1              (EMMC_BASE + 0x14)  /* Response bits 63:32 */
#define EMMC_RESP2              (EMMC_BASE + 0x18)  /* Response bits 95:64 */
#define EMMC_RESP3              (EMMC_BASE + 0x1C)  /* Response bits 127:96 */
#define EMMC_DATA               (EMMC_BASE + 0x20)  /* Data FIFO */
#define EMMC_STATUS             (EMMC_BASE + 0x24)  /* Status */
#define EMMC_CONTROL0           (EMMC_BASE + 0x28)  /* Host control 0 */
#define EMMC_CONTROL1           (EMMC_BASE + 0x2C)  /* Host control 1 */
#define EMMC_INTERRUPT          (EMMC_BASE + 0x30)  /* Interrupt flags */
#define EMMC_IRPT_MASK          (EMMC_BASE + 0x34)  /* Interrupt mask */
#define EMMC_IRPT_EN            (EMMC_BASE + 0x38)  /* Interrupt enable */
#define EMMC_CONTROL2           (EMMC_BASE + 0x3C)  /* Host control 2 */
#define EMMC_FORCE_IRPT         (EMMC_BASE + 0x50)  /* Force interrupt */
#define EMMC_BOOT_TIMEOUT       (EMMC_BASE + 0x70)  /* Boot timeout */
#define EMMC_DBG_SEL            (EMMC_BASE + 0x74)  /* Debug bus select */
#define EMMC_EXRDFIFO_CFG       (EMMC_BASE + 0x80)  /* Extended FIFO config */
#define EMMC_EXRDFIFO_EN        (EMMC_BASE + 0x84)  /* Extended FIFO enable */
#define EMMC_TUNE_STEP          (EMMC_BASE + 0x88)  /* Tuning step */
#define EMMC_TUNE_STEPS_STD     (EMMC_BASE + 0x8C)  /* Tuning steps standard */
#define EMMC_TUNE_STEPS_DDR     (EMMC_BASE + 0x90)  /* Tuning steps DDR */
#define EMMC_SPI_INT_SPT        (EMMC_BASE + 0xF0)  /* SPI interrupt support */
#define EMMC_SLOTISR_VER        (EMMC_BASE + 0xFC)  /* Slot ISR version */

/* EMMC_STATUS bits */
#define EMMC_STATUS_CMD_INHIBIT     (1 << 0)
#define EMMC_STATUS_DAT_INHIBIT     (1 << 1)
#define EMMC_STATUS_DAT_ACTIVE      (1 << 2)
#define EMMC_STATUS_WRITE_TRANSFER  (1 << 8)
#define EMMC_STATUS_READ_TRANSFER   (1 << 9)
#define EMMC_STATUS_DAT0_LEVEL      (1 << 20)
#define EMMC_STATUS_CMD_LEVEL       (1 << 24)

/* EMMC_CONTROL0 bits */
#define EMMC_C0_HCTL_DWIDTH         (1 << 1)    /* 4-bit data width */
#define EMMC_C0_HCTL_HS_EN          (1 << 2)    /* High speed enable */
#define EMMC_C0_HCTL_8BIT           (1 << 5)    /* 8-bit data width */

/* EMMC_CONTROL1 bits */
#define EMMC_C1_CLK_INTLEN          (1 << 0)    /* Internal clock enable */
#define EMMC_C1_CLK_STABLE          (1 << 1)    /* Internal clock stable */
#define EMMC_C1_CLK_EN              (1 << 2)    /* SD clock enable */
#define EMMC_C1_CLK_GENSEL          (1 << 5)    /* Clock generator select */
#define EMMC_C1_SRST_HC             (1 << 24)   /* Reset host circuit */
#define EMMC_C1_SRST_CMD            (1 << 25)   /* Reset command circuit */
#define EMMC_C1_SRST_DATA           (1 << 26)   /* Reset data circuit */

/* EMMC_INTERRUPT bits */
#define EMMC_INT_CMD_DONE           (1 << 0)
#define EMMC_INT_DATA_DONE          (1 << 1)
#define EMMC_INT_BLOCK_GAP          (1 << 2)
#define EMMC_INT_WRITE_RDY          (1 << 4)
#define EMMC_INT_READ_RDY           (1 << 5)
#define EMMC_INT_CARD               (1 << 8)
#define EMMC_INT_RETUNE             (1 << 12)
#define EMMC_INT_BOOTACK            (1 << 13)
#define EMMC_INT_ENDBOOT            (1 << 14)
#define EMMC_INT_ERR                (1 << 15)
#define EMMC_INT_CTO_ERR            (1 << 16)   /* Command timeout */
#define EMMC_INT_CCRC_ERR           (1 << 17)   /* Command CRC error */
#define EMMC_INT_CEND_ERR           (1 << 18)   /* Command end bit error */
#define EMMC_INT_CBAD_ERR           (1 << 19)   /* Command index error */
#define EMMC_INT_DTO_ERR            (1 << 20)   /* Data timeout */
#define EMMC_INT_DCRC_ERR           (1 << 21)   /* Data CRC error */
#define EMMC_INT_DEND_ERR           (1 << 22)   /* Data end bit error */
#define EMMC_INT_ACMD_ERR           (1 << 24)   /* Auto CMD error */

#define EMMC_INT_ERROR_MASK         (EMMC_INT_CTO_ERR | EMMC_INT_CCRC_ERR | \
                                     EMMC_INT_CEND_ERR | EMMC_INT_CBAD_ERR | \
                                     EMMC_INT_DTO_ERR | EMMC_INT_DCRC_ERR | \
                                     EMMC_INT_DEND_ERR | EMMC_INT_ACMD_ERR | \
                                     EMMC_INT_ERR)

/* EMMC_CMDTM bits */
#define EMMC_CMD_RSPNS_TYPE_NONE    (0 << 16)
#define EMMC_CMD_RSPNS_TYPE_136     (1 << 16)   /* R2 */
#define EMMC_CMD_RSPNS_TYPE_48      (2 << 16)   /* R1, R3, R6, R7 */
#define EMMC_CMD_RSPNS_TYPE_48B     (3 << 16)   /* R1b */
#define EMMC_CMD_CRCCHK_EN          (1 << 19)
#define EMMC_CMD_IXCHK_EN           (1 << 20)
#define EMMC_CMD_ISDATA             (1 << 21)
#define EMMC_CMD_TM_DAT_DIR_HC      (0 << 4)    /* Host to card */
#define EMMC_CMD_TM_DAT_DIR_CH      (1 << 4)    /* Card to host */
#define EMMC_CMD_TM_BLKCNT_EN       (1 << 1)
#define EMMC_CMD_TM_MULTI_BLOCK     (1 << 5)
#define EMMC_CMD_TM_AUTO_CMD12      (1 << 2)

/* SD Commands */
#define SD_CMD_GO_IDLE              0
#define SD_CMD_ALL_SEND_CID         2
#define SD_CMD_SEND_REL_ADDR        3
#define SD_CMD_SELECT_CARD          7
#define SD_CMD_SEND_IF_COND         8
#define SD_CMD_STOP_TRANSMISSION    12
#define SD_CMD_READ_SINGLE_BLOCK    17
#define SD_CMD_READ_MULTIPLE_BLOCK  18
#define SD_CMD_WRITE_SINGLE_BLOCK   24
#define SD_CMD_WRITE_MULTIPLE_BLOCK 25
#define SD_CMD_APP_CMD              55
#define SD_CMD_SET_BUS_WIDTH        (0x80 + 6)  /* ACMD6 */
#define SD_CMD_SEND_OP_COND         (0x80 + 41) /* ACMD41 */

/* ============================================================================
 * MEMORY MAP
 * ============================================================================ */

/* Kernel load address (default for kernel8.img) */
#define KERNEL_LOAD_ADDR        0x80000UL

/* ARM Generic Timer frequency on RPi3 (19.2 MHz) */
#define ARM_TIMER_FREQ_HZ       19200000UL
#define ARM_TIMER_FREQ_MHZ      19

/* ============================================================================
 * REGISTER ACCESS MACROS
 * ============================================================================ */

#define REG32(addr)             (*(volatile uint32_t *)(addr))
#define REG64(addr)             (*(volatile uint64_t *)(addr))

/* Read/write with explicit pointer cast */
static inline uint32_t mmio_read32(uintptr_t addr)
{
    return *(volatile uint32_t *)addr;
}

static inline void mmio_write32(uintptr_t addr, uint32_t value)
{
    *(volatile uint32_t *)addr = value;
}

static inline uint64_t mmio_read64(uintptr_t addr)
{
    return *(volatile uint64_t *)addr;
}

static inline void mmio_write64(uintptr_t addr, uint64_t value)
{
    *(volatile uint64_t *)addr = value;
}

/* ============================================================================
 * DELAY LOOPS
 * ============================================================================ */

static inline void delay_cycles(uint32_t cycles)
{
    while (cycles--) {
        __asm__ volatile("nop");
    }
}

#endif /* RPI3_HW_H */
