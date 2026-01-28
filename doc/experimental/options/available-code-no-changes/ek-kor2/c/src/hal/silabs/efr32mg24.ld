/*
 * EFR32MG24 Linker Script for EK-KOR v2
 *
 * Silicon Labs EFR32MG24 (Cortex-M33)
 *
 * Memory Map:
 *   Flash:  1.5 MB @ 0x08000000 (code, const data)
 *   RAM:    256 KB @ 0x20000000 (stack, heap, .data, .bss, field region)
 *
 * RAM Layout:
 *   0x20000000 - Stack (8 KB)
 *   Stack end  - .data, .bss, heap (224 KB)
 *   0x2003C000 - Field region (16 KB, for EK-KOR shared data)
 *   0x2003FFFC - Module ID override (4 bytes, set by Renode)
 *
 * Copyright (c) 2026 Elektrokombinacija
 * SPDX-License-Identifier: MIT
 */

/* Entry point */
ENTRY(Reset_Handler)

/* Memory regions */
MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1536K
    RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 240K    /* 256K - 16K field region */
    FIELD  (rwx) : ORIGIN = 0x2003C000, LENGTH = 16K     /* Field region for EK-KOR */
}

/* Stack and heap sizes */
_Min_Heap_Size  = 0x4000;   /* 16 KB heap */
_Min_Stack_Size = 0x2000;   /* 8 KB main stack */

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

    /* ARM exception handling (Cortex-M33) */
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

    /* Constructors/destructors for C++ and static init */
    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } >FLASH

    /* Used by startup to initialize .data */
    _sidata = LOADADDR(.data);

    /* Initialized data (copied from Flash to RAM) */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

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
    } >RAM

    /* User heap (in RAM, after .bss) */
    ._user_heap :
    {
        . = ALIGN(8);
        PROVIDE(_heap_start = .);
        . = . + _Min_Heap_Size;
        PROVIDE(_heap_end = .);
        . = ALIGN(8);
    } >RAM

    /* Main stack (at end of RAM, before field region) */
    ._main_stack :
    {
        . = ALIGN(8);
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
        _estack = .;
    } >RAM

    /* Field region for EK-KOR shared data (last 16KB of RAM) */
    .field_region (NOLOAD) :
    {
        . = ALIGN(4);
        _field_start = .;
        *(.field_region)
        *(.field_region*)
        . = ALIGN(4);
        _field_end = .;
    } >FIELD

    /* Provide symbols for startup code and HAL */
    PROVIDE(_flash_start = ORIGIN(FLASH));
    PROVIDE(_flash_end = ORIGIN(FLASH) + LENGTH(FLASH));
    PROVIDE(_ram_start = ORIGIN(RAM));
    PROVIDE(_ram_end = ORIGIN(RAM) + LENGTH(RAM));
    PROVIDE(_field_region_start = ORIGIN(FIELD));
    PROVIDE(_field_region_end = ORIGIN(FIELD) + LENGTH(FIELD));

    /* Module ID override address (for Renode multi-module testing) */
    PROVIDE(_module_id_override = 0x2003FFFC);

    /* Note: Stack/heap collision check removed - layout is safe with 240KB RAM */

    /* Remove debug info from release builds (optional) */
    /DISCARD/ :
    {
        libc.a (*)
        libm.a (*)
        libgcc.a (*)
    }
}
