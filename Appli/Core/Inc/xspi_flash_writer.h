/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : xspi_flash_writer.h
  * @brief          : XSPI flash write functions (RAM-based for XIP safety)
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __XSPI_FLASH_WRITER_H
#define __XSPI_FLASH_WRITER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7rsxx_hal.h"

/* Status codes */
typedef enum {
    XSPI_FLASH_OK       = 0x00,
    XSPI_FLASH_ERROR    = 0x01,
    XSPI_FLASH_BUSY     = 0x02,
    XSPI_FLASH_TIMEOUT  = 0x03
} XSPI_Flash_Status;

/* External XSPI handle - must be defined in main.c or provided by Boot */
extern XSPI_HandleTypeDef hxspi2;

/* RAM-based flash write functions */
XSPI_Flash_Status XSPI_Flash_Init_FromXIP(void);
XSPI_Flash_Status XSPI_Flash_EraseSector(uint32_t sector_address);
XSPI_Flash_Status XSPI_Flash_WritePage(uint32_t address, uint8_t *data, uint32_t size);
XSPI_Flash_Status XSPI_Flash_Read(uint32_t address, uint8_t *data, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* __XSPI_FLASH_WRITER_H */
