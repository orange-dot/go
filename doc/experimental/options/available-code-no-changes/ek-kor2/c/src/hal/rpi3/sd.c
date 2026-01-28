/**
 * @file sd.c
 * @brief SD Card Driver for Raspberry Pi 3B+ (BCM2837B0 EMMC)
 *
 * @copyright Copyright (c) 2026 Elektrokombinacija
 * @license MIT
 *
 * This driver implements SD card access via the BCM2837 EMMC controller.
 * It uses polling mode for simplicity and reliability.
 *
 * References:
 * - BCM2835 ARM Peripherals (applies to BCM2837)
 * - SD Physical Layer Simplified Specification v6.00
 */

#include "sd.h"
#include "rpi3_hw.h"
#include "uart.h"
#include "mailbox.h"
#include <string.h>

/* Debug output control */
#define SD_DEBUG                1

#if SD_DEBUG
#define sd_debug(...)           uart_printf(__VA_ARGS__)
#else
#define sd_debug(...)
#endif

/* Timeouts */
#define SD_TIMEOUT_CMD          1000000     /* Command timeout (cycles) */
#define SD_TIMEOUT_DATA         5000000     /* Data timeout (cycles) */
#define SD_TIMEOUT_RESET        100000      /* Reset timeout (cycles) */

/* EMMC clock settings */
#define EMMC_BASE_CLOCK_HZ      41666666    /* 41.67 MHz base clock */
#define SD_CLOCK_IDENT_HZ       400000      /* 400 kHz identification */
#define SD_CLOCK_DATA_HZ        25000000    /* 25 MHz data transfer */

/* Voltage and capacity */
#define SD_VDD_27_36            0x00FF8000  /* 2.7-3.6V */
#define SD_OCR_HCS              (1 << 30)   /* Host Capacity Support (SDHC) */
#define SD_OCR_CCS              (1 << 30)   /* Card Capacity Status */
#define SD_OCR_BUSY             (1 << 31)   /* Card power up busy */

/* Static card info */
static sd_card_info_t g_card;

/* ============================================================================
 * GPIO Configuration
 * ============================================================================ */

/**
 * @brief Configure GPIO pins for SD card (pins 48-53 for ALT3)
 */
static void sd_gpio_init(void)
{
    /* SD card uses GPIO 48-53 in ALT3 mode */
    /* GPIO 48 = CLK, 49 = CMD, 50-53 = DAT0-3 */

    /* Configure GPFSEL4 (GPIO 40-49) */
    uint32_t fsel4 = mmio_read32(GPFSEL4);
    fsel4 &= ~(7 << 24);    /* GPIO 48 */
    fsel4 |= (GPIO_FUNC_ALT3 << 24);
    fsel4 &= ~(7 << 27);    /* GPIO 49 */
    fsel4 |= (GPIO_FUNC_ALT3 << 27);
    mmio_write32(GPFSEL4, fsel4);

    /* Configure GPFSEL5 (GPIO 50-53) */
    uint32_t fsel5 = mmio_read32(GPFSEL5);
    fsel5 &= ~(7 << 0);     /* GPIO 50 */
    fsel5 |= (GPIO_FUNC_ALT3 << 0);
    fsel5 &= ~(7 << 3);     /* GPIO 51 */
    fsel5 |= (GPIO_FUNC_ALT3 << 3);
    fsel5 &= ~(7 << 6);     /* GPIO 52 */
    fsel5 |= (GPIO_FUNC_ALT3 << 6);
    fsel5 &= ~(7 << 9);     /* GPIO 53 */
    fsel5 |= (GPIO_FUNC_ALT3 << 9);
    mmio_write32(GPFSEL5, fsel5);

    /* No pull-up/down for now (card has internal pull-ups) */
    mmio_write32(GPPUD, 0);
    delay_cycles(150);
    mmio_write32(GPPUDCLK1, (0x3F << 16)); /* GPIO 48-53 */
    delay_cycles(150);
    mmio_write32(GPPUDCLK1, 0);
}

/* ============================================================================
 * Clock Control
 * ============================================================================ */

/**
 * @brief Set EMMC clock to specified frequency
 */
