/**
 ******************************************************************************
 * @file    ota_flash.c
 * @brief   OTA Support for Application - STM32H7RS
 ******************************************************************************
 */

#include "ota_flash.h"
#include <string.h>
#include <stdio.h>

/*============================================================================*/
/*                          DEFINITIONS                                       */
/*============================================================================*/

#define BOOT_FLAG_UPDATE        0x55AA55AA

typedef struct {
    uint32_t magic;
    uint32_t fwSize;
    uint32_t expectedCRC;
    uint32_t version;
    uint8_t  fwData[];
} OTA_Mailbox_t;

#define OTA_MAILBOX             ((OTA_Mailbox_t *)OTA_SRAM_BASE)

/*============================================================================*/
/*                          PRIVATE FUNCTIONS                                 */
/*============================================================================*/

/**
 * @brief  Enable backup domain for STM32H7RS
 */
static void OTA_EnableBackupDomain(void)
{
    /* Enable SBS clock */
    RCC->APB4ENR |= RCC_APB4ENR_SBSEN;
    __DSB();

    /* Enable backup domain access via PWR->CR1 */
    PWR->CR1 |= PWR_CR1_DBP;

    /* Wait until accessible */
    while ((PWR->CR1 & PWR_CR1_DBP) == 0);

    /* Small delay */
    for (volatile int i = 0; i < 1000; i++);
}

/**
 * @brief  Set boot flag in TAMP backup register
 */
static void OTA_SetBootFlag(void)
{
    OTA_EnableBackupDomain();
    TAMP->BKP0R = BOOT_FLAG_UPDATE;
}

/*============================================================================*/
/*                          PUBLIC FUNCTIONS                                  */
/*============================================================================*/

OTA_Flash_Status_t OTA_Flash_ApplyUpdate(uint8_t *fwData, uint32_t fwSize)
{
    uint32_t magic, size, crc, version;

    printf("\r\n========================================\r\n");
    printf("       APPLYING OTA UPDATE\r\n");
    printf("========================================\r\n");

    /* Validate */
    if (fwData == NULL || fwSize <= OTA_HEADER_SIZE)
    {
        printf("[OTA] ERROR: Invalid parameters\r\n");
        return OTA_FLASH_INVALID_PARAM;
    }

    /* Parse header */
    memcpy(&magic, &fwData[0], 4);
    memcpy(&size, &fwData[4], 4);
    memcpy(&crc, &fwData[8], 4);
    memcpy(&version, &fwData[12], 4);

    printf("[OTA] Firmware:\r\n");
    printf("      Magic: 0x%08lX\r\n", magic);
    printf("      Size: %lu bytes\r\n", size);
    printf("      CRC: 0x%08lX\r\n", crc);
    printf("      Version: 0x%08lX\r\n", version);

    if (magic != OTA_MAGIC)
    {
        printf("[OTA] ERROR: Invalid magic!\r\n");
        return OTA_FLASH_INVALID_PARAM;
    }

    if (size > OTA_MAX_FW_SIZE)
    {
        printf("[OTA] ERROR: Firmware too large!\r\n");
        printf("      Max: %lu bytes\r\n", (uint32_t)OTA_MAX_FW_SIZE);
        return OTA_FLASH_SIZE_ERROR;
    }

    /* Copy to AXI SRAM mailbox */
    printf("[OTA] Copying to mailbox at 0x%08lX...\r\n", (uint32_t)OTA_SRAM_BASE);

    OTA_MAILBOX->magic = magic;
    OTA_MAILBOX->fwSize = size;
    OTA_MAILBOX->expectedCRC = crc;
    OTA_MAILBOX->version = version;

    memcpy(OTA_MAILBOX->fwData, &fwData[OTA_HEADER_SIZE], size);

    printf("[OTA] Copy complete (%lu bytes)\r\n", size);

    /* Verify copy */
    printf("[OTA] Verifying copy...\r\n");
    if (memcmp(OTA_MAILBOX->fwData, &fwData[OTA_HEADER_SIZE], size) != 0)
    {
        printf("[OTA] ERROR: Copy verification failed!\r\n");
        return OTA_FLASH_ERROR;
    }
    printf("[OTA] Copy verified OK\r\n");

    /* Set boot flag */
    printf("[OTA] Setting boot flag...\r\n");
    OTA_SetBootFlag();

    /* Verify flag was set */
    OTA_EnableBackupDomain();  /* Make sure we can read */
    if (TAMP->BKP0R != BOOT_FLAG_UPDATE)
    {
        printf("[OTA] ERROR: Boot flag not set!\r\n");
        printf("      Read back: 0x%08lX\r\n", TAMP->BKP0R);
        return OTA_FLASH_ERROR;
    }
    printf("[OTA] Boot flag set: 0x%08lX\r\n", TAMP->BKP0R);

    /* Reset */
    printf("\r\n========================================\r\n");
    printf("       RESETTING SYSTEM\r\n");
    printf("========================================\r\n\r\n");

    HAL_Delay(100);

    NVIC_SystemReset();

    while (1);

    return OTA_FLASH_OK;
}
