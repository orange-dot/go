/**
 * @file efr32mg24_startup.c
 * @brief Startup code for Silicon Labs EFR32MG24 (Cortex-M33)
 *
 * Provides:
 * - Vector table with all IRQ handlers
 * - Reset_Handler: .data/.bss init, SystemInit, main()
 * - Default handlers (infinite loop)
 * - TrustZone-aware startup (Non-Secure by default)
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 */

#include <stdint.h>

/* Only compile for EFR32MG24 target */
#if defined(EFR32MG24) || defined(EKK_PLATFORM_EFR32MG24)

/* ============================================================================
 * LINKER SYMBOLS
 * ============================================================================ */

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _heap_start;
extern uint32_t _heap_end;

/* Main function */
extern int main(void);

/* System initialization (optional, can be empty) */
void SystemInit(void) __attribute__((weak));

/* ============================================================================
 * EXCEPTION HANDLERS
 * ============================================================================ */

/* Default handler for unused interrupts */
void Default_Handler(void) __attribute__((weak));

/* Cortex-M33 core handlers */
void Reset_Handler(void);
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SecureFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

/* EFR32MG24 peripheral interrupt handlers */
void SMU_SECURE_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SMU_S_PRIVILEGED_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SMU_NS_PRIVILEGED_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EMU_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIMER0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIMER1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIMER2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIMER3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIMER4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART0_RX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART0_TX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EUSART0_RX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EUSART0_TX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EUSART1_RX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EUSART1_TX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EUSART2_RX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EUSART2_TX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ICACHE0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void BURTC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void LETIMER0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SYSCFG_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void MPAHBRAM_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void LDMA_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void LFXO_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void LFRCO_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ULFRCO_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void GPIO_ODD_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void GPIO_EVEN_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EMUDG_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void AGC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void BUFC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FRC_PRI_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FRC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void MODEM_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void PROTIMER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RAC_RSM_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RAC_SEQ_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HOSTMAILBOX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SYNTH_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ACMP0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ACMP1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void WDOG0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void WDOG1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HFXO0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HFRCO0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HFRCOEM23_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CMU_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void AES_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void IADC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void MSC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DPLL0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EMUEFP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DCDC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void VDAC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void PCNT0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SW0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SW1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SW2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SW3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void KERNEL0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void KERNEL1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void M33CTI0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void M33CTI1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FPUEXH_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SETAMPERHOST_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SEMBRX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SEMBTX_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SYSRTC_APP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SYSRTC_SEQ_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void KEYSCAN_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RFECA0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RFECA1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void VDAC0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void VDAC1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void AHB2AHB0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void AHB2AHB1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

/* ============================================================================
 * VECTOR TABLE
 * ============================================================================ */

/* Vector table entry macro */
#define VECTOR(handler) ((uint32_t)(handler))

/**
 * @brief Vector table - placed at start of Flash
 *
 * EFR32MG24 has 78 peripheral interrupts.
 * Uses uint32_t array for ISO C compatibility.
 */