static int sd_set_clock(uint32_t freq_hz)
{
    /* Wait for command inhibit to clear */
    uint32_t timeout = SD_TIMEOUT_RESET;
    while ((mmio_read32(EMMC_STATUS) & (EMMC_STATUS_CMD_INHIBIT | EMMC_STATUS_DAT_INHIBIT)) && timeout--) {
        delay_cycles(10);
    }
    if (timeout == 0) {
        sd_debug("SD: Clock set timeout waiting for inhibit\n");
        return SD_ERR_TIMEOUT;
    }

    /* Disable clock */
    uint32_t ctrl1 = mmio_read32(EMMC_CONTROL1);
    ctrl1 &= ~EMMC_C1_CLK_EN;
    mmio_write32(EMMC_CONTROL1, ctrl1);
    delay_cycles(1000);

    /* Calculate divisor */
    uint32_t divisor = EMMC_BASE_CLOCK_HZ / freq_hz;
    if (divisor < 2) divisor = 2;
    if (divisor > 2046) divisor = 2046;
    divisor /= 2;

    /* Set divisor (bits 15:8 = high, bits 7:6 = low) */
    uint32_t div_lo = (divisor & 0x03) << 6;
    uint32_t div_hi = ((divisor >> 2) & 0xFF) << 8;

    ctrl1 = mmio_read32(EMMC_CONTROL1);
    ctrl1 &= ~0xFFE0;
    ctrl1 |= div_lo | div_hi;
    mmio_write32(EMMC_CONTROL1, ctrl1);
    delay_cycles(1000);

    /* Enable internal clock */
    ctrl1 |= EMMC_C1_CLK_INTLEN;
    mmio_write32(EMMC_CONTROL1, ctrl1);

    /* Wait for clock stable */
    timeout = SD_TIMEOUT_RESET;
    while (!(mmio_read32(EMMC_CONTROL1) & EMMC_C1_CLK_STABLE) && timeout--) {
        delay_cycles(10);
    }
    if (timeout == 0) {
        sd_debug("SD: Clock not stable\n");
        return SD_ERR_TIMEOUT;
    }

    /* Enable SD clock */
    ctrl1 = mmio_read32(EMMC_CONTROL1);
    ctrl1 |= EMMC_C1_CLK_EN;
    mmio_write32(EMMC_CONTROL1, ctrl1);
    delay_cycles(1000);

    sd_debug("SD: Clock set to %lu Hz (div=%lu)\n", freq_hz, divisor * 2);
    return SD_OK;
}

/* ============================================================================
 * Command Execution
 * ============================================================================ */

/**
 * @brief Build command transfer mode register value
 */
static uint32_t sd_build_cmdtm(uint32_t cmd, int is_data, int data_dir_read, int multi_block)
{
    uint32_t cmdtm = (cmd & 0x3F) << 24;

    /* Response type based on command */
    switch (cmd) {
        case SD_CMD_GO_IDLE:
            /* No response */
            break;
        case SD_CMD_ALL_SEND_CID:
        case 9:  /* SEND_CSD */
            cmdtm |= EMMC_CMD_RSPNS_TYPE_136;
            break;
        case SD_CMD_SEND_OP_COND & 0x3F:
            cmdtm |= EMMC_CMD_RSPNS_TYPE_48;
            break;
        case SD_CMD_SELECT_CARD:
            cmdtm |= EMMC_CMD_RSPNS_TYPE_48B | EMMC_CMD_CRCCHK_EN | EMMC_CMD_IXCHK_EN;
            break;
        case SD_CMD_STOP_TRANSMISSION:
            cmdtm |= EMMC_CMD_RSPNS_TYPE_48B | EMMC_CMD_CRCCHK_EN | EMMC_CMD_IXCHK_EN;
            break;
        default:
            cmdtm |= EMMC_CMD_RSPNS_TYPE_48 | EMMC_CMD_CRCCHK_EN | EMMC_CMD_IXCHK_EN;
            break;
    }

    if (is_data) {
        cmdtm |= EMMC_CMD_ISDATA;
        if (data_dir_read) {
            cmdtm |= EMMC_CMD_TM_DAT_DIR_CH;
        }
        if (multi_block) {
            cmdtm |= EMMC_CMD_TM_MULTI_BLOCK | EMMC_CMD_TM_BLKCNT_EN;
        }
    }

    return cmdtm;
}

/**
 * @brief Execute an SD command
 */
