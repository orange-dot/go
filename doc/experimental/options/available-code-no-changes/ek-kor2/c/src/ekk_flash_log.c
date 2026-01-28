/**
 * @file ekk_flash_log.c
 * @brief EK-KOR v2 - Flash Log with Wear-Leveling
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Append-only log stored in Flash with wear-leveling for AURIX TC397.
 *
 * WEAR-LEVELING STRATEGY:
 * - Circular buffer across sectors
 * - Sector erase count tracking
 * - Prefer sectors with fewer erases
 * - Max 100K erases per sector (Flash endurance)
 *
 * FLASH LAYOUT (TC397, 16MB):
 * - Sector size: 16KB
 * - Entry size: 64 bytes
 * - Entries per sector: 256
 * - Total sectors: 1024
 * - Total capacity: 262,144 entries
 */

#include "ekk/ekk_gateway.h"
#include <string.h>

/* ============================================================================
 * CONFIGURATION
 * ============================================================================ */

/** Entries per sector (16KB / 64 bytes) */
#define ENTRIES_PER_SECTOR      (EKK_FLASH_SECTOR_SIZE / sizeof(ekk_flash_entry_t))

/** Maximum sectors (16MB / 16KB) */
#define MAX_SECTORS             1024

/** Flash endurance (erase cycles) */
#define FLASH_ENDURANCE         100000

/** Magic number for sector header */
#define SECTOR_MAGIC            0x454B4B31  /* "EKK1" */

/* ============================================================================
 * SECTOR HEADER (first entry of each sector)
 * ============================================================================ */

/**
 * @brief Sector header stored in first 64 bytes
 */
EKK_PACK_BEGIN
typedef struct EKK_PACKED {
    uint32_t magic;             /**< SECTOR_MAGIC */
    uint32_t sector_id;         /**< Sector index */
    uint32_t erase_count;       /**< Times this sector erased */
    uint32_t first_seq;         /**< First sequence in sector */
    uint32_t entry_count;       /**< Valid entries in sector */
    uint32_t timestamp_us;      /**< Sector creation time */
    uint8_t _reserved[40];
} ekk_sector_header_t;
EKK_PACK_END

EKK_STATIC_ASSERT(sizeof(ekk_sector_header_t) == 64, "Sector header must be 64 bytes");

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

/** Sector erase counts (in SRAM for fast access) */
static uint32_t sector_erase_counts[MAX_SECTORS];

/** Sector valid entry counts */
static uint32_t sector_entry_counts[MAX_SECTORS];

/** Current write sector */
static uint32_t current_sector;

/** Write position within current sector */
static uint32_t sector_write_pos;

/* ============================================================================
 * HAL ABSTRACTION (Platform-specific implementation required)
 * ============================================================================ */

/**
 * @brief Read from flash
 * @note Weak - override with platform implementation
 */
EKK_WEAK ekk_error_t ekk_hal_flash_read(uint32_t address, void *buffer, uint32_t len) {
    /* Default: memory-mapped flash read */
    memcpy(buffer, (const void *)(uintptr_t)address, len);
    return EKK_OK;
}

/**
 * @brief Write to flash (must be erased first)
 * @note Weak - override with platform implementation
 */
EKK_WEAK ekk_error_t ekk_hal_flash_write(uint32_t address, const void *data, uint32_t len) {
    /* Default: no-op (implement for real hardware) */
    EKK_UNUSED(address);
    EKK_UNUSED(data);
    EKK_UNUSED(len);
    return EKK_OK;
}

/**
 * @brief Erase flash sector
 * @note Weak - override with platform implementation
 */
EKK_WEAK ekk_error_t ekk_hal_flash_erase_sector(uint32_t address) {
    /* Default: no-op (implement for real hardware) */
    EKK_UNUSED(address);
    return EKK_OK;
}

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/**
 * @brief Get address of sector
 */
static inline uint32_t sector_address(ekk_flash_log_t *log, uint32_t sector) {
    return log->base_address + (sector * EKK_FLASH_SECTOR_SIZE);
}

