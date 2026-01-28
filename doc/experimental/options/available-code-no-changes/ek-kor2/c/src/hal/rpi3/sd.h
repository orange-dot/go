/**
 * @file sd.h
 * @brief SD Card Driver for Raspberry Pi 3B+ (BCM2837B0 EMMC)
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * Low-level SD card driver using polling mode.
 * Supports reading/writing 512-byte blocks.
 */

#ifndef RPI3_SD_H
#define RPI3_SD_H

#include <stdint.h>

/* Block size (SD cards use 512-byte blocks) */
#define SD_BLOCK_SIZE           512

/* Error codes */
#define SD_OK                   0
#define SD_ERR_TIMEOUT          -1
#define SD_ERR_CMD              -2
#define SD_ERR_DATA             -3
#define SD_ERR_NO_CARD          -4
#define SD_ERR_UNSUPPORTED      -5
#define SD_ERR_INIT             -6

/* MBR Partition types */
#define MBR_PART_TYPE_EMPTY     0x00
#define MBR_PART_TYPE_FAT32_LBA 0x0C
#define MBR_PART_TYPE_LINUX     0x83
#define MBR_PART_TYPE_EKKFS     0xEE    /* Custom type for EKKFS */

/**
 * @brief MBR Partition Entry (16 bytes)
 */
typedef struct {
    uint8_t  status;            /* 0x80 = bootable, 0x00 = inactive */
    uint8_t  chs_first[3];      /* CHS address of first sector (legacy) */
    uint8_t  type;              /* Partition type */
    uint8_t  chs_last[3];       /* CHS address of last sector (legacy) */
    uint32_t lba_start;         /* LBA start sector */
    uint32_t sector_count;      /* Number of sectors */
} __attribute__((packed)) mbr_partition_t;

/**
 * @brief Master Boot Record (512 bytes)
 */
typedef struct {
    uint8_t  bootcode[446];     /* Boot code */
    mbr_partition_t partitions[4];  /* 4 partition entries */
    uint16_t signature;         /* 0xAA55 */
} __attribute__((packed)) mbr_t;

/**
 * @brief SD card information
 */
typedef struct {
    uint32_t rca;               /* Relative Card Address */
    uint32_t ocr;               /* Operating Conditions Register */
    uint32_t csd[4];            /* Card-Specific Data */
    uint32_t cid[4];            /* Card Identification */
    uint32_t total_blocks;      /* Total 512-byte blocks */
    uint32_t block_size;        /* Block size (always 512) */
    int      is_sdhc;           /* 1 = SDHC/SDXC, 0 = SDSC */
    int      is_initialized;    /* 1 = card initialized */
} sd_card_info_t;

/**
 * @brief Partition information
 */
typedef struct {
    uint32_t lba_start;         /* Starting LBA */
    uint32_t sector_count;      /* Number of sectors */
    uint8_t  type;              /* Partition type */
    int      is_valid;          /* 1 = valid partition */
} sd_partition_t;

/**
 * @brief Initialize the SD card controller
 *
 * Initializes the EMMC controller, detects the SD card,
 * and brings it to transfer state.
 *
 * @return SD_OK on success, error code on failure
 */
int sd_init(void);

/**
 * @brief Read blocks from the SD card
 *
 * @param lba Logical Block Address (sector number)
 * @param buffer Destination buffer (must be 512*count bytes)
 * @param count Number of blocks to read
 * @return SD_OK on success, error code on failure
 */
int sd_read_blocks(uint32_t lba, void *buffer, uint32_t count);

/**
 * @brief Write blocks to the SD card
 *
 * @param lba Logical Block Address (sector number)
 * @param buffer Source buffer (must be 512*count bytes)
 * @param count Number of blocks to write
 * @return SD_OK on success, error code on failure
 */
int sd_write_blocks(uint32_t lba, const void *buffer, uint32_t count);

/**
 * @brief Read a single block from the SD card
 *
 * @param lba Logical Block Address
 * @param buffer Destination buffer (must be 512 bytes)
 * @return SD_OK on success, error code on failure
 */
int sd_read_block(uint32_t lba, void *buffer);

/**
 * @brief Write a single block to the SD card
 *
 * @param lba Logical Block Address
 * @param buffer Source buffer (must be 512 bytes)
 * @return SD_OK on success, error code on failure
 */
int sd_write_block(uint32_t lba, const void *buffer);

/**
 * @brief Get SD card information
 *
 * @return Pointer to card info structure, or NULL if not initialized
 */
const sd_card_info_t* sd_get_info(void);

/**
 * @brief Parse MBR and find partitions
 *
 * Reads the MBR (block 0) and parses the partition table.
 *
 * @param partitions Array of 4 partition structures to fill
 * @return SD_OK on success, error code on failure
 */
int sd_parse_mbr(sd_partition_t partitions[4]);

/**
 * @brief Find EKKFS partition
 *
 * Searches for a partition with type MBR_PART_TYPE_EKKFS (0xEE).
 *
 * @param partition Pointer to partition structure to fill
 * @return SD_OK if found, SD_ERR_NO_CARD if not found
 */
int sd_find_ekkfs_partition(sd_partition_t *partition);

/**
 * @brief Check if SD card is initialized
 *
 * @return 1 if initialized, 0 if not
 */
int sd_is_initialized(void);

#endif /* RPI3_SD_H */