static int sd_send_cmd(uint32_t cmd, uint32_t arg, uint32_t *response)
{
    int is_acmd = (cmd & 0x80) != 0;
    uint32_t real_cmd = cmd & 0x3F;

    /* For ACMD, first send CMD55 */
    if (is_acmd) {
        int ret = sd_send_cmd(SD_CMD_APP_CMD, g_card.rca << 16, NULL);
        if (ret != SD_OK) {
            return ret;
        }
    }

    /* Wait for command inhibit to clear */
    uint32_t timeout = SD_TIMEOUT_CMD;
    while ((mmio_read32(EMMC_STATUS) & EMMC_STATUS_CMD_INHIBIT) && timeout--) {
        delay_cycles(10);
    }
    if (timeout == 0) {
        sd_debug("SD: CMD%d timeout (cmd inhibit)\n", real_cmd);
        return SD_ERR_TIMEOUT;
    }

    /* Clear interrupt flags */
    mmio_write32(EMMC_INTERRUPT, 0xFFFFFFFF);

    /* Set argument */
    mmio_write32(EMMC_ARG1, arg);

    /* Build and send command */
    uint32_t cmdtm = sd_build_cmdtm(real_cmd, 0, 0, 0);
    mmio_write32(EMMC_CMDTM, cmdtm);

    /* Wait for command complete or error */
    timeout = SD_TIMEOUT_CMD;
    uint32_t irpt;
    while (timeout--) {
        irpt = mmio_read32(EMMC_INTERRUPT);
        if (irpt & (EMMC_INT_CMD_DONE | EMMC_INT_ERROR_MASK)) {
            break;
        }
        delay_cycles(10);
    }

    if (timeout == 0) {
        sd_debug("SD: CMD%d timeout (no response)\n", real_cmd);
        return SD_ERR_TIMEOUT;
    }

    if (irpt & EMMC_INT_ERROR_MASK) {
        sd_debug("SD: CMD%d error, INTERRUPT=0x%08lx\n", real_cmd, irpt);
        mmio_write32(EMMC_INTERRUPT, irpt);
        return SD_ERR_CMD;
    }

    /* Clear command done */
    mmio_write32(EMMC_INTERRUPT, EMMC_INT_CMD_DONE);

    /* Get response */
    if (response) {
        response[0] = mmio_read32(EMMC_RESP0);
        response[1] = mmio_read32(EMMC_RESP1);
        response[2] = mmio_read32(EMMC_RESP2);
        response[3] = mmio_read32(EMMC_RESP3);
    }

    return SD_OK;
}

/**
 * @brief Execute a data transfer command
 */
static int sd_send_data_cmd(uint32_t cmd, uint32_t arg, void *buffer, uint32_t blocks, int is_write)
{
    /* Wait for data inhibit to clear */
    uint32_t timeout = SD_TIMEOUT_CMD;
    while ((mmio_read32(EMMC_STATUS) & EMMC_STATUS_DAT_INHIBIT) && timeout--) {
        delay_cycles(10);
    }
    if (timeout == 0) {
        sd_debug("SD: Data cmd timeout (dat inhibit)\n");
        return SD_ERR_TIMEOUT;
    }

    /* Clear interrupts */
    mmio_write32(EMMC_INTERRUPT, 0xFFFFFFFF);

    /* Set block size and count */
    mmio_write32(EMMC_BLKSIZECNT, (blocks << 16) | SD_BLOCK_SIZE);

    /* Set argument */
    mmio_write32(EMMC_ARG1, arg);

    /* Build command */
    uint32_t cmdtm = sd_build_cmdtm(cmd, 1, !is_write, blocks > 1);
    mmio_write32(EMMC_CMDTM, cmdtm);

    /* Wait for command complete */
    timeout = SD_TIMEOUT_CMD;
    uint32_t irpt;
    while (timeout--) {
        irpt = mmio_read32(EMMC_INTERRUPT);
        if (irpt & (EMMC_INT_CMD_DONE | EMMC_INT_ERROR_MASK)) {
            break;
        }
        delay_cycles(10);
    }

    if (timeout == 0) {
        sd_debug("SD: Data cmd timeout (no cmd response)\n");
        return SD_ERR_TIMEOUT;
    }

    if (irpt & EMMC_INT_ERROR_MASK) {
        sd_debug("SD: Data cmd error, INTERRUPT=0x%08lx\n", irpt);
        mmio_write32(EMMC_INTERRUPT, irpt);
        return SD_ERR_CMD;
    }

    mmio_write32(EMMC_INTERRUPT, EMMC_INT_CMD_DONE);

    /* Transfer data */
    uint32_t *data32 = (uint32_t *)buffer;
    uint32_t words_per_block = SD_BLOCK_SIZE / 4;

    for (uint32_t blk = 0; blk < blocks; blk++) {
        uint32_t wait_flag = is_write ? EMMC_INT_WRITE_RDY : EMMC_INT_READ_RDY;

        /* Wait for buffer ready */
        timeout = SD_TIMEOUT_DATA;
        while (timeout--) {
            irpt = mmio_read32(EMMC_INTERRUPT);
            if (irpt & (wait_flag | EMMC_INT_ERROR_MASK)) {
                break;
            }
            delay_cycles(10);
        }

        if (timeout == 0) {
            sd_debug("SD: Data transfer timeout\n");
            return SD_ERR_TIMEOUT;
        }

        if (irpt & EMMC_INT_ERROR_MASK) {
            sd_debug("SD: Data transfer error, INTERRUPT=0x%08lx\n", irpt);
            mmio_write32(EMMC_INTERRUPT, irpt);
            return SD_ERR_DATA;
        }

        mmio_write32(EMMC_INTERRUPT, wait_flag);

        /* Transfer block */
        for (uint32_t i = 0; i < words_per_block; i++) {
            if (is_write) {
                mmio_write32(EMMC_DATA, data32[i]);
            } else {
                data32[i] = mmio_read32(EMMC_DATA);
            }
        }

        data32 += words_per_block;
    }

    /* Wait for transfer complete */
    timeout = SD_TIMEOUT_DATA;
    while (timeout--) {
        irpt = mmio_read32(EMMC_INTERRUPT);
        if (irpt & (EMMC_INT_DATA_DONE | EMMC_INT_ERROR_MASK)) {
            break;
        }
        delay_cycles(10);
    }

    if (irpt & EMMC_INT_ERROR_MASK) {
        sd_debug("SD: Data done error, INTERRUPT=0x%08lx\n", irpt);
        mmio_write32(EMMC_INTERRUPT, irpt);
        return SD_ERR_DATA;
    }

    mmio_write32(EMMC_INTERRUPT, EMMC_INT_DATA_DONE);

    return SD_OK;
}

