/**
 * @file efr32mg24_config.h
 * @brief EK-KOR v2 - EFR32MG24 Configuration and Register Definitions
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Target: Silicon Labs EFR32MG24 (Cortex-M33, Series 2)
 *
 * Memory Map:
 *   Flash:  0x08000000 - 0x0817FFFF (1.5 MB)
 *   RAM:    0x20000000 - 0x2003FFFF (256 KB)
 *
 * Key features:
 *   - Cortex-M33 with TrustZone
 *   - 78 MHz system clock
 *   - Secure Vault for hardware security
 *   - 802.15.4 / BLE radio (future mesh support)
 *
 * ASIL-D Note:
 *   Silicon Labs has no ASIL certification. For ASIL-D compliance,
 *   use dual-chip architecture:
 *   - Option A: 2x EFR32MG24 (homogeneous, ASIL B(D) decomposition)
 *   - Option B: EFR32MG24 + TI C2000 F29 (heterogeneous, native ASIL-D monitor)
 */

#ifndef EFR32MG24_CONFIG_H
#define EFR32MG24_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * SYSTEM CONFIGURATION
 * ============================================================================ */

#define EFR32MG24_SYSCLK_FREQ       78000000UL   /* 78 MHz HFXO */
#define EFR32MG24_LFXO_FREQ         32768UL      /* 32.768 kHz LFXO */
#define EFR32MG24_HFRCO_FREQ        19000000UL   /* 19 MHz HFRCO (default) */

/* ============================================================================
 * MEMORY MAP
 * ============================================================================ */

/* Flash memory */
#define FLASH_BASE                  0x08000000UL
#define FLASH_SIZE                  (1536 * 1024)   /* 1.5 MB */

/* RAM */
#define SRAM_BASE                   0x20000000UL
#define SRAM_SIZE                   (256 * 1024)    /* 256 KB */

/* Field region - last 16 KB of RAM for EK-KOR shared data */
#define FIELD_REGION_ADDR           0x2003C000UL    /* 256KB - 16KB */
#define FIELD_REGION_SIZE           (16 * 1024)

/* Module ID override location (written by Renode before start) */
#define MODULE_ID_OVERRIDE_ADDR     0x2003FFFCUL
#define MODULE_ID_OVERRIDE          (*(volatile uint32_t *)MODULE_ID_OVERRIDE_ADDR)
#define MODULE_ID_MAGIC             0xECC00000UL    /* Magic pattern in upper bits */

/* Stack configuration */
#define STACK_SIZE                  0x2000          /* 8 KB main stack */
#define HEAP_SIZE                   0x4000          /* 16 KB heap */

/* ============================================================================
 * PERIPHERAL BASE ADDRESSES (EFR32xG24 Series 2)
 * ============================================================================ */

#define PERIPH_BASE                 0x40000000UL
#define SYSRTC_BASE                 0x400A8000UL    /* SYSRTC - System RTC */
#define TIMER0_BASE                 0x40048000UL    /* TIMER0 */
#define TIMER1_BASE                 0x4004C000UL    /* TIMER1 */
#define EUSART0_BASE                0x400A0000UL    /* EUSART0 - Enhanced USART */
#define EUSART1_BASE                0x400A4000UL    /* EUSART1 */
#define GPIO_BASE                   0x4003C000UL    /* GPIO */
#define CMU_BASE                    0x40008000UL    /* Clock Management Unit */
#define MSC_BASE                    0x40030000UL    /* Memory System Controller */
#define LDMA_BASE                   0x40040000UL    /* LDMA (Linked DMA) */
#define SYSCFG_BASE                 0x4002C000UL    /* System Configuration */
#define DEVINFO_BASE                0x0FE08000UL    /* Device Info (read-only) */

/* Cortex-M33 System Peripherals */
#define SCS_BASE                    0xE000E000UL    /* System Control Space */
#define NVIC_BASE                   (SCS_BASE + 0x100)
#define SCB_BASE                    (SCS_BASE + 0xD00)
#define SYSTICK_BASE                (SCS_BASE + 0x010)

/* ============================================================================
 * CMU (Clock Management Unit) REGISTERS
 * ============================================================================ */

