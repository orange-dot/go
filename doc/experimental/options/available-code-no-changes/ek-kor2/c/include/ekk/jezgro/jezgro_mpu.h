/**
 * @file jezgro_mpu.h
 * @brief JEZGRO Microkernel - Memory Protection Unit (MPU) Isolation
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license AGPL-3.0
 *
 * Provides hardware-enforced memory isolation for services running on
 * ARM Cortex-M processors with MPU support (Cortex-M4, M7, M33).
 *
 * DESIGN GOALS:
 * - Isolate services into separate memory regions
 * - Prevent faulty services from corrupting kernel/other services
 * - Enable privilege separation (kernel vs user mode)
 * - Support 8 memory regions (typical MPU limit)
 *
 * MEMORY LAYOUT (STM32G474):
 *   Region 0: Kernel code (privileged, execute-only)
 *   Region 1: Kernel data (privileged, no-execute)
 *   Region 2: Shared IPC buffer (unprivileged, no-execute)
 *   Region 3: Service 1 code (unprivileged, execute-only)
 *   Region 4: Service 1 data (unprivileged, no-execute)
 *   Region 5: Service 2 code (unprivileged, execute-only)
 *   Region 6: Service 2 data (unprivileged, no-execute)
 *   Region 7: Peripheral access (privileged, device memory)
 *
 * PAPER REFERENCE:
 * ROJ Paper Section IV.B - JEZGRO Microkernel: "Hardware-enforced isolation
 * via MPU ensures that a crash in coordination logic cannot compromise
 * safety-critical LLC control"
 */

#ifndef JEZGRO_MPU_H
#define JEZGRO_MPU_H

#include "../ekk_types.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/** Maximum number of MPU regions (hardware dependent) */
#define JEZGRO_MPU_MAX_REGIONS      8

/** Maximum number of services that can be isolated */
#define JEZGRO_MAX_SERVICES         3

/** Stack size per service (bytes) */
#define JEZGRO_SERVICE_STACK_SIZE   2048

/** IPC buffer size (shared memory) */
#define JEZGRO_IPC_BUFFER_SIZE      4096

/* ============================================================================
 * REGION IDENTIFIERS
 * ============================================================================ */

typedef enum {
    JEZGRO_REGION_KERNEL_CODE   = 0,
    JEZGRO_REGION_KERNEL_DATA   = 1,
    JEZGRO_REGION_IPC_BUFFER    = 2,
    JEZGRO_REGION_SERVICE1_CODE = 3,
    JEZGRO_REGION_SERVICE1_DATA = 4,
    JEZGRO_REGION_SERVICE2_CODE = 5,
    JEZGRO_REGION_SERVICE2_DATA = 6,
    JEZGRO_REGION_PERIPHERALS   = 7,
} jezgro_region_id_t;

/* ============================================================================
 * ACCESS PERMISSIONS
 * ============================================================================ */

/**
 * @brief Memory access permissions
 */
typedef enum {
    JEZGRO_AP_NO_ACCESS     = 0,  /**< No access at all */
    JEZGRO_AP_PRIV_RW       = 1,  /**< Privileged read-write only */
    JEZGRO_AP_PRIV_RW_UNPRIV_RO = 2,  /**< Privileged RW, unprivileged RO */
    JEZGRO_AP_FULL_ACCESS   = 3,  /**< Full access */
    JEZGRO_AP_PRIV_RO       = 5,  /**< Privileged read-only */
    JEZGRO_AP_RO            = 6,  /**< Read-only for all */
} jezgro_access_perm_t;

/**
 * @brief Memory type attributes
 */
typedef enum {
    JEZGRO_MEM_NORMAL       = 0,  /**< Normal memory (cacheable) */
    JEZGRO_MEM_DEVICE       = 1,  /**< Device memory (non-cacheable) */
    JEZGRO_MEM_STRONGLY_ORD = 2,  /**< Strongly ordered */
} jezgro_mem_type_t;

/* ============================================================================
 * REGION CONFIGURATION
 * ============================================================================ */

/**
 * @brief MPU region configuration
 */
typedef struct {
    uint32_t base_addr;         /**< Region base address (must be aligned) */
    uint32_t size;              /**< Region size in bytes */
    jezgro_access_perm_t ap;    /**< Access permissions */
    jezgro_mem_type_t mem_type; /**< Memory type */
    bool execute_never;         /**< XN bit - disable code execution */
    bool shareable;             /**< Shareable between cores */
    bool enabled;               /**< Region enabled */
} jezgro_mpu_region_t;

/**
 * @brief MPU state for a service context
 */
typedef struct {
    uint8_t service_id;
    uint8_t num_regions;
    jezgro_mpu_region_t regions[JEZGRO_MPU_MAX_REGIONS];
    bool privileged;            /**< Running in privileged mode */
} jezgro_mpu_context_t;

/* ============================================================================
 * SERVICE DEFINITION
 * ============================================================================ */

/**
 * @brief Service privilege level
 */
typedef enum {
    JEZGRO_PRIV_KERNEL = 0,     /**< Kernel privilege (LLC, safety) */
    JEZGRO_PRIV_USER   = 1,     /**< User privilege (ROJ, diagnostics) */
} jezgro_privilege_t;