/* ============================================================================
 * Card Initialization
 * ============================================================================ */

/**
 * @brief Reset the EMMC controller
 */
static int sd_reset(void)
{
    /* Reset the host controller */
    uint32_t ctrl1 = mmio_read32(EMMC_CONTROL1);
    ctrl1 |= EMMC_C1_SRST_HC;
    mmio_write32(EMMC_CONTROL1, ctrl1);

    /* Wait for reset complete */
    uint32_t timeout = SD_TIMEOUT_RESET;
    while ((mmio_read32(EMMC_CONTROL1) & EMMC_C1_SRST_HC) && timeout--) {
        delay_cycles(10);
    }

    if (timeout == 0) {
        sd_debug("SD: Reset timeout\n");
        return SD_ERR_TIMEOUT;
    }

    sd_debug("SD: Controller reset complete\n");
    return SD_OK;
}

/**
 * @brief Initialize the SD card
 */
int sd_init(void)
{
    int ret;
    uint32_t resp[4];

    memset(&g_card, 0, sizeof(g_card));

    sd_debug("SD: Initializing SD card driver...\n");

    /* Configure GPIO for SD */
    sd_gpio_init();
    sd_debug("SD: GPIO configured\n");

    /* Reset controller */
    ret = sd_reset();
    if (ret != SD_OK) {
        return ret;
    }

    /* Set identification clock (400 kHz) */
    ret = sd_set_clock(SD_CLOCK_IDENT_HZ);
    if (ret != SD_OK) {
        return ret;
    }

    /* Enable interrupts */
    mmio_write32(EMMC_IRPT_EN, 0xFFFFFFFF);
    mmio_write32(EMMC_IRPT_MASK, 0xFFFFFFFF);

    /* Clear pending interrupts */
    mmio_write32(EMMC_INTERRUPT, 0xFFFFFFFF);

    /* Wait for card to be ready */
    delay_cycles(1000000);

    /* CMD0: GO_IDLE_STATE */
    sd_debug("SD: Sending CMD0 (GO_IDLE)\n");
    ret = sd_send_cmd(SD_CMD_GO_IDLE, 0, NULL);
    if (ret != SD_OK) {
        sd_debug("SD: CMD0 failed\n");
        return SD_ERR_NO_CARD;
    }

    /* CMD8: SEND_IF_COND (voltage check) */
    sd_debug("SD: Sending CMD8 (SEND_IF_COND)\n");
    ret = sd_send_cmd(SD_CMD_SEND_IF_COND, 0x1AA, resp);
    if (ret != SD_OK) {
        sd_debug("SD: CMD8 failed - SDSC card or no card\n");
        /* Could be SDSC card, continue */
    } else {
        if ((resp[0] & 0xFFF) != 0x1AA) {
            sd_debug("SD: CMD8 invalid response: 0x%08lx\n", resp[0]);
            return SD_ERR_UNSUPPORTED;
        }
        sd_debug("SD: CMD8 OK - SD v2.0 card\n");
    }

    /* ACMD41: SEND_OP_COND (capacity check) */
    sd_debug("SD: Sending ACMD41 (SEND_OP_COND)\n");
    uint32_t timeout = 100;
    do {
        delay_cycles(100000);
        ret = sd_send_cmd(SD_CMD_SEND_OP_COND, SD_VDD_27_36 | SD_OCR_HCS, resp);
        if (ret != SD_OK) {
            sd_debug("SD: ACMD41 failed\n");
            return SD_ERR_NO_CARD;
        }
    } while (!(resp[0] & SD_OCR_BUSY) && --timeout);

    if (timeout == 0) {
        sd_debug("SD: ACMD41 timeout - card not ready\n");
        return SD_ERR_TIMEOUT;
    }

    g_card.ocr = resp[0];
    g_card.is_sdhc = (resp[0] & SD_OCR_CCS) ? 1 : 0;
    sd_debug("SD: OCR=0x%08lx, SDHC=%d\n", g_card.ocr, g_card.is_sdhc);

    /* CMD2: ALL_SEND_CID */
    sd_debug("SD: Sending CMD2 (ALL_SEND_CID)\n");
    ret = sd_send_cmd(SD_CMD_ALL_SEND_CID, 0, g_card.cid);
    if (ret != SD_OK) {
        sd_debug("SD: CMD2 failed\n");
        return ret;
    }

    /* CMD3: SEND_RELATIVE_ADDR */
    sd_debug("SD: Sending CMD3 (SEND_REL_ADDR)\n");
    ret = sd_send_cmd(SD_CMD_SEND_REL_ADDR, 0, resp);
    if (ret != SD_OK) {
        sd_debug("SD: CMD3 failed\n");
        return ret;
    }
    g_card.rca = resp[0] >> 16;
    sd_debug("SD: RCA=0x%04lx\n", g_card.rca);

    /* Set data transfer clock (25 MHz) */
    ret = sd_set_clock(SD_CLOCK_DATA_HZ);
    if (ret != SD_OK) {
        return ret;
    }

    /* CMD7: SELECT_CARD */
    sd_debug("SD: Sending CMD7 (SELECT_CARD)\n");
    ret = sd_send_cmd(SD_CMD_SELECT_CARD, g_card.rca << 16, resp);
    if (ret != SD_OK) {
        sd_debug("SD: CMD7 failed\n");
        return ret;
    }

    /* ACMD6: SET_BUS_WIDTH (4-bit mode) */
    sd_debug("SD: Sending ACMD6 (SET_BUS_WIDTH 4-bit)\n");
    ret = sd_send_cmd(SD_CMD_SET_BUS_WIDTH, 2, resp);
    if (ret != SD_OK) {
        sd_debug("SD: ACMD6 failed, staying in 1-bit mode\n");
    } else {
        /* Enable 4-bit mode in host */
        uint32_t ctrl0 = mmio_read32(EMMC_CONTROL0);
        ctrl0 |= EMMC_C0_HCTL_DWIDTH;
        mmio_write32(EMMC_CONTROL0, ctrl0);
        sd_debug("SD: 4-bit mode enabled\n");
    }

    g_card.block_size = SD_BLOCK_SIZE;
    g_card.is_initialized = 1;

    /* Calculate capacity (simplified - read from CSD for accurate value) */
    if (g_card.is_sdhc) {
        g_card.total_blocks = 0xFFFFFFFF;  /* Unknown for now */
    } else {
        g_card.total_blocks = 0xFFFFFFFF;
    }

    sd_debug("SD: Card initialized successfully!\n");
    return SD_OK;
}