/**
 * @brief Get address of entry within sector
 */
static inline uint32_t entry_address(ekk_flash_log_t *log, uint32_t sector, uint32_t entry) {
    return sector_address(log, sector) + (entry * sizeof(ekk_flash_entry_t));
}

/**
 * @brief Read sector header
 */
static ekk_error_t read_sector_header(ekk_flash_log_t *log, uint32_t sector,
                                       ekk_sector_header_t *header) {
    return ekk_hal_flash_read(sector_address(log, sector),
                              header, sizeof(*header));
}

/**
 * @brief Write sector header
 */
static ekk_error_t write_sector_header(ekk_flash_log_t *log, uint32_t sector,
                                        const ekk_sector_header_t *header) {
    return ekk_hal_flash_write(sector_address(log, sector),
                               header, sizeof(*header));
}

/**
 * @brief Find sector with minimum erase count
 */
static uint32_t find_min_erase_sector(ekk_flash_log_t *log, uint32_t exclude) {
    uint32_t min_erases = UINT32_MAX;
    uint32_t min_sector = 0;

    for (uint32_t i = 0; i < log->sector_count; i++) {
        if (i == exclude) continue;

        if (sector_erase_counts[i] < min_erases) {
            min_erases = sector_erase_counts[i];
            min_sector = i;
        }
    }

    return min_sector;
}

/**
 * @brief Erase and initialize sector
 */
static ekk_error_t erase_and_init_sector(ekk_flash_log_t *log, uint32_t sector,
                                          uint32_t first_seq) {
    /* Erase sector */
    ekk_error_t err = ekk_hal_flash_erase_sector(sector_address(log, sector));
    if (err != EKK_OK) {
        return err;
    }

    /* Update erase count */
    sector_erase_counts[sector]++;
    log->total_erases++;

    /* Write sector header */
    ekk_sector_header_t header = {
        .magic = SECTOR_MAGIC,
        .sector_id = sector,
        .erase_count = sector_erase_counts[sector],
        .first_seq = first_seq,
        .entry_count = 0,
        .timestamp_us = (uint32_t)ekk_hal_time_us()
    };
    memset(header._reserved, 0, sizeof(header._reserved));

    err = write_sector_header(log, sector, &header);
    if (err != EKK_OK) {
        return err;
    }

    sector_entry_counts[sector] = 0;

    return EKK_OK;
}

/**
 * @brief Move to next sector for writing
 */
static ekk_error_t advance_sector(ekk_flash_log_t *log) {
    /* Find next sector (prefer low erase count) */
    uint32_t next = (current_sector + 1) % log->sector_count;

    /* If next sector has data, use wear-leveling */
    if (sector_entry_counts[next] > 0) {
        /* Prefer sector with minimum erases */
        next = find_min_erase_sector(log, current_sector);
    }

    /* Erase and initialize */
    ekk_error_t err = erase_and_init_sector(log, next, log->global_sequence);
    if (err != EKK_OK) {
        return err;
    }

    current_sector = next;
    sector_write_pos = 1;  /* Skip header */
    log->write_sector = next;

    return EKK_OK;
}

/* ============================================================================
 * PUBLIC API
 * ============================================================================ */

