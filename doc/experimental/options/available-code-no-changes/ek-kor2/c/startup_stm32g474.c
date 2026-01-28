/**
 * @file startup_stm32g474.c
 * @brief Startup code for STM32G474 (Cortex-M4)
 *
 * Provides:
 * - Vector table with all IRQ handlers
 * - Reset_Handler: .data/.bss init, SystemInit, main()
 * - Default handlers (infinite loop)
 *
 * Copyright (c) 2026 Elektrokombinacija
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

/* Linker symbols */
extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

/* Main function */
extern int main(void);

/* System initialization (optional, can be empty) */
void SystemInit(void) __attribute__((weak));

/* Default handler for unused interrupts */
void Default_Handler(void) __attribute__((weak));

/* Cortex-M4 core handlers */
void Reset_Handler(void);
void NMI_Handler(void) __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void) __attribute__((weak));
void MemManage_Handler(void) __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void) __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));

/* STM32G4 peripheral interrupt handlers */
void WWDG_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void PVD_PVM_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RTC_TAMP_LSECSS_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RTC_WKUP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FLASH_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RCC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ADC1_2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USB_HP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USB_LP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FDCAN1_IT0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FDCAN1_IT1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI9_5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM1_BRK_TIM15_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM1_UP_TIM16_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM1_TRG_COM_TIM17_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM1_CC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C1_EV_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C1_ER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C2_EV_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C2_ER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USART3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void EXTI15_10_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RTC_Alarm_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void USBWakeUp_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM8_BRK_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM8_UP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM8_TRG_COM_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM8_CC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ADC3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FMC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void LPTIM1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void UART4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void UART5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM6_DAC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM7_DAC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel2_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ADC4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void ADC5_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void UCPD1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void COMP1_2_3_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void COMP4_5_6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void COMP7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HRTIM1_Master_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HRTIM1_TIMA_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HRTIM1_TIMB_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HRTIM1_TIMC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HRTIM1_TIMD_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HRTIM1_TIME_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HRTIM1_FLT_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void HRTIM1_TIMF_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CRS_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SAI1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM20_BRK_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM20_UP_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM20_TRG_COM_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void TIM20_CC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FPU_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C4_EV_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C4_ER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void SPI4_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FDCAN2_IT0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FDCAN2_IT1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FDCAN3_IT0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FDCAN3_IT1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void RNG_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void LPUART1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C3_EV_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void I2C3_ER_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMAMUX_OVR_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void QUADSPI_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA1_Channel8_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel6_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel7_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void DMA2_Channel8_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CORDIC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void FMAC_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

/* Vector table entry macro - casts handler to uint32_t address */
#define VECTOR(handler) ((uint32_t)(handler))

/* Vector table - placed at start of Flash
 * Uses uint32_t array to avoid ISO C warnings about mixing
 * void* and function pointers in the same array.
 */