/* ============================================================================
 * Block I/O
 * ============================================================================ */

int sd_read_block(uint32_t lba, void *buffer)
{
    return sd_read_blocks(lba, buffer, 1);
}

int sd_read_blocks(uint32_t lba, void *buffer, uint32_t count)
{
    if (!g_card.is_initialized) {
        return SD_ERR_NO_CARD;
    }
    if (count == 0) {
        return SD_OK;
    }

    /* For SDSC cards, address is byte offset; for SDHC, block number */
    uint32_t addr = g_card.is_sdhc ? lba : (lba * SD_BLOCK_SIZE);

    uint32_t cmd = (count > 1) ? SD_CMD_READ_MULTIPLE_BLOCK : SD_CMD_READ_SINGLE_BLOCK;
    int ret = sd_send_data_cmd(cmd, addr, buffer, count, 0);

    if (ret != SD_OK) {
        sd_debug("SD: Read failed at LBA %lu\n", lba);
        return ret;
    }

    /* For multi-block, send stop command */
    if (count > 1) {
        sd_send_cmd(SD_CMD_STOP_TRANSMISSION, 0, NULL);
    }

    return SD_OK;
}

int sd_write_block(uint32_t lba, const void *buffer)
{
    return sd_write_blocks(lba, buffer, 1);
}

