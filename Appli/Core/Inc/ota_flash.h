/**
 ******************************************************************************
 * @file    ota_flash.h
 * @brief   OTA Support for Application
 ******************************************************************************
 */

#ifndef OTA_FLASH_H
#define OTA_FLASH_H

#include "main.h"
#include <stdint.h>

/*============================================================================*/
/*                    SHARED DEFINITIONS (Must match Boot!)                   */
/*============================================================================*/

#define OTA_MAGIC               0x4F544131
#define OTA_HEADER_SIZE         16

/* OTA mailbox in AXI SRAM */
#define OTA_SRAM_BASE           0x2406C000
#define OTA_SRAM_SIZE           0x00020000  /* 128KB */
#define OTA_MAX_FW_SIZE         (OTA_SRAM_SIZE - 32)

/*============================================================================*/
/*                          STATUS CODES                                      */
/*============================================================================*/

typedef enum {
    OTA_FLASH_OK = 0,
    OTA_FLASH_ERROR,
    OTA_FLASH_INVALID_PARAM,
    OTA_FLASH_SIZE_ERROR
} OTA_Flash_Status_t;

/*============================================================================*/
/*                          FUNCTIONS                                         */
/*============================================================================*/

/**
 * @brief  Apply OTA update
 * @param  fwData: Firmware with OTA header
 * @param  fwSize: Total size including header
 * @retval Does not return - resets system
 */
OTA_Flash_Status_t OTA_Flash_ApplyUpdate(uint8_t *fwData, uint32_t fwSize);

#endif /* OTA_FLASH_H */
