/**
 * @file boot.S
 * @brief AArch64 Boot Stub for Raspberry Pi 3B+ (BCM2837B0)
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Boot sequence:
 * 1. GPU loads kernel8.img at 0x80000
 * 2. Core 0 starts here, cores 1-3 spin waiting
 * 3. Drop from EL2 to EL1
 * 4. Set up stack, clear BSS
 * 5. Jump to kernel_main()
 */

.section ".text.boot"
.global _start

/* Entry point - must be at 0x80000 */
_start:
    /* Read CPU ID from MPIDR_EL1 */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF           /* Core ID: 0-3 */

    /* Core 0 continues, cores 1-3 wait for spin address */
    cbz     x0, primary_core_init

/* -----------------------------------------------------------------
 * Secondary cores (1-3): Wait for spin table entry
 * ----------------------------------------------------------------- */
secondary_core_wait:
    /* Calculate spin table entry address */
    adr     x1, spin_table
    add     x1, x1, x0, lsl #3      /* spin_table[core_id] (8 bytes each) */

.spin_loop:
    /* Wait for event (low power) */
    wfe

    /* Check if entry point is set */
    ldr     x2, [x1]
    cbz     x2, .spin_loop          /* Keep waiting if NULL */

    /* Clear entry and jump */
    str     xzr, [x1]               /* Clear spin table entry */
    mov     x3, x2
    br      x3                      /* Jump to entry point! */

/* -----------------------------------------------------------------
 * Primary core (0): Initialize system
 * ----------------------------------------------------------------- */
primary_core_init:
    /* Check current exception level */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2              /* Shift EL bits to position */
    cmp     x0, #2                  /* Are we at EL2? */
    b.ne    el1_setup               /* No, already at EL1 */

/* -----------------------------------------------------------------
 * EL2 to EL1 transition
 * ----------------------------------------------------------------- */
el2_to_el1:
    /* Set EL1 execution state to AArch64 */
    mov     x0, #(1 << 31)          /* RW bit: EL1 uses AArch64 */
    msr     hcr_el2, x0

    /* Set up SCTLR_EL1 defaults */
    ldr     x0, =0x30D00800         /* I, C, SA, SA0, EOS, TSCXT bits */
    msr     sctlr_el1, x0

    /* Disable coprocessor traps */
    mov     x0, #0x33FF
    msr     cptr_el2, x0            /* Don't trap FP/SIMD to EL2 */
    msr     hstr_el2, xzr           /* Don't trap coprocessor access */

    /* Enable FP/SIMD access at EL1 */
    mov     x0, #(3 << 20)          /* FPEN bits */
    msr     cpacr_el1, x0

    /* Set SPSR for return to EL1h (using SP_EL1) */
    mov     x0, #0x3C5              /* D=1, A=1, I=1, F=1, EL1h */
    msr     spsr_el2, x0

    /* Set return address */
    adr     x0, el1_setup
    msr     elr_el2, x0

    /* Return to EL1 */
    eret

/* -----------------------------------------------------------------
 * EL1 setup (stack, BSS, call kernel)
 * ----------------------------------------------------------------- */
el1_setup:
    /* Install exception vectors FIRST (before anything can crash) */
    adr     x0, exception_vectors
    msr     vbar_el1, x0
    isb

    /* Get core ID again (might have come from EL2 transition) */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF

    /* Calculate per-core stack pointer */
    ldr     x1, =__stack_start
    mov     x2, #0x4000             /* 16KB per core */
    add     x0, x0, #1              /* Core 0 uses stack 1, etc. */
    mul     x2, x0, x2
    add     x1, x1, x2
    mov     sp, x1

    /* Only core 0 clears BSS */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbnz    x0, skip_bss_clear

    /* Clear BSS section */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
    mov     x2, #0
.bss_loop:
    cmp     x0, x1
    b.ge    .bss_done
    str     x2, [x0], #8
    b       .bss_loop
.bss_done:

skip_bss_clear:
    /* Jump to C code */
    bl      kernel_main

    /* If kernel_main returns, halt */
.halt:
    wfe
    b       .halt

/* -----------------------------------------------------------------
 * Spin table for secondary cores (QEMU only)
 * ----------------------------------------------------------------- */
.align 4
.global spin_table
spin_table:
    .quad 0                         /* Core 1 entry point */
    .quad 0                         /* Core 2 entry point */
    .quad 0                         /* Core 3 entry point */