int sd_write_blocks(uint32_t lba, const void *buffer, uint32_t count)
{
    if (!g_card.is_initialized) {
        return SD_ERR_NO_CARD;
    }
    if (count == 0) {
        return SD_OK;
    }

    /* For SDSC cards, address is byte offset; for SDHC, block number */
    uint32_t addr = g_card.is_sdhc ? lba : (lba * SD_BLOCK_SIZE);

    uint32_t cmd = (count > 1) ? SD_CMD_WRITE_MULTIPLE_BLOCK : SD_CMD_WRITE_SINGLE_BLOCK;
    int ret = sd_send_data_cmd(cmd, addr, (void *)buffer, count, 1);

    if (ret != SD_OK) {
        sd_debug("SD: Write failed at LBA %lu\n", lba);
        return ret;
    }

    /* For multi-block, send stop command */
    if (count > 1) {
        sd_send_cmd(SD_CMD_STOP_TRANSMISSION, 0, NULL);
    }

    return SD_OK;
}

/* ============================================================================
 * Info and MBR
 * ============================================================================ */

const sd_card_info_t* sd_get_info(void)
{
    if (!g_card.is_initialized) {
        return NULL;
    }
    return &g_card;
}

int sd_is_initialized(void)
{
    return g_card.is_initialized;
}

int sd_parse_mbr(sd_partition_t partitions[4])
{
    uint8_t mbr_buf[SD_BLOCK_SIZE];

    /* Read MBR (block 0) */
    int ret = sd_read_block(0, mbr_buf);
    if (ret != SD_OK) {
        sd_debug("SD: Failed to read MBR\n");
        return ret;
    }

    /* Check signature */
    mbr_t *mbr = (mbr_t *)mbr_buf;
    if (mbr->signature != 0xAA55) {
        sd_debug("SD: Invalid MBR signature: 0x%04X\n", mbr->signature);
        return SD_ERR_DATA;
    }

    /* Parse partitions */
    for (int i = 0; i < 4; i++) {
        partitions[i].type = mbr->partitions[i].type;
        partitions[i].lba_start = mbr->partitions[i].lba_start;
        partitions[i].sector_count = mbr->partitions[i].sector_count;
        partitions[i].is_valid = (mbr->partitions[i].type != MBR_PART_TYPE_EMPTY);

        if (partitions[i].is_valid) {
            sd_debug("SD: Partition %d: type=0x%02X, start=%lu, count=%lu\n",
                     i, partitions[i].type, partitions[i].lba_start,
                     partitions[i].sector_count);
        }
    }

    return SD_OK;
}

int sd_find_ekkfs_partition(sd_partition_t *partition)
{
    sd_partition_t parts[4];

    int ret = sd_parse_mbr(parts);
    if (ret != SD_OK) {
        return ret;
    }

    /* Look for EKKFS partition type (0xEE) or Linux type as fallback */
    for (int i = 0; i < 4; i++) {
        if (parts[i].is_valid && parts[i].type == MBR_PART_TYPE_EKKFS) {
            *partition = parts[i];
            sd_debug("SD: Found EKKFS partition %d\n", i);
            return SD_OK;
        }
    }

    /* Fallback: use second partition if it's Linux type */
    if (parts[1].is_valid && parts[1].type == MBR_PART_TYPE_LINUX) {
        *partition = parts[1];
        sd_debug("SD: Using Linux partition 1 as EKKFS\n");
        return SD_OK;
    }

    sd_debug("SD: EKKFS partition not found\n");
    return SD_ERR_NO_CARD;
}