__attribute__((section(".isr_vector")))
const uint32_t g_pfnVectors[] = {
    /* Initial stack pointer */
    (uint32_t)&_estack,

    /* Cortex-M33 Exception Handlers */
    VECTOR(Reset_Handler),          /* Reset */
    VECTOR(NMI_Handler),            /* -14: NMI */
    VECTOR(HardFault_Handler),      /* -13: Hard fault */
    VECTOR(MemManage_Handler),      /* -12: Memory management fault */
    VECTOR(BusFault_Handler),       /* -11: Bus fault */
    VECTOR(UsageFault_Handler),     /* -10: Usage fault */
    VECTOR(SecureFault_Handler),    /* -9: Secure fault (Cortex-M33 TrustZone) */
    0, 0, 0,                        /* Reserved */
    VECTOR(SVC_Handler),            /* -5: SVCall */
    VECTOR(DebugMon_Handler),       /* -4: Debug monitor */
    0,                              /* Reserved */
    VECTOR(PendSV_Handler),         /* -2: PendSV */
    VECTOR(SysTick_Handler),        /* -1: SysTick */

    /* EFR32MG24 Peripheral Interrupts (IRQ 0-77) */
    VECTOR(SMU_SECURE_IRQHandler),          /* 0 */
    VECTOR(SMU_S_PRIVILEGED_IRQHandler),    /* 1 */
    VECTOR(SMU_NS_PRIVILEGED_IRQHandler),   /* 2 */
    VECTOR(EMU_IRQHandler),                 /* 3 */
    VECTOR(TIMER0_IRQHandler),              /* 4 */
    VECTOR(TIMER1_IRQHandler),              /* 5 */
    VECTOR(TIMER2_IRQHandler),              /* 6 */
    VECTOR(TIMER3_IRQHandler),              /* 7 */
    VECTOR(TIMER4_IRQHandler),              /* 8 */
    VECTOR(USART0_RX_IRQHandler),           /* 9 */
    VECTOR(USART0_TX_IRQHandler),           /* 10 */
    VECTOR(EUSART0_RX_IRQHandler),          /* 11 */
    VECTOR(EUSART0_TX_IRQHandler),          /* 12 */
    VECTOR(EUSART1_RX_IRQHandler),          /* 13 */
    VECTOR(EUSART1_TX_IRQHandler),          /* 14 */
    VECTOR(EUSART2_RX_IRQHandler),          /* 15 */
    VECTOR(EUSART2_TX_IRQHandler),          /* 16 */
    VECTOR(ICACHE0_IRQHandler),             /* 17 */
    VECTOR(BURTC_IRQHandler),               /* 18 */
    VECTOR(LETIMER0_IRQHandler),            /* 19 */
    VECTOR(SYSCFG_IRQHandler),              /* 20 */
    VECTOR(MPAHBRAM_IRQHandler),            /* 21 */
    VECTOR(LDMA_IRQHandler),                /* 22 */
    VECTOR(LFXO_IRQHandler),                /* 23 */
    VECTOR(LFRCO_IRQHandler),               /* 24 */
    VECTOR(ULFRCO_IRQHandler),              /* 25 */
    VECTOR(GPIO_ODD_IRQHandler),            /* 26 */
    VECTOR(GPIO_EVEN_IRQHandler),           /* 27 */
    VECTOR(I2C0_IRQHandler),                /* 28 */
    VECTOR(I2C1_IRQHandler),                /* 29 */
    VECTOR(EMUDG_IRQHandler),               /* 30 */
    VECTOR(AGC_IRQHandler),                 /* 31 */
    VECTOR(BUFC_IRQHandler),                /* 32 */
    VECTOR(FRC_PRI_IRQHandler),             /* 33 */
    VECTOR(FRC_IRQHandler),                 /* 34 */
    VECTOR(MODEM_IRQHandler),               /* 35 */
    VECTOR(PROTIMER_IRQHandler),            /* 36 */
    VECTOR(RAC_RSM_IRQHandler),             /* 37 */
    VECTOR(RAC_SEQ_IRQHandler),             /* 38 */
    VECTOR(HOSTMAILBOX_IRQHandler),         /* 39 */
    VECTOR(SYNTH_IRQHandler),               /* 40 */
    VECTOR(ACMP0_IRQHandler),               /* 41 */
    VECTOR(ACMP1_IRQHandler),               /* 42 */
    VECTOR(WDOG0_IRQHandler),               /* 43 */
    VECTOR(WDOG1_IRQHandler),               /* 44 */
    VECTOR(HFXO0_IRQHandler),               /* 45 */
    VECTOR(HFRCO0_IRQHandler),              /* 46 */
    VECTOR(HFRCOEM23_IRQHandler),           /* 47 */
    VECTOR(CMU_IRQHandler),                 /* 48 */
    VECTOR(AES_IRQHandler),                 /* 49 */
    VECTOR(IADC_IRQHandler),                /* 50 */
    VECTOR(MSC_IRQHandler),                 /* 51 */
    VECTOR(DPLL0_IRQHandler),               /* 52 */
    VECTOR(EMUEFP_IRQHandler),              /* 53 */
    VECTOR(DCDC_IRQHandler),                /* 54 */
    VECTOR(VDAC_IRQHandler),                /* 55 */
    VECTOR(PCNT0_IRQHandler),               /* 56 */
    VECTOR(SW0_IRQHandler),                 /* 57 */
    VECTOR(SW1_IRQHandler),                 /* 58 */
    VECTOR(SW2_IRQHandler),                 /* 59 */
    VECTOR(SW3_IRQHandler),                 /* 60 */
    VECTOR(KERNEL0_IRQHandler),             /* 61 */
    VECTOR(KERNEL1_IRQHandler),             /* 62 */
    VECTOR(M33CTI0_IRQHandler),             /* 63 */
    VECTOR(M33CTI1_IRQHandler),             /* 64 */
    VECTOR(FPUEXH_IRQHandler),              /* 65 */
    VECTOR(SETAMPERHOST_IRQHandler),        /* 66 */
    VECTOR(SEMBRX_IRQHandler),              /* 67 */
    VECTOR(SEMBTX_IRQHandler),              /* 68 */
    VECTOR(SYSRTC_APP_IRQHandler),          /* 69 */
    VECTOR(SYSRTC_SEQ_IRQHandler),          /* 70 */
    VECTOR(KEYSCAN_IRQHandler),             /* 71 */
    VECTOR(RFECA0_IRQHandler),              /* 72 */
    VECTOR(RFECA1_IRQHandler),              /* 73 */
    VECTOR(VDAC0_IRQHandler),               /* 74 */
    VECTOR(VDAC1_IRQHandler),               /* 75 */
    VECTOR(AHB2AHB0_IRQHandler),            /* 76 */
    VECTOR(AHB2AHB1_IRQHandler),            /* 77 */
};

/* ============================================================================
 * RESET HANDLER
 * ============================================================================ */