#define CMU_CLKEN0                  (*(volatile uint32_t *)(CMU_BASE + 0x000))
#define CMU_CLKEN1                  (*(volatile uint32_t *)(CMU_BASE + 0x004))
#define CMU_SYSCLKCTRL              (*(volatile uint32_t *)(CMU_BASE + 0x060))
#define CMU_HFXOCTRL                (*(volatile uint32_t *)(CMU_BASE + 0x064))
#define CMU_STATUS                  (*(volatile uint32_t *)(CMU_BASE + 0x054))

/* CMU_CLKEN0 bits */
#define CMU_CLKEN0_SYSRTC           (1UL << 24)
#define CMU_CLKEN0_TIMER0           (1UL << 4)
#define CMU_CLKEN0_TIMER1           (1UL << 5)
#define CMU_CLKEN0_EUSART0          (1UL << 20)
#define CMU_CLKEN0_GPIO             (1UL << 16)
#define CMU_CLKEN0_LDMA             (1UL << 12)

/* ============================================================================
 * SYSRTC (System Real-Time Counter) REGISTERS
 * ============================================================================ */

#define SYSRTC_EN                   (*(volatile uint32_t *)(SYSRTC_BASE + 0x000))
#define SYSRTC_CTRL                 (*(volatile uint32_t *)(SYSRTC_BASE + 0x004))
#define SYSRTC_CMD                  (*(volatile uint32_t *)(SYSRTC_BASE + 0x008))
#define SYSRTC_STATUS               (*(volatile uint32_t *)(SYSRTC_BASE + 0x00C))
#define SYSRTC_CNT                  (*(volatile uint32_t *)(SYSRTC_BASE + 0x010))

/* SYSRTC_EN bits */
#define SYSRTC_EN_EN                (1UL << 0)

/* SYSRTC_CMD bits */
#define SYSRTC_CMD_START            (1UL << 0)
#define SYSRTC_CMD_STOP             (1UL << 1)

/* ============================================================================
 * TIMER0/1 REGISTERS (32-bit timer for microsecond counting)
 * ============================================================================ */

#define TIMER0_EN                   (*(volatile uint32_t *)(TIMER0_BASE + 0x000))
#define TIMER0_CTRL                 (*(volatile uint32_t *)(TIMER0_BASE + 0x004))
#define TIMER0_CMD                  (*(volatile uint32_t *)(TIMER0_BASE + 0x008))
#define TIMER0_STATUS               (*(volatile uint32_t *)(TIMER0_BASE + 0x00C))
#define TIMER0_CNT                  (*(volatile uint32_t *)(TIMER0_BASE + 0x018))
#define TIMER0_TOP                  (*(volatile uint32_t *)(TIMER0_BASE + 0x01C))

/* TIMER_EN bits */
#define TIMER_EN_EN                 (1UL << 0)

/* TIMER_CMD bits */
#define TIMER_CMD_START             (1UL << 0)
#define TIMER_CMD_STOP              (1UL << 1)

/* TIMER_CTRL bits */
#define TIMER_CTRL_PRESC_DIV1       (0UL << 24)
#define TIMER_CTRL_PRESC_DIV2       (1UL << 24)
#define TIMER_CTRL_PRESC_DIV4       (2UL << 24)
#define TIMER_CTRL_PRESC_DIV8       (3UL << 24)
#define TIMER_CTRL_PRESC_DIV16      (4UL << 24)
#define TIMER_CTRL_PRESC_DIV32      (5UL << 24)
#define TIMER_CTRL_PRESC_DIV64      (6UL << 24)
#define TIMER_CTRL_PRESC_DIV128     (7UL << 24)
#define TIMER_CTRL_PRESC_DIV256     (8UL << 24)
#define TIMER_CTRL_PRESC_DIV512     (9UL << 24)
#define TIMER_CTRL_PRESC_DIV1024    (10UL << 24)
#define TIMER_CTRL_MODE_UP          (0UL << 0)

/* ============================================================================
 * EUSART0 REGISTERS (Enhanced USART for debug)
 * ============================================================================ */

