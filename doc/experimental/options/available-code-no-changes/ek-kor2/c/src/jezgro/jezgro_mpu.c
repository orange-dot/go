/**
 * @file jezgro_mpu.c
 * @brief JEZGRO Microkernel - MPU Implementation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license AGPL-3.0
 *
 * Implementation of ARM Cortex-M MPU isolation for JEZGRO microkernel.
 * Supports STM32G474 (Cortex-M4) and similar ARMv7-M processors.
 */

#include "../../include/ekk/jezgro/jezgro_mpu.h"
#include "../../include/ekk/ekk_hal.h"
#include <string.h>

/* ============================================================================
 * ARM CORTEX-M MPU REGISTERS (ARMv7-M)
 * ============================================================================ */

#ifdef __arm__
/* MPU Type Register */
#define MPU_TYPE        (*(volatile uint32_t*)0xE000ED90)
/* MPU Control Register */
#define MPU_CTRL        (*(volatile uint32_t*)0xE000ED94)
/* MPU Region Number Register */
#define MPU_RNR         (*(volatile uint32_t*)0xE000ED98)
/* MPU Region Base Address Register */
#define MPU_RBAR        (*(volatile uint32_t*)0xE000ED9C)
/* MPU Region Attribute and Size Register */
#define MPU_RASR        (*(volatile uint32_t*)0xE000EDA0)

/* SCB registers for fault status */
#define SCB_CFSR        (*(volatile uint32_t*)0xE000ED28)
#define SCB_MMFAR       (*(volatile uint32_t*)0xE000ED34)

/* MPU_CTRL bits */
#define MPU_CTRL_ENABLE     (1 << 0)
#define MPU_CTRL_HFNMIENA   (1 << 1)
#define MPU_CTRL_PRIVDEFENA (1 << 2)

/* MPU_RBAR bits */
#define MPU_RBAR_VALID      (1 << 4)

/* MPU_RASR bits */
#define MPU_RASR_ENABLE     (1 << 0)
#define MPU_RASR_SIZE_POS   1
#define MPU_RASR_SRD_POS    8
#define MPU_RASR_B_POS      16
#define MPU_RASR_C_POS      17
#define MPU_RASR_S_POS      18
#define MPU_RASR_TEX_POS    19
#define MPU_RASR_AP_POS     24
#define MPU_RASR_XN_POS     28

/* CFSR Memory Management Fault bits */
#define CFSR_MMARVALID      (1 << 7)
#define CFSR_MSTKERR        (1 << 4)
#define CFSR_MUNSTKERR      (1 << 3)
#define CFSR_DACCVIOL       (1 << 1)
#define CFSR_IACCVIOL       (1 << 0)

#else
/* Simulation stubs for non-ARM platforms */
static uint32_t sim_mpu_type = 0x00000800;  /* 8 regions */
static uint32_t sim_mpu_ctrl = 0;
static uint32_t sim_mpu_regions[8][2] = {0};  /* RBAR, RASR per region */
static uint8_t sim_current_region = 0;

#define MPU_TYPE        sim_mpu_type
#define MPU_CTRL        sim_mpu_ctrl
#define MPU_RNR         sim_current_region
#define MPU_RBAR        sim_mpu_regions[sim_current_region][0]
#define MPU_RASR        sim_mpu_regions[sim_current_region][1]

#define MPU_CTRL_ENABLE     (1 << 0)
#define MPU_CTRL_HFNMIENA   (1 << 1)
#define MPU_CTRL_PRIVDEFENA (1 << 2)
#define MPU_RBAR_VALID      (1 << 4)
#define MPU_RASR_ENABLE     (1 << 0)
#define MPU_RASR_SIZE_POS   1
#define MPU_RASR_SRD_POS    8
#define MPU_RASR_B_POS      16
#define MPU_RASR_C_POS      17
#define MPU_RASR_S_POS      18
#define MPU_RASR_TEX_POS    19
#define MPU_RASR_AP_POS     24
#define MPU_RASR_XN_POS     28
#endif

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

/** Service contexts */
static jezgro_mpu_context_t g_service_contexts[JEZGRO_MAX_SERVICES + 1];

/** Currently active service (0 = kernel) */
static uint8_t g_current_service = 0;

/** Number of registered services */
static uint8_t g_num_services = 0;

/** Fault handler callback */
static jezgro_mpu_fault_cb g_fault_callback = NULL;

/** MPU initialized flag */
static bool g_mpu_initialized = false;

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

