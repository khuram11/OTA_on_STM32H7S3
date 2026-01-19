/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : ota_flash_manager.h
  * @brief          : OTA firmware flash management
  ******************************************************************************
  * @attention
  *
  * This module provides high-level OTA firmware management functions that
  * integrate with the XSPI flash writer for safe XIP operation.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __OTA_FLASH_MANAGER_H
#define __OTA_FLASH_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7rsxx_hal.h"

/* OTA firmware storage configuration */
#define OTA_FIRMWARE_BANK_A_ADDR    0x70000000  /* Current running firmware */
#define OTA_FIRMWARE_BANK_B_ADDR    0x70100000  /* OTA download target (1MB offset) */
#define OTA_FIRMWARE_MAX_SIZE       0x00100000  /* 1MB per bank */

/* Status codes */
typedef enum {
    OTA_FLASH_OK       = 0x00,
    OTA_FLASH_ERROR    = 0x01,
    OTA_FLASH_CRC_FAIL = 0x02,
    OTA_FLASH_SIZE_ERR = 0x03
} OTA_Flash_Status;

/* OTA firmware header (place at start of each firmware binary) */
typedef struct {
    uint32_t magic;           /* 0xDEADBEEF */
    uint32_t version;         /* Firmware version */
    uint32_t size;            /* Firmware size (excluding header) */
    uint32_t crc32;           /* CRC32 of firmware data */
    uint32_t reserved[4];     /* Reserved for future use */
} OTA_FirmwareHeader_t;

#define OTA_FIRMWARE_MAGIC  0xDEADBEEF

/* Public API */
OTA_Flash_Status OTA_Flash_Init(void);
OTA_Flash_Status OTA_Flash_WriteFirmware(uint8_t *firmware_data, uint32_t size);
OTA_Flash_Status OTA_Flash_VerifyFirmware(uint32_t flash_address);
OTA_Flash_Status OTA_Flash_ReadFirmware(uint32_t flash_address, uint8_t *buffer, uint32_t size);
void OTA_Flash_SwitchToNewFirmware(void);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_FLASH_MANAGER_H */