#define EUSART0_EN                  (*(volatile uint32_t *)(EUSART0_BASE + 0x000))
#define EUSART0_CFG0                (*(volatile uint32_t *)(EUSART0_BASE + 0x004))
#define EUSART0_CFG1                (*(volatile uint32_t *)(EUSART0_BASE + 0x008))
#define EUSART0_CFG2                (*(volatile uint32_t *)(EUSART0_BASE + 0x00C))
#define EUSART0_FRAMECFG            (*(volatile uint32_t *)(EUSART0_BASE + 0x010))
#define EUSART0_IRHFCFG             (*(volatile uint32_t *)(EUSART0_BASE + 0x014))
#define EUSART0_CLKDIV              (*(volatile uint32_t *)(EUSART0_BASE + 0x024))
#define EUSART0_CMD                 (*(volatile uint32_t *)(EUSART0_BASE + 0x028))
#define EUSART0_STATUS              (*(volatile uint32_t *)(EUSART0_BASE + 0x02C))
#define EUSART0_IF                  (*(volatile uint32_t *)(EUSART0_BASE + 0x030))
#define EUSART0_IEN                 (*(volatile uint32_t *)(EUSART0_BASE + 0x034))
#define EUSART0_TXDATA              (*(volatile uint32_t *)(EUSART0_BASE + 0x03C))
#define EUSART0_RXDATA              (*(volatile uint32_t *)(EUSART0_BASE + 0x040))

/* EUSART_EN bits */
#define EUSART_EN_EN                (1UL << 0)

/* EUSART_CMD bits */
#define EUSART_CMD_RXEN             (1UL << 0)
#define EUSART_CMD_RXDIS            (1UL << 1)
#define EUSART_CMD_TXEN             (1UL << 2)
#define EUSART_CMD_TXDIS            (1UL << 3)
#define EUSART_CMD_CLEARTX          (1UL << 6)
#define EUSART_CMD_CLEARRX          (1UL << 7)

/* EUSART_STATUS bits */
#define EUSART_STATUS_RXFL          (1UL << 0)     /* RX FIFO level */
#define EUSART_STATUS_TXFL          (1UL << 8)     /* TX FIFO level */
#define EUSART_STATUS_RXFULL        (1UL << 4)     /* RX FIFO full */
#define EUSART_STATUS_TXIDLE        (1UL << 13)    /* TX idle */
#define EUSART_STATUS_TXC           (1UL << 12)    /* TX complete */

/* EUSART_CFG0 bits */
#define EUSART_CFG0_SYNC            (0UL << 0)     /* Async mode */
#define EUSART_CFG0_LOOPBK          (1UL << 4)     /* Loopback enable */
#define EUSART_CFG0_MVDIS           (1UL << 8)     /* Majority vote disable */
#define EUSART_CFG0_AUTOTX          (1UL << 30)    /* Auto TX enable */

/* EUSART_FRAMECFG bits */
#define EUSART_FRAMECFG_DATABITS_8  (0UL << 0)
#define EUSART_FRAMECFG_STOPBITS_1  (1UL << 12)
#define EUSART_FRAMECFG_PARITY_NONE (0UL << 8)

/* ============================================================================
 * GPIO REGISTERS
 * ============================================================================ */

#define GPIO_PORTA_CTRL             (*(volatile uint32_t *)(GPIO_BASE + 0x000))
#define GPIO_PORTA_MODEL            (*(volatile uint32_t *)(GPIO_BASE + 0x004))
#define GPIO_PORTA_MODEH            (*(volatile uint32_t *)(GPIO_BASE + 0x008))
#define GPIO_PORTA_DOUT             (*(volatile uint32_t *)(GPIO_BASE + 0x00C))
#define GPIO_PORTA_DIN              (*(volatile uint32_t *)(GPIO_BASE + 0x018))

#define GPIO_PORTB_CTRL             (*(volatile uint32_t *)(GPIO_BASE + 0x030))
#define GPIO_PORTB_MODEL            (*(volatile uint32_t *)(GPIO_BASE + 0x034))
#define GPIO_PORTB_MODEH            (*(volatile uint32_t *)(GPIO_BASE + 0x038))

#define GPIO_PORTC_CTRL             (*(volatile uint32_t *)(GPIO_BASE + 0x060))
#define GPIO_PORTC_MODEL            (*(volatile uint32_t *)(GPIO_BASE + 0x064))

#define GPIO_PORTD_CTRL             (*(volatile uint32_t *)(GPIO_BASE + 0x090))
#define GPIO_PORTD_MODEL            (*(volatile uint32_t *)(GPIO_BASE + 0x094))

/* EUSART routing */
#define GPIO_EUSART0_TXROUTE        (*(volatile uint32_t *)(GPIO_BASE + 0x2A0))
#define GPIO_EUSART0_RXROUTE        (*(volatile uint32_t *)(GPIO_BASE + 0x2A4))