/**
 * @brief Convert size to MPU SIZE field value
 *
 * MPU SIZE field encodes size as 2^(SIZE+1) bytes.
 * Minimum region size is 32 bytes (SIZE = 4).
 */
static uint8_t size_to_mpu_size(uint32_t size)
{
    if (size < 32) return 4;  /* Minimum */

    /* Find highest bit set */
    uint8_t msb = 31;
    while (msb > 0 && !(size & (1u << msb))) {
        msb--;
    }

    /* Round up to power of 2 */
    if (size > (1u << msb)) {
        msb++;
    }

    return msb - 1;  /* SIZE = log2(size) - 1 */
}

/**
 * @brief Check if address is aligned to size
 */
static bool is_aligned(uint32_t addr, uint32_t size)
{
    return (addr & (size - 1)) == 0;
}

/**
 * @brief Build RASR register value from configuration
 */
static uint32_t build_rasr(const jezgro_mpu_region_t* config)
{
    uint32_t rasr = 0;

    if (config->enabled) {
        rasr |= MPU_RASR_ENABLE;
    }

    /* Size field */
    uint8_t size_field = size_to_mpu_size(config->size);
    rasr |= (size_field << MPU_RASR_SIZE_POS);

    /* Access permissions */
    rasr |= ((uint32_t)config->ap << MPU_RASR_AP_POS);

    /* Execute never */
    if (config->execute_never) {
        rasr |= (1u << MPU_RASR_XN_POS);
    }

    /* Memory type attributes */
    switch (config->mem_type) {
        case JEZGRO_MEM_NORMAL:
            /* TEX=001, C=1, B=1 (write-back, write-allocate) */
            rasr |= (1u << MPU_RASR_TEX_POS);
            rasr |= (1u << MPU_RASR_C_POS);
            rasr |= (1u << MPU_RASR_B_POS);
            break;

        case JEZGRO_MEM_DEVICE:
            /* TEX=000, C=0, B=1 (device, shareable) */
            rasr |= (1u << MPU_RASR_B_POS);
            break;

        case JEZGRO_MEM_STRONGLY_ORD:
            /* TEX=000, C=0, B=0 (strongly ordered) */
            break;
    }

    /* Shareable */
    if (config->shareable) {
        rasr |= (1u << MPU_RASR_S_POS);
    }

    return rasr;
}

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

ekk_error_t jezgro_mpu_init(void)
{
    if (g_mpu_initialized) {
        return EKK_OK;
    }

    /* Check for MPU presence */
    if (!jezgro_mpu_available()) {
        ekk_hal_printf("JEZGRO: MPU not available on this platform\n");
        return EKK_ERR_NOT_SUPPORTED;
    }

    uint8_t num_regions = jezgro_mpu_num_regions();
    ekk_hal_printf("JEZGRO: MPU init, %d regions available\n", num_regions);

    /* Disable MPU during configuration */
    MPU_CTRL = 0;

    /* Clear all regions */
    for (uint8_t i = 0; i < num_regions; i++) {
        MPU_RNR = i;
        MPU_RBAR = 0;
        MPU_RASR = 0;
    }

    /* Initialize kernel context (service 0) */
    memset(&g_service_contexts[0], 0, sizeof(jezgro_mpu_context_t));
    g_service_contexts[0].service_id = 0;
    g_service_contexts[0].privileged = true;

    /* Setup default kernel regions */
    jezgro_mpu_region_t kernel_code = {
        .base_addr = 0x08000000,  /* Flash start (STM32) */
        .size = 512 * 1024,       /* 512KB */
        .ap = JEZGRO_AP_PRIV_RO,
        .mem_type = JEZGRO_MEM_NORMAL,
        .execute_never = false,
        .shareable = false,
        .enabled = true
    };
    jezgro_mpu_configure_region(JEZGRO_REGION_KERNEL_CODE, &kernel_code);

    jezgro_mpu_region_t kernel_data = {
        .base_addr = 0x20000000,  /* SRAM start (STM32) */
        .size = 128 * 1024,       /* 128KB */
        .ap = JEZGRO_AP_PRIV_RW,
        .mem_type = JEZGRO_MEM_NORMAL,
        .execute_never = true,
        .shareable = false,
        .enabled = true
    };
    jezgro_mpu_configure_region(JEZGRO_REGION_KERNEL_DATA, &kernel_data);

    jezgro_mpu_region_t peripherals = {
        .base_addr = 0x40000000,  /* Peripheral base */
        .size = 512 * 1024 * 1024, /* 512MB peripheral region */
        .ap = JEZGRO_AP_PRIV_RW,
        .mem_type = JEZGRO_MEM_DEVICE,
        .execute_never = true,
        .shareable = false,
        .enabled = true
    };
    jezgro_mpu_configure_region(JEZGRO_REGION_PERIPHERALS, &peripherals);

    /* Set default fault handler */
    jezgro_mpu_set_fault_handler(jezgro_mpu_default_fault_handler);

    g_mpu_initialized = true;
    g_current_service = 0;
    g_num_services = 0;

    return EKK_OK;
}