ekk_error_t ekk_flash_log_init(ekk_flash_log_t *log,
                                uint32_t base_address,
                                uint32_t capacity) {
    if (!log) {
        return EKK_ERR_INVALID_ARG;
    }

    memset(log, 0, sizeof(*log));
    log->base_address = base_address;
    log->capacity = capacity;
    log->sector_count = capacity / ENTRIES_PER_SECTOR;

    if (log->sector_count > MAX_SECTORS) {
        log->sector_count = MAX_SECTORS;
    }

    /* Initialize erase count tracking */
    memset(sector_erase_counts, 0, sizeof(sector_erase_counts));
    memset(sector_entry_counts, 0, sizeof(sector_entry_counts));

    /* Scan existing sectors to recover state */
    uint32_t max_seq = 0;
    uint32_t newest_sector = 0;

    for (uint32_t s = 0; s < log->sector_count; s++) {
        ekk_sector_header_t header;
        if (read_sector_header(log, s, &header) == EKK_OK) {
            if (header.magic == SECTOR_MAGIC) {
                /* Valid sector */
                sector_erase_counts[s] = header.erase_count;
                sector_entry_counts[s] = header.entry_count;

                uint32_t sector_max_seq = header.first_seq + header.entry_count;
                if (sector_max_seq > max_seq) {
                    max_seq = sector_max_seq;
                    newest_sector = s;
                }
            }
        }
    }

    /* Set write position to continue from newest sector */
    current_sector = newest_sector;
    sector_write_pos = sector_entry_counts[newest_sector] + 1;  /* +1 for header */
    log->write_sector = newest_sector;
    log->global_sequence = max_seq;

    /* Handle sector overflow */
    if (sector_write_pos >= ENTRIES_PER_SECTOR) {
        advance_sector(log);
    }

    return EKK_OK;
}

uint32_t ekk_flash_log_write(ekk_flash_log_t *log,
                              const ekk_flash_entry_t *entry) {
    if (!log || !entry) {
        return 0xFFFFFFFF;
    }

    /* Check if current sector is full */
    if (sector_write_pos >= ENTRIES_PER_SECTOR) {
        if (advance_sector(log) != EKK_OK) {
            return 0xFFFFFFFF;
        }
    }

    /* Calculate write address */
    uint32_t offset = (current_sector * ENTRIES_PER_SECTOR + sector_write_pos)
                      * sizeof(ekk_flash_entry_t);
    uint32_t address = log->base_address + offset;

    /* Write entry */
    if (ekk_hal_flash_write(address, entry, sizeof(*entry)) != EKK_OK) {
        return 0xFFFFFFFF;
    }

    /* Update state */
    sector_write_pos++;
    sector_entry_counts[current_sector]++;
    log->write_head++;
    log->total_writes++;

    return offset;
}

ekk_error_t ekk_flash_log_read(ekk_flash_log_t *log,
                                uint32_t offset,
                                ekk_flash_entry_t *entry) {
    if (!log || !entry) {
        return EKK_ERR_INVALID_ARG;
    }

    uint32_t address = log->base_address + offset;

    ekk_error_t err = ekk_hal_flash_read(address, entry, sizeof(*entry));
    if (err != EKK_OK) {
        return err;
    }

    /* Verify CRC */
    if (!ekk_flash_entry_verify(entry)) {
        log->crc_errors++;
        return EKK_ERR_INVALID_ARG;
    }

    return EKK_OK;
}

/* ============================================================================
 * WEAR-LEVELING STATISTICS
 * ============================================================================ */

/**
 * @brief Get wear-leveling statistics
 */
void ekk_flash_log_wear_stats(ekk_flash_log_t *log,
                               uint32_t *min_erases,
                               uint32_t *max_erases,
                               uint32_t *avg_erases) {
    if (!log) return;

    uint32_t min = UINT32_MAX;
    uint32_t max = 0;
    uint64_t sum = 0;

    for (uint32_t i = 0; i < log->sector_count; i++) {
        uint32_t count = sector_erase_counts[i];
        if (count < min) min = count;
        if (count > max) max = count;
        sum += count;
    }

    if (min_erases) *min_erases = min;
    if (max_erases) *max_erases = max;
    if (avg_erases) *avg_erases = (uint32_t)(sum / log->sector_count);
}

/**
 * @brief Check if flash is near end of life
 *
 * @return Remaining life percentage (0-100)
 */
uint32_t ekk_flash_log_remaining_life(ekk_flash_log_t *log) {
    if (!log) return 0;

    uint32_t max_erases = 0;
    for (uint32_t i = 0; i < log->sector_count; i++) {
        if (sector_erase_counts[i] > max_erases) {
            max_erases = sector_erase_counts[i];
        }
    }

    if (max_erases >= FLASH_ENDURANCE) {
        return 0;
    }

    return 100 - (max_erases * 100 / FLASH_ENDURANCE);
}
