/**
 * Linker script for EK-KOR v2 on Raspberry Pi 3B+ (BCM2837B0)
 *
 * Memory layout:
 * 0x00080000 - kernel8.img load address (GPU loads kernel here)
 * 0x00100000 - Per-core stacks (16KB x 4 = 64KB)
 * 0x00110000 - EK-KOR field region (64KB)
 * 0x00120000 - Per-core message queues (256KB, cache-aligned)
 * 0x00160000 - Heap start
 * 0x3F000000 - Peripheral base (MMIO)
 *
 * Total kernel footprint: ~1MB before heap
 */

ENTRY(_start)

MEMORY
{
    /* Main RAM - 1GB total, but we'll use a small portion */
    RAM (rwx) : ORIGIN = 0x80000, LENGTH = 256M
}

SECTIONS
{
    /* ================================================================
     * Code and read-only data (starts at 0x80000)
     * ================================================================ */

    . = 0x80000;

    .text : {
        KEEP(*(.text.boot))         /* Boot code must be first */
        *(.text .text.*)
    } > RAM

    .rodata : ALIGN(4K) {
        *(.rodata .rodata.*)
    } > RAM

    /* ================================================================
     * Exception vectors (must be 2KB aligned)
     * ================================================================ */

    .vectors : ALIGN(2K) {
        KEEP(*(.vectors))
    } > RAM

    /* ================================================================
     * Read-write data
     * ================================================================ */

    .data : ALIGN(4K) {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    } > RAM

    /* ================================================================
     * BSS (uninitialized data, zeroed at startup)
     * ================================================================ */

    .bss : ALIGN(4K) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    /* ================================================================
     * Per-core stacks (16KB each, 64KB total)
     * ================================================================ */

    . = ALIGN(16K);
    __stack_start = .;
    .stacks (NOLOAD) : {
        . += 64K;                   /* 4 cores x 16KB each */
    } > RAM
    __stack_end = .;

    /* ================================================================
     * EK-KOR Field Region (64KB, cache-line aligned)
     * Shared between all cores for coordination fields
     * ================================================================ */

    . = ALIGN(64);
    __field_region = .;
    .field_region (NOLOAD) : {
        . += 64K;
    } > RAM
    __field_region_end = .;

    /* ================================================================
     * Per-core Message Queues (256KB total, cache-line aligned)
     * Each core has its own queue to avoid contention
     * ================================================================ */

    . = ALIGN(64);
    __msg_queues = .;
    .msg_queues (NOLOAD) : {
        . += 256K;                  /* 4 cores x 64KB each */
    } > RAM
    __msg_queues_end = .;

    /* ================================================================
     * Heap (remaining memory)
     * ================================================================ */

    . = ALIGN(4K);
    __heap_start = .;
    .heap (NOLOAD) : {
        . += 4M;                    /* 4MB heap */
    } > RAM
    __heap_end = .;

    /* ================================================================
     * Discard sections we don't need
     * ================================================================ */

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.ARM.*)
    }
}

/* ================================================================
 * Symbol exports for C code
 * ================================================================ */

PROVIDE(__kernel_start = 0x80000);
PROVIDE(__kernel_end = __heap_start);

/* Stack symbols (used in boot.S) */
PROVIDE(__stack_size_per_core = 16K);
PROVIDE(__num_cores = 4);

/* Field region size for HAL */
PROVIDE(__field_region_size = 64K);

/* Message queue size for HAL */
PROVIDE(__msg_queue_size = 64K);