/**
 * @brief Service descriptor
 */
typedef struct {
    uint8_t id;                 /**< Service identifier */
    const char* name;           /**< Human-readable name */
    jezgro_privilege_t priv;    /**< Privilege level */

    /* Memory regions */
    void* code_start;           /**< Start of code section */
    uint32_t code_size;         /**< Size of code section */
    void* data_start;           /**< Start of data section */
    uint32_t data_size;         /**< Size of data section */
    void* stack_start;          /**< Stack base */
    uint32_t stack_size;        /**< Stack size */

    /* Entry point */
    void (*entry)(void);        /**< Service entry point */
} jezgro_service_desc_t;

/* ============================================================================
 * INITIALIZATION
 * ============================================================================ */

/**
 * @brief Initialize JEZGRO MPU subsystem
 *
 * Sets up the MPU with default kernel regions and prepares
 * for service isolation.
 *
 * @return EKK_OK on success, error code otherwise
 */
ekk_error_t jezgro_mpu_init(void);

/**
 * @brief Check if MPU hardware is present
 *
 * @return true if MPU is available
 */
bool jezgro_mpu_available(void);

/**
 * @brief Get number of available MPU regions
 *
 * @return Number of regions (typically 8)
 */
uint8_t jezgro_mpu_num_regions(void);

/* ============================================================================
 * REGION MANAGEMENT
 * ============================================================================ */

/**
 * @brief Configure an MPU region
 *
 * @param region_id Region to configure (0-7)
 * @param config Region configuration
 * @return EKK_OK on success
 */
ekk_error_t jezgro_mpu_configure_region(jezgro_region_id_t region_id,
                                         const jezgro_mpu_region_t* config);

/**
 * @brief Enable an MPU region
 *
 * @param region_id Region to enable
 * @return EKK_OK on success
 */
ekk_error_t jezgro_mpu_enable_region(jezgro_region_id_t region_id);

/**
 * @brief Disable an MPU region
 *
 * @param region_id Region to disable
 * @return EKK_OK on success
 */
ekk_error_t jezgro_mpu_disable_region(jezgro_region_id_t region_id);

/**
 * @brief Enable MPU globally
 */
void jezgro_mpu_enable(void);

/**
 * @brief Disable MPU globally
 */
void jezgro_mpu_disable(void);

/* ============================================================================
 * SERVICE ISOLATION
 * ============================================================================ */

/**
 * @brief Register a service for MPU protection
 *
 * Sets up memory regions for the service based on its descriptor.
 * Service code will be execute-only, data will be no-execute.
 *
 * @param desc Service descriptor
 * @return EKK_OK on success, EKK_ERR_LIMIT if no regions available
 */
ekk_error_t jezgro_mpu_register_service(const jezgro_service_desc_t* desc);

/**
 * @brief Switch MPU context to a service
 *
 * Reconfigures MPU regions for the specified service.
 * Called during service switch.
 *
 * @param service_id Service to switch to
 * @return EKK_OK on success
 */
ekk_error_t jezgro_mpu_switch_context(uint8_t service_id);

/**
 * @brief Switch to kernel context
 *
 * Enables full privileged access for kernel operations.
 */
void jezgro_mpu_enter_kernel(void);

/**
 * @brief Switch to unprivileged mode
 *
 * Drops privileges before running user service.
 */
void jezgro_mpu_enter_user(void);

/* ============================================================================
 * FAULT HANDLING
 * ============================================================================ */

/**
 * @brief MPU fault information
 */
typedef struct {
    uint32_t fault_addr;        /**< Address that caused fault */
    uint32_t fault_status;      /**< MMFSR register value */
    uint8_t faulting_service;   /**< Service that caused fault */
    bool is_stacking_error;     /**< True if fault during stacking */
    bool is_instruction_access; /**< True if instruction access fault */
    bool is_data_access;        /**< True if data access fault */
} jezgro_mpu_fault_t;

/**
 * @brief Fault handler callback type
 */
typedef void (*jezgro_mpu_fault_cb)(const jezgro_mpu_fault_t* fault);

/**
 * @brief Register fault handler callback
 *
 * Called when MPU violation occurs. Handler should NOT return
 * if fault is unrecoverable.
 *
 * @param callback Fault handler function
 */
void jezgro_mpu_set_fault_handler(jezgro_mpu_fault_cb callback);

/**
 * @brief Default fault handler (terminates faulting service)
 *
 * @param fault Fault information
 */
void jezgro_mpu_default_fault_handler(const jezgro_mpu_fault_t* fault);

/* ============================================================================
 * DEBUG
 * ============================================================================ */

/**
 * @brief Dump MPU configuration to debug output
 */
void jezgro_mpu_dump_config(void);

/**
 * @brief Verify MPU configuration is valid
 *
 * Checks for overlapping regions, alignment issues, etc.
 *
 * @return EKK_OK if valid
 */
ekk_error_t jezgro_mpu_verify_config(void);

#ifdef __cplusplus
}
#endif

#endif /* JEZGRO_MPU_H */