bool jezgro_mpu_available(void)
{
    /* Check DREGION field in MPU_TYPE (bits 15:8) */
    uint32_t dregion = (MPU_TYPE >> 8) & 0xFF;
    return dregion > 0;
}

uint8_t jezgro_mpu_num_regions(void)
{
    return (MPU_TYPE >> 8) & 0xFF;
}

/* ============================================================================
 * REGION MANAGEMENT
 * ============================================================================ */

ekk_error_t jezgro_mpu_configure_region(jezgro_region_id_t region_id,
                                         const jezgro_mpu_region_t* config)
{
    if (region_id >= JEZGRO_MPU_MAX_REGIONS) {
        return EKK_ERR_INVALID_ARG;
    }

    if (config == NULL) {
        return EKK_ERR_INVALID_ARG;
    }

    /* Validate alignment */
    uint32_t actual_size = 1u << (size_to_mpu_size(config->size) + 1);
    if (!is_aligned(config->base_addr, actual_size)) {
        ekk_hal_printf("JEZGRO: Region %d base 0x%08X not aligned to size %u\n",
                       region_id, config->base_addr, actual_size);
        return EKK_ERR_INVALID_ARG;
    }

    uint32_t state = ekk_hal_critical_enter();

    /* Select region */
    MPU_RNR = region_id;

    /* Configure base address with region number */
    MPU_RBAR = (config->base_addr & ~0x1F) | MPU_RBAR_VALID | region_id;

    /* Configure attributes */
    MPU_RASR = build_rasr(config);

    ekk_hal_critical_exit(state);

    return EKK_OK;
}

ekk_error_t jezgro_mpu_enable_region(jezgro_region_id_t region_id)
{
    if (region_id >= JEZGRO_MPU_MAX_REGIONS) {
        return EKK_ERR_INVALID_ARG;
    }

    uint32_t state = ekk_hal_critical_enter();

    MPU_RNR = region_id;
    MPU_RASR |= MPU_RASR_ENABLE;

    ekk_hal_critical_exit(state);

    return EKK_OK;
}

ekk_error_t jezgro_mpu_disable_region(jezgro_region_id_t region_id)
{
    if (region_id >= JEZGRO_MPU_MAX_REGIONS) {
        return EKK_ERR_INVALID_ARG;
    }

    uint32_t state = ekk_hal_critical_enter();

    MPU_RNR = region_id;
    MPU_RASR &= ~MPU_RASR_ENABLE;

    ekk_hal_critical_exit(state);

    return EKK_OK;
}

void jezgro_mpu_enable(void)
{
#ifdef __arm__
    /* Data and instruction barriers before enabling MPU */
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
#endif

    /* Enable MPU with:
     * - PRIVDEFENA: Enable default memory map for privileged access
     * - HFNMIENA: Enable MPU during hard fault/NMI
     */
    MPU_CTRL = MPU_CTRL_ENABLE | MPU_CTRL_PRIVDEFENA;

#ifdef __arm__
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
#endif
}

void jezgro_mpu_disable(void)
{
#ifdef __arm__
    __asm__ volatile ("dsb" ::: "memory");
#endif
    MPU_CTRL = 0;
#ifdef __arm__
    __asm__ volatile ("dsb" ::: "memory");
    __asm__ volatile ("isb" ::: "memory");
#endif
}

/* ============================================================================
 * SERVICE ISOLATION
 * ============================================================================ */