/* GPIO mode values */
#define GPIO_MODE_DISABLED          0x0
#define GPIO_MODE_INPUT             0x1
#define GPIO_MODE_INPUTPULL         0x2
#define GPIO_MODE_INPUTPULLFILTER   0x3
#define GPIO_MODE_PUSHPULL          0x4
#define GPIO_MODE_PUSHPULLALT       0x5
#define GPIO_MODE_WIREDAND          0x6
#define GPIO_MODE_WIREDANDPULLUP    0x7

/* ============================================================================
 * DEVICE INFO (Read-Only, Factory Programmed)
 * ============================================================================ */

/* Unique device ID (64-bit EUI-64) */
#define DEVINFO_EUI64L              (*(volatile uint32_t *)(DEVINFO_BASE + 0x0FC))
#define DEVINFO_EUI64H              (*(volatile uint32_t *)(DEVINFO_BASE + 0x100))

/* Part info */
#define DEVINFO_PART                (*(volatile uint32_t *)(DEVINFO_BASE + 0x004))
#define DEVINFO_MEMINFO             (*(volatile uint32_t *)(DEVINFO_BASE + 0x008))

/* ============================================================================
 * NVIC (Nested Vectored Interrupt Controller)
 * ============================================================================ */

#define NVIC_ISER0                  (*(volatile uint32_t *)(NVIC_BASE + 0x000))
#define NVIC_ISER1                  (*(volatile uint32_t *)(NVIC_BASE + 0x004))
#define NVIC_ICER0                  (*(volatile uint32_t *)(NVIC_BASE + 0x080))
#define NVIC_ICER1                  (*(volatile uint32_t *)(NVIC_BASE + 0x084))
#define NVIC_ISPR0                  (*(volatile uint32_t *)(NVIC_BASE + 0x100))
#define NVIC_ICPR0                  (*(volatile uint32_t *)(NVIC_BASE + 0x180))

/* ============================================================================
 * SCB (System Control Block)
 * ============================================================================ */

#define SCB_CPUID                   (*(volatile uint32_t *)(SCB_BASE + 0x000))
#define SCB_ICSR                    (*(volatile uint32_t *)(SCB_BASE + 0x004))
#define SCB_VTOR                    (*(volatile uint32_t *)(SCB_BASE + 0x008))
#define SCB_AIRCR                   (*(volatile uint32_t *)(SCB_BASE + 0x00C))
#define SCB_SCR                     (*(volatile uint32_t *)(SCB_BASE + 0x010))
#define SCB_CCR                     (*(volatile uint32_t *)(SCB_BASE + 0x014))
#define SCB_SHPR1                   (*(volatile uint32_t *)(SCB_BASE + 0x018))
#define SCB_SHPR2                   (*(volatile uint32_t *)(SCB_BASE + 0x01C))
#define SCB_SHPR3                   (*(volatile uint32_t *)(SCB_BASE + 0x020))
#define SCB_SHCSR                   (*(volatile uint32_t *)(SCB_BASE + 0x024))
#define SCB_CFSR                    (*(volatile uint32_t *)(SCB_BASE + 0x028))

/* SCB_AIRCR bits */
#define SCB_AIRCR_VECTKEY           (0x05FA << 16)
#define SCB_AIRCR_SYSRESETREQ       (1UL << 2)

/* FPU (Cortex-M33) */
#define FPU_CPACR                   (*(volatile uint32_t *)0xE000ED88UL)
#define FPU_FPCCR                   (*(volatile uint32_t *)0xE000EF34UL)

/* ============================================================================
 * IRQ NUMBERS (EFR32MG24)
 * ============================================================================ */