/**
 * @brief Reset handler - entry point after reset
 *
 * 1. Copy .data from Flash to SRAM
 * 2. Zero .bss section
 * 3. Enable FPU (Cortex-M33 with FPU)
 * 4. Call SystemInit (clock setup)
 * 5. Call main()
 */
void Reset_Handler(void)
{
    uint32_t *src, *dst;

    /* Copy .data section from Flash to SRAM */
    src = &_sidata;
    dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    /* Zero .bss section */
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    /* Enable FPU (Cortex-M33 with FPU)
     * Set CP10 and CP11 to full access (non-secure)
     * CPACR address: 0xE000ED88
     */
    *((volatile uint32_t *)0xE000ED88UL) |= (0xF << 20);

    /* Data/instruction sync barriers */
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("isb" ::: "memory");

    /* Call system init if provided */
    if (SystemInit) {
        SystemInit();
    }

    /* Call main */
    main();

    /* If main returns, loop forever */
    while (1) {
        __asm__ volatile("wfi");
    }
}

/* ============================================================================
 * DEFAULT HANDLERS
 * ============================================================================ */

/**
 * @brief Default handler for unused interrupts
 */
void Default_Handler(void)
{
    while (1) {
        __asm__ volatile("bkpt #0");
    }
}

/**
 * @brief Hard fault handler with register dump
 *
 * For Cortex-M33, can also check for secure fault via SCB->CFSR
 */
void HardFault_Handler(void)
{
    /* Read stacked registers for debugging */
    __asm__ volatile(
        "tst lr, #4      \n"
        "ite eq          \n"
        "mrseq r0, msp   \n"
        "mrsne r0, psp   \n"
        "bkpt #0         \n"
    );

    while (1);
}

/**
 * @brief Weak SystemInit - can be overridden
 *
 * Default: Uses HFRCO @ 19 MHz (reset default)
 * Override to configure HFXO @ 78 MHz for full speed.
 */
void SystemInit(void)
{
    /* Default: no clock configuration, use HFRCO @ 19 MHz
     * Override this function to configure 78 MHz HFXO
     *
     * Full clock configuration would include:
     * 1. Enable HFXO
     * 2. Wait for HFXO to stabilize
     * 3. Switch SYSCLK to HFXO
     * 4. Configure flash wait states
     *
     * For Renode: HFRCO is sufficient for testing
     */
}

/* ============================================================================
 * NEWLIB SYSCALLS (for printf, malloc, etc.)
 * ============================================================================ */

/**
 * @brief _sbrk - Extend heap for malloc
 *
 * Required by newlib for dynamic memory allocation.
 */
void *_sbrk(int incr)
{
    static uint8_t *heap_ptr = 0;
    uint8_t *prev_heap_ptr;

    if (heap_ptr == 0) {
        heap_ptr = (uint8_t *)&_heap_start;
    }

    prev_heap_ptr = heap_ptr;

    if ((heap_ptr + incr) > (uint8_t *)&_heap_end) {
        /* Out of heap memory */
        return (void *)-1;
    }

    heap_ptr += incr;
    return (void *)prev_heap_ptr;
}

/**
 * @brief _write - Write to file descriptor (for printf via EUSART)
 */
int _write(int fd, char *ptr, int len)
{
    (void)fd;  /* Unused - always write to EUSART0 */

    /* EUSART0 registers */
    volatile uint32_t *EUSART0_STATUS = (volatile uint32_t *)0x400A002CUL;
    volatile uint32_t *EUSART0_TXDATA = (volatile uint32_t *)0x400A003CUL;

    for (int i = 0; i < len; i++) {
        /* Wait for TX FIFO not full */
        while (*EUSART0_STATUS & (1UL << 8));  /* TXFL bit */
        *EUSART0_TXDATA = ptr[i];
    }

    return len;
}

/**
 * @brief _read - Read from file descriptor (stub)
 */
int _read(int fd, char *ptr, int len)
{
    (void)fd;
    (void)ptr;
    (void)len;
    return 0;
}

/**
 * @brief _close - Close file descriptor (stub)
 */
int _close(int fd)
{
    (void)fd;
    return -1;
}

/**
 * @brief _lseek - Seek in file (stub)
 */
int _lseek(int fd, int ptr, int dir)
{
    (void)fd;
    (void)ptr;
    (void)dir;
    return 0;
}

/**
 * @brief _fstat - Get file status (stub)
 */
int _fstat(int fd, void *st)
{
    (void)fd;
    (void)st;
    return 0;
}

/**
 * @brief _isatty - Check if file is a terminal (stub)
 */
int _isatty(int fd)
{
    (void)fd;
    return 1;  /* Pretend everything is a terminal */
}

/**
 * @brief _exit - Exit program (stub - just halt)
 */
void _exit(int status)
{
    (void)status;
    while (1) {
        __asm__ volatile("wfi");
    }
}

/**
 * @brief _kill - Send signal to process (stub)
 */
int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    return -1;
}

/**
 * @brief _getpid - Get process ID (stub)
 */
int _getpid(void)
{
    return 1;
}

#endif /* EFR32MG24 || EKK_PLATFORM_EFR32MG24 */