ekk_error_t jezgro_mpu_register_service(const jezgro_service_desc_t* desc)
{
    if (desc == NULL || desc->id == 0) {
        return EKK_ERR_INVALID_ARG;
    }

    if (g_num_services >= JEZGRO_MAX_SERVICES) {
        return EKK_ERR_LIMIT;
    }

    /* Calculate region indices for this service */
    uint8_t code_region = JEZGRO_REGION_SERVICE1_CODE + (g_num_services * 2);
    uint8_t data_region = JEZGRO_REGION_SERVICE1_DATA + (g_num_services * 2);

    if (data_region >= JEZGRO_MPU_MAX_REGIONS) {
        return EKK_ERR_LIMIT;
    }

    /* Store service context */
    uint8_t ctx_idx = g_num_services + 1;
    jezgro_mpu_context_t* ctx = &g_service_contexts[ctx_idx];

    ctx->service_id = desc->id;
    ctx->privileged = (desc->priv == JEZGRO_PRIV_KERNEL);
    ctx->num_regions = 2;

    /* Code region - execute only */
    ctx->regions[0] = (jezgro_mpu_region_t){
        .base_addr = (uint32_t)desc->code_start,
        .size = desc->code_size,
        .ap = ctx->privileged ? JEZGRO_AP_PRIV_RO : JEZGRO_AP_RO,
        .mem_type = JEZGRO_MEM_NORMAL,
        .execute_never = false,
        .shareable = false,
        .enabled = true
    };

    /* Data region - no execute */
    ctx->regions[1] = (jezgro_mpu_region_t){
        .base_addr = (uint32_t)desc->data_start,
        .size = desc->data_size + desc->stack_size,
        .ap = ctx->privileged ? JEZGRO_AP_PRIV_RW : JEZGRO_AP_FULL_ACCESS,
        .mem_type = JEZGRO_MEM_NORMAL,
        .execute_never = true,
        .shareable = false,
        .enabled = true
    };

    /* Configure MPU regions */
    jezgro_mpu_configure_region(code_region, &ctx->regions[0]);
    jezgro_mpu_configure_region(data_region, &ctx->regions[1]);

    g_num_services++;

    ekk_hal_printf("JEZGRO: Registered service '%s' (id=%d, priv=%d)\n",
                   desc->name, desc->id, desc->priv);

    return EKK_OK;
}

ekk_error_t jezgro_mpu_switch_context(uint8_t service_id)
{
    /* Find service context */
    jezgro_mpu_context_t* ctx = NULL;

    if (service_id == 0) {
        ctx = &g_service_contexts[0];
    } else {
        for (uint8_t i = 1; i <= g_num_services; i++) {
            if (g_service_contexts[i].service_id == service_id) {
                ctx = &g_service_contexts[i];
                break;
            }
        }
    }

    if (ctx == NULL) {
        return EKK_ERR_NOT_FOUND;
    }

    uint32_t state = ekk_hal_critical_enter();

    /* Disable MPU during reconfiguration */
    jezgro_mpu_disable();

    /* Reconfigure service-specific regions */
    if (service_id != 0) {
        uint8_t code_region = JEZGRO_REGION_SERVICE1_CODE;
        uint8_t data_region = JEZGRO_REGION_SERVICE1_DATA;

        jezgro_mpu_configure_region(code_region, &ctx->regions[0]);
        jezgro_mpu_configure_region(data_region, &ctx->regions[1]);
    }

    /* Re-enable MPU */
    jezgro_mpu_enable();

    g_current_service = service_id;

    ekk_hal_critical_exit(state);

    return EKK_OK;
}

void jezgro_mpu_enter_kernel(void)
{
#ifdef __arm__
    /* Switch to privileged mode using SVC or direct control register write */
    /* On Cortex-M, clear bit 0 of CONTROL register for privileged mode */
    __asm__ volatile (
        "mrs r0, CONTROL\n"
        "bic r0, r0, #1\n"
        "msr CONTROL, r0\n"
        "isb"
        ::: "r0", "memory"
    );
#else
    /* No-op on non-ARM platforms */
    (void)0;
#endif
}

void jezgro_mpu_enter_user(void)
{
#ifdef __arm__
    /* Switch to unprivileged mode */
    /* Set bit 0 of CONTROL register for unprivileged mode */
    __asm__ volatile (
        "mrs r0, CONTROL\n"
        "orr r0, r0, #1\n"
        "msr CONTROL, r0\n"
        "isb"
        ::: "r0", "memory"
    );
#else
    /* No-op on non-ARM platforms */
    (void)0;
#endif
}

/* ============================================================================
 * FAULT HANDLING
 * ============================================================================ */

void jezgro_mpu_set_fault_handler(jezgro_mpu_fault_cb callback)
{
    g_fault_callback = callback;
}