typedef enum {
    /* Cortex-M33 exceptions */
    NonMaskableInt_IRQn         = -14,
    HardFault_IRQn              = -13,
    MemoryManagement_IRQn       = -12,
    BusFault_IRQn               = -11,
    UsageFault_IRQn             = -10,
    SecureFault_IRQn            = -9,
    SVCall_IRQn                 = -5,
    DebugMonitor_IRQn           = -4,
    PendSV_IRQn                 = -2,
    SysTick_IRQn                = -1,

    /* EFR32MG24 peripheral interrupts */
    SMU_SECURE_IRQn             = 0,
    SMU_S_PRIVILEGED_IRQn       = 1,
    SMU_NS_PRIVILEGED_IRQn      = 2,
    EMU_IRQn                    = 3,
    TIMER0_IRQn                 = 4,
    TIMER1_IRQn                 = 5,
    TIMER2_IRQn                 = 6,
    TIMER3_IRQn                 = 7,
    TIMER4_IRQn                 = 8,
    USART0_RX_IRQn              = 9,
    USART0_TX_IRQn              = 10,
    EUSART0_RX_IRQn             = 11,
    EUSART0_TX_IRQn             = 12,
    EUSART1_RX_IRQn             = 13,
    EUSART1_TX_IRQn             = 14,
    EUSART2_RX_IRQn             = 15,
    EUSART2_TX_IRQn             = 16,
    ICACHE0_IRQn                = 17,
    BURTC_IRQn                  = 18,
    LETIMER0_IRQn               = 19,
    SYSCFG_IRQn                 = 20,
    MPAHBRAM_IRQn               = 21,
    LDMA_IRQn                   = 22,
    LFXO_IRQn                   = 23,
    LFRCO_IRQn                  = 24,
    ULFRCO_IRQn                 = 25,
    GPIO_ODD_IRQn               = 26,
    GPIO_EVEN_IRQn              = 27,
    I2C0_IRQn                   = 28,
    I2C1_IRQn                   = 29,
    EMUDG_IRQn                  = 30,
    AGC_IRQn                    = 31,
    BUFC_IRQn                   = 32,
    FRC_PRI_IRQn                = 33,
    FRC_IRQn                    = 34,
    MODEM_IRQn                  = 35,
    PROTIMER_IRQn               = 36,
    RAC_RSM_IRQn                = 37,
    RAC_SEQ_IRQn                = 38,
    HOSTMAILBOX_IRQn            = 39,
    SYNTH_IRQn                  = 40,
    ACMP0_IRQn                  = 41,
    ACMP1_IRQn                  = 42,
    WDOG0_IRQn                  = 43,
    WDOG1_IRQn                  = 44,
    HFXO0_IRQn                  = 45,
    HFRCO0_IRQn                 = 46,
    HFRCOEM23_IRQn              = 47,
    CMU_IRQn                    = 48,
    AES_IRQn                    = 49,
    IADC_IRQn                   = 50,
    MSC_IRQn                    = 51,
    DPLL0_IRQn                  = 52,
    EMUEFP_IRQn                 = 53,
    DCDC_IRQn                   = 54,
    VDAC_IRQn                   = 55,
    PCNT0_IRQn                  = 56,
    SW0_IRQn                    = 57,
    SW1_IRQn                    = 58,
    SW2_IRQn                    = 59,
    SW3_IRQn                    = 60,
    KERNEL0_IRQn                = 61,
    KERNEL1_IRQn                = 62,
    M33CTI0_IRQn                = 63,
    M33CTI1_IRQn                = 64,
    FPUEXH_IRQn                 = 65,
    SETAMPERHOST_IRQn           = 66,
    SEMBRX_IRQn                 = 67,
    SEMBTX_IRQn                 = 68,
    SYSRTC_APP_IRQn             = 69,
    SYSRTC_SEQ_IRQn             = 70,
    KEYSCAN_IRQn                = 71,
    RFECA0_IRQn                 = 72,
    RFECA1_IRQn                 = 73,
    VDAC0_IRQn                  = 74,
    VDAC1_IRQn                  = 75,
    AHB2AHB0_IRQn               = 76,
    AHB2AHB1_IRQn               = 77,
} IRQn_Type;

#define EFR32MG24_IRQ_COUNT         78

/* ============================================================================
 * MESSAGE QUEUE CONFIGURATION (for IPC)
 * ============================================================================ */

#define MSG_QUEUE_SIZE              64
#define MSG_MAX_LEN                 64

/* IPC via EUSART (serial wire, for Renode testing)
 * Future: Replace with 802.15.4 radio for mesh
 */
#define EKK_IPC_USE_EUSART          1
#define EKK_IPC_USE_RADIO           0   /* Enable for 802.15.4 mesh */

/* Debug output configuration */
#ifndef EKK_DEBUG
#define EKK_DEBUG                   1
#endif

#ifdef __cplusplus
}
#endif

#endif /* EFR32MG24_CONFIG_H */