/* -----------------------------------------------------------------
 * Secondary core entry point (Real Hardware)
 *
 * GPU firmware releases cores 1-3 by writing to mailbox addresses
 * 0xE0/0xE8/0xF0. The core jumps here in EL2, no stack set up.
 * We must:
 *   1. Drop to EL1
 *   2. Set up stack
 *   3. Call C entry function
 * ----------------------------------------------------------------- */
.global secondary_entry_asm
secondary_entry_asm:
    /* Get core ID */
    mrs     x0, mpidr_el1
    and     x19, x0, #0xFF          /* Save core ID in callee-saved reg */

    /* Check exception level */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #3
    b.eq    .sec_el3_to_el2         /* At EL3 - need to drop to EL2 first */
    cmp     x0, #2
    b.eq    .sec_el2_to_el1         /* At EL2 - drop to EL1 */
    b       .sec_el1_setup          /* Already at EL1 */

/* EL3 to EL2 transition */
.sec_el3_to_el2:
    /* Set EL2 to AArch64 */
    mov     x0, #(1 << 10)          /* RW bit for EL2 */
    orr     x0, x0, #(1 << 0)       /* NS bit - non-secure */
    msr     scr_el3, x0

    mov     x0, #0x3C9              /* EL2h, DAIF masked */
    msr     spsr_el3, x0

    adr     x0, .sec_el2_to_el1
    msr     elr_el3, x0
    eret

/* EL2 to EL1 transition */
.sec_el2_to_el1:
    mov     x0, #(1 << 31)          /* RW bit: EL1 uses AArch64 */
    msr     hcr_el2, x0

    ldr     x0, =0x30D00800
    msr     sctlr_el1, x0

    mov     x0, #0x33FF
    msr     cptr_el2, x0
    msr     hstr_el2, xzr

    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0

    mov     x0, #0x3C5              /* EL1h, DAIF masked */
    msr     spsr_el2, x0

    adr     x0, .sec_el1_setup
    msr     elr_el2, x0

    eret

.sec_el1_setup:
    /* Install exception vectors */
    adr     x0, exception_vectors
    msr     vbar_el1, x0
    isb

    /* Set up per-core stack */
    ldr     x1, =__stack_start
    mov     x2, #0x4000             /* 16KB per core */
    add     x0, x19, #1             /* Core ID + 1 */
    mul     x2, x0, x2
    add     x1, x1, x2
    mov     sp, x1

    /* Call C entry with core_id as argument */
    mov     x0, x19
    bl      secondary_core_c_entry

    /* Should not return, but if it does, halt */
.sec_halt:
    wfe
    b       .sec_halt

/* -----------------------------------------------------------------
 * Exception vectors (minimal - just halt on exception)
 * ----------------------------------------------------------------- */
.align 11                           /* Vectors must be 2KB aligned */
.global exception_vectors
exception_vectors:
    /* Current EL with SP0 */
    .align 7
    b       exception_halt          /* Synchronous */
    .align 7
    b       exception_halt          /* IRQ */
    .align 7
    b       exception_halt          /* FIQ */
    .align 7
    b       exception_halt          /* SError */

    /* Current EL with SPx */
    .align 7
    b       exception_halt          /* Synchronous */
    .align 7
    b       irq_handler             /* IRQ */
    .align 7
    b       exception_halt          /* FIQ */
    .align 7
    b       exception_halt          /* SError */

    /* Lower EL using AArch64 */
    .align 7
    b       exception_halt          /* Synchronous */
    .align 7
    b       exception_halt          /* IRQ */
    .align 7
    b       exception_halt          /* FIQ */
    .align 7
    b       exception_halt          /* SError */

    /* Lower EL using AArch32 */
    .align 7
    b       exception_halt          /* Synchronous */
    .align 7
    b       exception_halt          /* IRQ */
    .align 7
    b       exception_halt          /* FIQ */
    .align 7
    b       exception_halt          /* SError */

exception_halt:
    wfe
    b       exception_halt

/* IRQ handler stub - calls C handler */
irq_handler:
    /* Save context */
    sub     sp, sp, #256
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]
    str     x30, [sp, #240]

    /* Call C handler */
    bl      irq_handler_c

    /* Restore context */
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x19, [sp, #144]
    ldp     x20, x21, [sp, #160]
    ldp     x22, x23, [sp, #176]
    ldp     x24, x25, [sp, #192]
    ldp     x26, x27, [sp, #208]
    ldp     x28, x29, [sp, #224]
    ldr     x30, [sp, #240]
    add     sp, sp, #256

    eret

/* -----------------------------------------------------------------
 * Weak C handler (can be overridden)
 * ----------------------------------------------------------------- */
.weak irq_handler_c
irq_handler_c:
    ret
