/**
 ******************************************************************************
 * @file    ota_bootloader.h
 * @brief   OTA Flash Writer for Bootloader
 ******************************************************************************
 */

#ifndef OTA_BOOTLOADER_H
#define OTA_BOOTLOADER_H

#include "main.h"
#include <stdint.h>

/*============================================================================*/
/*                    SHARED DEFINITIONS (Must match Appli!)                  */
/*============================================================================*/

#define OTA_MAGIC               0x4F544131  /* "OTA1" */
#define OTA_HEADER_SIZE         16

/* Boot flags in RTC backup register */
#define BOOT_FLAG_NORMAL        0x00000000
#define BOOT_FLAG_UPDATE        0x55AA55AA

/* Application slots */
#define SLOT_A_FLASH_ADDR       0x00000000  /* Flash internal address */
#define SLOT_B_FLASH_ADDR       0x01000000  /* 16MB offset */
#define SLOT_A_CPU_ADDR         0x70000000  /* Memory-mapped address */
#define SLOT_B_CPU_ADDR         0x71000000

/* OTA mailbox in AXI SRAM (must match Appli!) */
#define OTA_SRAM_BASE           0x2406C000
#define OTA_SRAM_SIZE           0x00020000  /* 128KB */

/*============================================================================*/
/*                          STATUS CODES                                      */
/*============================================================================*/

typedef enum {
    OTA_BOOT_OK = 0,
    OTA_BOOT_ERROR,
    OTA_BOOT_NO_UPDATE,
    OTA_BOOT_INVALID_FW,
    OTA_BOOT_FLASH_ERROR,
    OTA_BOOT_VERIFY_ERROR
} OTA_Boot_Status_t;

/*============================================================================*/
/*                          FUNCTIONS                                         */
/*============================================================================*/

/**
 * @brief  Process OTA update if pending
 * @note   Call BEFORE MX_EXTMEM_MANAGER_Init() or after disabling mapped mode
 * @retval Jump address (SLOT_A_CPU_ADDR or SLOT_B_CPU_ADDR)
 */
uint32_t OTA_Bootloader_Process(void);

/**
 * @brief  Jump to application
 * @param  appAddr: 0x70000000 (Slot A) or 0x71000000 (Slot B)
 */
void OTA_Bootloader_JumpToApp(uint32_t appAddr);

#endif /* OTA_BOOTLOADER_H */