__attribute__((section(".isr_vector")))
const uint32_t g_pfnVectors[] = {
    (uint32_t)&_estack,             /* Initial stack pointer (in CCM) */
    VECTOR(Reset_Handler),          /* Reset handler */
    VECTOR(NMI_Handler),            /* -14: NMI */
    VECTOR(HardFault_Handler),      /* -13: Hard fault */
    VECTOR(MemManage_Handler),      /* -12: Memory management fault */
    VECTOR(BusFault_Handler),       /* -11: Bus fault */
    VECTOR(UsageFault_Handler),     /* -10: Usage fault */
    0, 0, 0, 0,                     /* Reserved */
    VECTOR(SVC_Handler),            /* -5: SVCall */
    VECTOR(DebugMon_Handler),       /* -4: Debug monitor */
    0,                              /* Reserved */
    VECTOR(PendSV_Handler),         /* -2: PendSV */
    VECTOR(SysTick_Handler),        /* -1: SysTick */

    /* STM32G4 peripheral interrupts (IRQ 0-101) */
    VECTOR(WWDG_IRQHandler),                /* 0 */
    VECTOR(PVD_PVM_IRQHandler),             /* 1 */
    VECTOR(RTC_TAMP_LSECSS_IRQHandler),     /* 2 */
    VECTOR(RTC_WKUP_IRQHandler),            /* 3 */
    VECTOR(FLASH_IRQHandler),               /* 4 */
    VECTOR(RCC_IRQHandler),                 /* 5 */
    VECTOR(EXTI0_IRQHandler),               /* 6 */
    VECTOR(EXTI1_IRQHandler),               /* 7 */
    VECTOR(EXTI2_IRQHandler),               /* 8 */
    VECTOR(EXTI3_IRQHandler),               /* 9 */
    VECTOR(EXTI4_IRQHandler),               /* 10 */
    VECTOR(DMA1_Channel1_IRQHandler),       /* 11 */
    VECTOR(DMA1_Channel2_IRQHandler),       /* 12 */
    VECTOR(DMA1_Channel3_IRQHandler),       /* 13 */
    VECTOR(DMA1_Channel4_IRQHandler),       /* 14 */
    VECTOR(DMA1_Channel5_IRQHandler),       /* 15 */
    VECTOR(DMA1_Channel6_IRQHandler),       /* 16 */
    VECTOR(DMA1_Channel7_IRQHandler),       /* 17 */
    VECTOR(ADC1_2_IRQHandler),              /* 18 */
    VECTOR(USB_HP_IRQHandler),              /* 19 */
    VECTOR(USB_LP_IRQHandler),              /* 20 */
    VECTOR(FDCAN1_IT0_IRQHandler),          /* 21 */
    VECTOR(FDCAN1_IT1_IRQHandler),          /* 22 */
    VECTOR(EXTI9_5_IRQHandler),             /* 23 */
    VECTOR(TIM1_BRK_TIM15_IRQHandler),      /* 24 */
    VECTOR(TIM1_UP_TIM16_IRQHandler),       /* 25 */
    VECTOR(TIM1_TRG_COM_TIM17_IRQHandler),  /* 26 */
    VECTOR(TIM1_CC_IRQHandler),             /* 27 */
    VECTOR(TIM2_IRQHandler),                /* 28 */
    VECTOR(TIM3_IRQHandler),                /* 29 */
    VECTOR(TIM4_IRQHandler),                /* 30 */
    VECTOR(I2C1_EV_IRQHandler),             /* 31 */
    VECTOR(I2C1_ER_IRQHandler),             /* 32 */
    VECTOR(I2C2_EV_IRQHandler),             /* 33 */
    VECTOR(I2C2_ER_IRQHandler),             /* 34 */
    VECTOR(SPI1_IRQHandler),                /* 35 */
    VECTOR(SPI2_IRQHandler),                /* 36 */
    VECTOR(USART1_IRQHandler),              /* 37 */
    VECTOR(USART2_IRQHandler),              /* 38 */
    VECTOR(USART3_IRQHandler),              /* 39 */
    VECTOR(EXTI15_10_IRQHandler),           /* 40 */
    VECTOR(RTC_Alarm_IRQHandler),           /* 41 */
    VECTOR(USBWakeUp_IRQHandler),           /* 42 */
    VECTOR(TIM8_BRK_IRQHandler),            /* 43 */
    VECTOR(TIM8_UP_IRQHandler),             /* 44 */
    VECTOR(TIM8_TRG_COM_IRQHandler),        /* 45 */
    VECTOR(TIM8_CC_IRQHandler),             /* 46 */
    VECTOR(ADC3_IRQHandler),                /* 47 */
    VECTOR(FMC_IRQHandler),                 /* 48 */
    VECTOR(LPTIM1_IRQHandler),              /* 49 */
    VECTOR(TIM5_IRQHandler),                /* 50 */
    VECTOR(SPI3_IRQHandler),                /* 51 */
    VECTOR(UART4_IRQHandler),               /* 52 */
    VECTOR(UART5_IRQHandler),               /* 53 */
    VECTOR(TIM6_DAC_IRQHandler),            /* 54 */
    VECTOR(TIM7_DAC_IRQHandler),            /* 55 */
    VECTOR(DMA2_Channel1_IRQHandler),       /* 56 */
    VECTOR(DMA2_Channel2_IRQHandler),       /* 57 */
    VECTOR(DMA2_Channel3_IRQHandler),       /* 58 */
    VECTOR(DMA2_Channel4_IRQHandler),       /* 59 */
    VECTOR(DMA2_Channel5_IRQHandler),       /* 60 */
    VECTOR(ADC4_IRQHandler),                /* 61 */
    VECTOR(ADC5_IRQHandler),                /* 62 */
    VECTOR(UCPD1_IRQHandler),               /* 63 */
    VECTOR(COMP1_2_3_IRQHandler),           /* 64 */
    VECTOR(COMP4_5_6_IRQHandler),           /* 65 */
    VECTOR(COMP7_IRQHandler),               /* 66 */
    VECTOR(HRTIM1_Master_IRQHandler),       /* 67 */
    VECTOR(HRTIM1_TIMA_IRQHandler),         /* 68 */
    VECTOR(HRTIM1_TIMB_IRQHandler),         /* 69 */
    VECTOR(HRTIM1_TIMC_IRQHandler),         /* 70 */
    VECTOR(HRTIM1_TIMD_IRQHandler),         /* 71 */
    VECTOR(HRTIM1_TIME_IRQHandler),         /* 72 */
    VECTOR(HRTIM1_FLT_IRQHandler),          /* 73 */
    VECTOR(HRTIM1_TIMF_IRQHandler),         /* 74 */
    VECTOR(CRS_IRQHandler),                 /* 75 */
    VECTOR(SAI1_IRQHandler),                /* 76 */
    VECTOR(TIM20_BRK_IRQHandler),           /* 77 */
    VECTOR(TIM20_UP_IRQHandler),            /* 78 */
    VECTOR(TIM20_TRG_COM_IRQHandler),       /* 79 */
    VECTOR(TIM20_CC_IRQHandler),            /* 80 */
    VECTOR(FPU_IRQHandler),                 /* 81 */
    VECTOR(I2C4_EV_IRQHandler),             /* 82 */
    VECTOR(I2C4_ER_IRQHandler),             /* 83 */
    VECTOR(SPI4_IRQHandler),                /* 84 */
    0,                                      /* 85: Reserved */
    VECTOR(FDCAN2_IT0_IRQHandler),          /* 86 */
    VECTOR(FDCAN2_IT1_IRQHandler),          /* 87 */
    VECTOR(FDCAN3_IT0_IRQHandler),          /* 88 */
    VECTOR(FDCAN3_IT1_IRQHandler),          /* 89 */
    VECTOR(RNG_IRQHandler),                 /* 90 */
    VECTOR(LPUART1_IRQHandler),             /* 91 */
    VECTOR(I2C3_EV_IRQHandler),             /* 92 */
    VECTOR(I2C3_ER_IRQHandler),             /* 93 */
    VECTOR(DMAMUX_OVR_IRQHandler),          /* 94 */
    0,                                      /* 95: Reserved */
    VECTOR(DMA1_Channel8_IRQHandler),       /* 96 */
    VECTOR(DMA2_Channel6_IRQHandler),       /* 97 */
    VECTOR(DMA2_Channel7_IRQHandler),       /* 98 */
    VECTOR(DMA2_Channel8_IRQHandler),       /* 99 */
    VECTOR(CORDIC_IRQHandler),              /* 100 */
    VECTOR(FMAC_IRQHandler),                /* 101 */
};

