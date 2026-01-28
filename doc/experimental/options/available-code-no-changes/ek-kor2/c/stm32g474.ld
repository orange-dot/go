/*
 * STM32G474 Linker Script for EK-KOR v2
 *
 * Memory Map:
 *   Flash:  512KB @ 0x08000000 (code, const data)
 *   CCM:     32KB @ 0x10000000 (kernel stack, critical data)
 *   SRAM1:   80KB @ 0x20000000 (heap, task stacks, .data, .bss)
 *   SRAM2:   16KB @ 0x20014000 (field region, CAN buffers)
 *
 * Copyright (c) 2026 Elektrokombinacija
 * SPDX-License-Identifier: MIT
 */

/* Entry point */
ENTRY(Reset_Handler)

/* Memory regions */
MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    CCM    (rwx) : ORIGIN = 0x10000000, LENGTH = 32K
    SRAM1  (rwx) : ORIGIN = 0x20000000, LENGTH = 80K
    SRAM2  (rwx) : ORIGIN = 0x20014000, LENGTH = 16K
}

/* Stack and heap sizes */
_Min_Heap_Size  = 0x2000;  /* 8KB heap */
_Min_Stack_Size = 0x1000;  /* 4KB main stack (in CCM) */

SECTIONS
{
    /* Vector table and code in Flash */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    /* Code */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)

        KEEP(*(.init))
        KEEP(*(.fini))

        . = ALIGN(4);
        _etext = .;
    } >FLASH

    /* Read-only data */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    /* ARM exception handling */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } >FLASH

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    /* Used by startup to initialize .data */
    _sidata = LOADADDR(.data);

    /* Initialized data (copied from Flash to SRAM1) */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >SRAM1 AT> FLASH

    /* Uninitialized data (zeroed by startup) */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } >SRAM1

    /* User heap (in SRAM1, after .bss) */
    ._user_heap :
    {
        . = ALIGN(8);
        PROVIDE(_heap_start = .);
        . = . + _Min_Heap_Size;
        PROVIDE(_heap_end = .);
        . = ALIGN(8);
    } >SRAM1

    /* Main stack (in CCM for fast access) */
    ._main_stack :
    {
        . = ALIGN(8);
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
        _estack = .;
    } >CCM

    /* SRAM1 section for explicit placement */
    .sram1 (NOLOAD) :
    {
        . = ALIGN(4);
        *(.sram1)
        *(.sram1*)
        . = ALIGN(4);
    } >SRAM1

    /* Field region and shared data in SRAM2 */
    .sram2 (NOLOAD) :
    {
        . = ALIGN(4);
        _sram2_start = .;
        *(.sram2)
        *(.sram2*)
        *(.field_region)
        . = ALIGN(4);
        _sram2_end = .;
    } >SRAM2

    /* CCM section for critical data */
    .ccm (NOLOAD) :
    {
        . = ALIGN(4);
        *(.ccm)
        *(.ccm*)
        . = ALIGN(4);
    } >CCM

    /* Remove debug info from release builds */
    /DISCARD/ :
    {
        libc.a (*)
        libm.a (*)
        libgcc.a (*)
    }

    /* Provide symbols for startup code */
    PROVIDE(_flash_start = ORIGIN(FLASH));
    PROVIDE(_flash_end = ORIGIN(FLASH) + LENGTH(FLASH));
    PROVIDE(_sram1_start = ORIGIN(SRAM1));
    PROVIDE(_sram1_end = ORIGIN(SRAM1) + LENGTH(SRAM1));
    PROVIDE(_sram2_start = ORIGIN(SRAM2));
    PROVIDE(_sram2_end = ORIGIN(SRAM2) + LENGTH(SRAM2));
    PROVIDE(_ccm_start = ORIGIN(CCM));
    PROVIDE(_ccm_end = ORIGIN(CCM) + LENGTH(CCM));
}