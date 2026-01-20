/**
 ******************************************************************************
 * @file    ota_flash.h
 * @brief   OTA Flash Writer for STM32H7S3 + MX25UW25645G
 ******************************************************************************
 */

#ifndef OTA_FLASH_H
#define OTA_FLASH_H

#include "main.h"
#include <stdint.h>

/*============================================================================*/
/*                              DEFINITIONS                                   */
/*============================================================================*/

/* Flash geometry for MX25UW25645G */
#define FLASH_PAGE_SIZE         256
#define FLASH_SECTOR_SIZE_4K    0x1000      /* 4KB */
#define FLASH_SECTOR_SIZE_64K   0x10000     /* 64KB - use this for faster erase */
#define FLASH_TOTAL_SIZE        0x2000000   /* 32MB */

/* Memory mapping */
#define EXTFLASH_BASE_ADDR      0x70000000  /* XIP base address */
#define SLOT_A_CPU_ADDR         0x70000000  /* Current app */
#define SLOT_B_CPU_ADDR         0x71000000  /* Update slot */
#define SLOT_A_FLASH_ADDR       0x00000000  /* Flash internal address */
#define SLOT_B_FLASH_ADDR       0x01000000  /* Flash internal address (16MB offset) */

/* OTA Header */
#define OTA_HEADER_SIZE         16
#define OTA_MAGIC               0x4F544131  /* "OTA1" */

/* Boot flags (stored in RTC backup register) */
#define BOOT_FLAG_NORMAL        0x00000000
#define BOOT_FLAG_UPDATE        0x55AA55AA
#define BOOT_FLAG_VERIFY        0xAA55AA55

/* Status codes */
typedef enum {
    OTA_FLASH_OK = 0,
    OTA_FLASH_ERROR,
    OTA_FLASH_ERASE_ERROR,
    OTA_FLASH_PROGRAM_ERROR,
    OTA_FLASH_VERIFY_ERROR,
    OTA_FLASH_TIMEOUT,
    OTA_FLASH_INVALID_PARAM
} OTA_Flash_Status_t;

/* Update info structure - shared between app and bootloader */
typedef struct {
    uint32_t magic;             /* Must be OTA_MAGIC */
    uint32_t srcAddress;        /* Source address in RAM */
    uint32_t dstFlashAddr;      /* Destination flash address (internal) */
    uint32_t size;              /* Firmware size (excluding header) */
    uint32_t crc;               /* Expected CRC */
    uint32_t version;           /* Firmware version */
} OTA_UpdateInfo_t;

/*============================================================================*/
/*                          FUNCTION PROTOTYPES                               */
/*============================================================================*/

/**
 * @brief  Initialize flash writer (configure XSPI if needed)
 * @retval OTA_FLASH_OK on success
 */
OTA_Flash_Status_t OTA_Flash_Init(void);

/**
 * @brief  Write firmware to external flash slot B
 * @param  fwData: Pointer to firmware data (with OTA header)
 * @param  fwSize: Total size including header
 * @retval OTA_FLASH_OK on success
 * @note   This function runs from RAM and disables interrupts!
 */
OTA_Flash_Status_t OTA_Flash_WriteFirmware(uint8_t *fwData, uint32_t fwSize);

/**
 * @brief  Verify written firmware by reading back and checking CRC
 * @retval OTA_FLASH_OK if verification passes
 */
OTA_Flash_Status_t OTA_Flash_VerifySlotB(void);

/**
 * @brief  Set boot flag to boot from slot B on next reset
 */
void OTA_Flash_SetBootFlagUpdate(void);

/**
 * @brief  Clear boot flag (normal boot)
 */
void OTA_Flash_ClearBootFlag(void);

/**
 * @brief  Get current boot flag
 */
uint32_t OTA_Flash_GetBootFlag(void);

/**
 * @brief  Trigger system reset to apply update
 */
void OTA_Flash_ResetToUpdate(void);

/**
 * @brief  Complete OTA process: write, verify, set flag, reset
 * @param  fwData: Pointer to firmware data (with OTA header)
 * @param  fwSize: Total size including header
 * @retval OTA_FLASH_OK on success (but won't return - resets!)
 */
OTA_Flash_Status_t OTA_Flash_ApplyUpdate(uint8_t *fwData, uint32_t fwSize);

#endif /* OTA_FLASH_H */