/**
 * @brief Reset handler - entry point after reset
 *
 * 1. Copy .data from Flash to SRAM
 * 2. Zero .bss section
 * 3. Call SystemInit (clock setup)
 * 4. Call main()
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

    /* Enable FPU (Cortex-M4 with FPU) */
    /* Set CP10 and CP11 to full access */
    *((volatile uint32_t *)0xE000ED88) |= (0xF << 20);

    /* Data/instruction sync barriers */
    __asm__ volatile("dsb");
    __asm__ volatile("isb");

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
 */
void SystemInit(void)
{
    /* Default: no clock configuration, use HSI @ 16 MHz */
    /* Override this function to configure 170 MHz PLL */
}

/* ============================================================================
 * NEWLIB SYSCALLS (for printf, malloc, etc.)
 * ============================================================================ */

/* Heap boundaries from linker script */
extern uint32_t _heap_start;
extern uint32_t _heap_end;

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
 * @brief _write - Write to file descriptor (for printf via UART)
 */
int _write(int fd, char *ptr, int len)
{
    (void)fd;  /* Unused - always write to UART */

    for (int i = 0; i < len; i++) {
        /* Wait for TX ready and send character */
        /* This is implemented in HAL - use simple UART write here */
        volatile uint32_t *USART2_ISR = (volatile uint32_t *)0x4000441C;
        volatile uint32_t *USART2_TDR = (volatile uint32_t *)0x40004428;

        while (!(*USART2_ISR & (1 << 7)));  /* Wait for TXE */
        *USART2_TDR = ptr[i];
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