void jezgro_mpu_default_fault_handler(const jezgro_mpu_fault_t* fault)
{
    ekk_hal_printf("\n*** JEZGRO MPU FAULT ***\n");
    ekk_hal_printf("Service: %d\n", fault->faulting_service);
    ekk_hal_printf("Address: 0x%08X\n", fault->fault_addr);
    ekk_hal_printf("Status:  0x%08X\n", fault->fault_status);

    if (fault->is_instruction_access) {
        ekk_hal_printf("Type: Instruction access violation\n");
    } else if (fault->is_data_access) {
        ekk_hal_printf("Type: Data access violation\n");
    } else if (fault->is_stacking_error) {
        ekk_hal_printf("Type: Stacking error\n");
    }

    ekk_hal_printf("************************\n");

    /* In a full implementation, this would trigger service restart
     * via the reincarnation server. For now, we just log and halt. */
}

/**
 * @brief MemManage fault handler (called from vector table)
 */
void MemManage_Handler(void)
{
#ifdef __arm__
    jezgro_mpu_fault_t fault = {0};

    fault.fault_status = SCB_CFSR & 0xFF;  /* MMFSR is lower byte */
    fault.faulting_service = g_current_service;

    if (fault.fault_status & CFSR_MMARVALID) {
        fault.fault_addr = SCB_MMFAR;
    }

    fault.is_instruction_access = (fault.fault_status & CFSR_IACCVIOL) != 0;
    fault.is_data_access = (fault.fault_status & CFSR_DACCVIOL) != 0;
    fault.is_stacking_error = (fault.fault_status & CFSR_MSTKERR) != 0;

    /* Clear fault status */
    SCB_CFSR = fault.fault_status;

    if (g_fault_callback) {
        g_fault_callback(&fault);
    }
#endif

    /* Halt (should be replaced by service restart in production) */
    while (1) {
#ifdef __arm__
        __asm__ volatile ("wfi");
#endif
    }
}

/* ============================================================================
 * DEBUG
 * ============================================================================ */

void jezgro_mpu_dump_config(void)
{
    ekk_hal_printf("\nJEZGRO MPU Configuration:\n");
    ekk_hal_printf("  Control: 0x%08X\n", MPU_CTRL);
    ekk_hal_printf("  Current service: %d\n", g_current_service);
    ekk_hal_printf("  Registered services: %d\n", g_num_services);

    uint8_t num_regions = jezgro_mpu_num_regions();
    for (uint8_t i = 0; i < num_regions; i++) {
        MPU_RNR = i;
        uint32_t rbar = MPU_RBAR;
        uint32_t rasr = MPU_RASR;

        if (rasr & MPU_RASR_ENABLE) {
            uint8_t size_field = (rasr >> MPU_RASR_SIZE_POS) & 0x1F;
            uint32_t size = 1u << (size_field + 1);
            uint8_t ap = (rasr >> MPU_RASR_AP_POS) & 0x7;
            bool xn = (rasr >> MPU_RASR_XN_POS) & 0x1;

            ekk_hal_printf("  Region %d: base=0x%08X size=%uKB AP=%d XN=%d\n",
                           i, rbar & ~0x1F, size / 1024, ap, xn);
        } else {
            ekk_hal_printf("  Region %d: disabled\n", i);
        }
    }
}

ekk_error_t jezgro_mpu_verify_config(void)
{
    uint8_t num_regions = jezgro_mpu_num_regions();

    /* Check for overlapping regions */
    for (uint8_t i = 0; i < num_regions; i++) {
        MPU_RNR = i;
        uint32_t rbar_i = MPU_RBAR;
        uint32_t rasr_i = MPU_RASR;

        if (!(rasr_i & MPU_RASR_ENABLE)) continue;

        uint8_t size_i = (rasr_i >> MPU_RASR_SIZE_POS) & 0x1F;
        uint32_t start_i = rbar_i & ~0x1F;
        uint32_t end_i = start_i + (1u << (size_i + 1));

        for (uint8_t j = i + 1; j < num_regions; j++) {
            MPU_RNR = j;
            uint32_t rbar_j = MPU_RBAR;
            uint32_t rasr_j = MPU_RASR;

            if (!(rasr_j & MPU_RASR_ENABLE)) continue;

            uint8_t size_j = (rasr_j >> MPU_RASR_SIZE_POS) & 0x1F;
            uint32_t start_j = rbar_j & ~0x1F;
            uint32_t end_j = start_j + (1u << (size_j + 1));

            /* Check for overlap */
            if (start_i < end_j && start_j < end_i) {
                ekk_hal_printf("JEZGRO: Warning - regions %d and %d overlap\n", i, j);
                /* Overlap is allowed in MPU (higher region number takes precedence) */
            }
        }
    }

    return EKK_OK;
}
