/**
 ******************************************************************************
 * @file    ota_bootloader.c
 * @brief   OTA Flash Writer for Bootloader - STM32H7S3 + MX25UW25645G
 ******************************************************************************
 */

#include "ota_bootloader.h"
#include "stm32_extmem.h"
#include "stm32_boot_xip.h"  /* For EXTMEM_XIP_IMAGE_OFFSET, EXTMEM_HEADER_OFFSET */
#include <string.h>

/*============================================================================*/
/*                          EXTERNAL REFERENCES                               */
/*============================================================================*/

extern UART_HandleTypeDef huart4;

/*============================================================================*/
/*                          PRIVATE DEFINITIONS                               */
/*============================================================================*/

/*
 * OTA Mailbox location in AXI SRAM
 */
#define OTA_SRAM_BASE           0x2406C000
#define OTA_SRAM_SIZE           0x00020000  /* 128KB */
#define OTA_MAX_FW_SIZE         (OTA_SRAM_SIZE - 32)

/* Flash geometry */
#define FLASH_BLOCK_SIZE_64K    0x10000

/* Mailbox structure */
typedef struct {
    uint32_t magic;
    uint32_t fwSize;
    uint32_t expectedCRC;
    uint32_t version;
    uint8_t  fwData[];
} OTA_Mailbox_t;

#define OTA_MAILBOX             ((OTA_Mailbox_t *)OTA_SRAM_BASE)

/* ExtMemManager memory ID */
#ifndef EXTMEMORY_1
#define EXTMEMORY_1             0
#endif

/* Slot B offset from Slot A (16MB) */
#define SLOT_B_OFFSET           0x01000000

/*============================================================================*/
/*                          PRIVATE FUNCTIONS                                 */
/*============================================================================*/

static void Boot_Print(const char *str)
{
    HAL_UART_Transmit(&huart4, (uint8_t *)str, strlen(str), 500);
}

static void Boot_PrintHex32(const char *prefix, uint32_t val)
{
    char buf[128];  // Increased size
    const char hex[] = "0123456789ABCDEF";

    uint32_t len = strlen(prefix);

    // Safety check
    if (len > 100) len = 100;

    memcpy(buf, prefix, len);

    buf[len++] = '0';
    buf[len++] = 'x';

    for (int i = 7; i >= 0; i--)
    {
        buf[len++] = hex[(val >> (i * 4)) & 0x0F];
    }

    buf[len++] = '\r';
    buf[len++] = '\n';

    HAL_UART_Transmit(&huart4, (uint8_t *)buf, len, 500);
}

static void Boot_EnableBackupDomain(void)
{
    RCC->APB4ENR |= RCC_APB4ENR_SBSEN;
    __DSB();

    PWR->CR1 |= PWR_CR1_DBP;

    while ((PWR->CR1 & PWR_CR1_DBP) == 0);

    for (volatile int i = 0; i < 1000; i++);
}

static uint32_t Boot_GetBootFlag(void)
{
    return TAMP->BKP0R;
}

static void Boot_ClearBootFlag(void)
{
    Boot_EnableBackupDomain();
    TAMP->BKP0R = BOOT_FLAG_NORMAL;
}

static uint32_t Boot_CalculateCRC32(uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;

    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }

    return crc ^ 0xFFFFFFFF;
}

/*============================================================================*/
/*                    FLASH OPERATIONS VIA EXTMEM                             */
/*============================================================================*/

static OTA_Boot_Status_t Boot_WriteFirmwareToFlash(uint32_t flashAddr,
                                                    uint8_t *data,
                                                    uint32_t size)
{
    EXTMEM_StatusTypeDef status;
    uint32_t blockCount;
    uint32_t eraseAddr;

    Boot_Print("[BOOT] Writing firmware to flash\r\n");
    Boot_PrintHex32("       Flash addr: ", flashAddr);
    Boot_PrintHex32("       Size: ", size);

    /* Disable memory-mapped mode */
    Boot_Print("[BOOT] Disabling memory-mapped mode...\r\n");

    status = EXTMEM_MemoryMappedMode(EXTMEMORY_1, EXTMEM_DISABLE);
    if (status != EXTMEM_OK)
    {
        Boot_Print("[BOOT] Note: Mapped mode disable returned ");
        Boot_PrintHex32("", (uint32_t)status);
    }

    /* Erase blocks */
    blockCount = (size + FLASH_BLOCK_SIZE_64K - 1) / FLASH_BLOCK_SIZE_64K;
    Boot_PrintHex32("[BOOT] Erasing blocks: ", blockCount);

    for (uint32_t i = 0; i < blockCount; i++)
    {
        eraseAddr = flashAddr + (i * FLASH_BLOCK_SIZE_64K);

        Boot_Print(".");

        status = EXTMEM_EraseSector(EXTMEMORY_1, eraseAddr, FLASH_BLOCK_SIZE_64K);

        if (status != EXTMEM_OK)
        {
            Boot_Print("\r\n[BOOT] ERASE FAILED!\r\n");
            Boot_PrintHex32("       Address: ", eraseAddr);
            Boot_PrintHex32("       Status: ", (uint32_t)status);
            return OTA_BOOT_FLASH_ERROR;
        }
    }
    Boot_Print(" Done\r\n");

    /* Program data */
    Boot_Print("[BOOT] Programming...\r\n");

    status = EXTMEM_Write(EXTMEMORY_1, flashAddr, data, size);

    if (status != EXTMEM_OK)
    {
        Boot_Print("[BOOT] PROGRAM FAILED!\r\n");
        Boot_PrintHex32("       Status: ", (uint32_t)status);
        return OTA_BOOT_FLASH_ERROR;
    }

    Boot_Print("[BOOT] Programming complete!\r\n");

    /* Re-enable memory-mapped mode */
    Boot_Print("[BOOT] Re-enabling memory-mapped mode...\r\n");

    status = EXTMEM_MemoryMappedMode(EXTMEMORY_1, EXTMEM_ENABLE);
    if (status != EXTMEM_OK)
    {
        Boot_Print("[BOOT] Note: Re-enable mapped mode returned ");
        Boot_PrintHex32("", (uint32_t)status);
    }

    return OTA_BOOT_OK;
}

/*============================================================================*/
/*                          PUBLIC FUNCTIONS                                  */
/*============================================================================*/

uint32_t OTA_Bootloader_Process(void)
{
    uint32_t bootFlag;
    uint32_t calculatedCRC;
    OTA_Boot_Status_t status;

    Boot_Print("\r\n========================================\r\n");
    Boot_Print("       OTA UPDATE CHECK\r\n");
    Boot_Print("========================================\r\n");

    Boot_EnableBackupDomain();

    bootFlag = Boot_GetBootFlag();
    Boot_PrintHex32("[BOOT] Boot flag: ", bootFlag);

    if (bootFlag != BOOT_FLAG_UPDATE)
    {
        Boot_Print("[BOOT] No update pending\r\n");
        Boot_Print("[BOOT] Booting Slot A\r\n");
        Boot_Print("========================================\r\n\r\n");
        return SLOT_A_CPU_ADDR;
    }

    Boot_Print("[BOOT] *** UPDATE PENDING ***\r\n");

    Boot_ClearBootFlag();
    Boot_Print("[BOOT] Boot flag cleared\r\n");

    Boot_PrintHex32("[BOOT] Mailbox magic: ", OTA_MAILBOX->magic);

    if (OTA_MAILBOX->magic != OTA_MAGIC)
    {
        Boot_Print("[BOOT] ERROR: Invalid mailbox!\r\n");
        Boot_Print("[BOOT] Falling back to Slot A\r\n");
        return SLOT_A_CPU_ADDR;
    }

    Boot_Print("[BOOT] Firmware info:\r\n");
    Boot_PrintHex32("       Size: ", OTA_MAILBOX->fwSize);
    Boot_PrintHex32("       Version: ", OTA_MAILBOX->version);
    Boot_PrintHex32("       Expected CRC: ", OTA_MAILBOX->expectedCRC);

    if (OTA_MAILBOX->fwSize == 0 || OTA_MAILBOX->fwSize > OTA_MAX_FW_SIZE)
    {
        Boot_Print("[BOOT] ERROR: Invalid firmware size!\r\n");
        Boot_PrintHex32("       Max: ", OTA_MAX_FW_SIZE);
        return SLOT_A_CPU_ADDR;
    }

    Boot_Print("[BOOT] Calculating CRC...\r\n");
    calculatedCRC = Boot_CalculateCRC32(OTA_MAILBOX->fwData, OTA_MAILBOX->fwSize);
    Boot_PrintHex32("       Calculated: ", calculatedCRC);

    if (calculatedCRC != OTA_MAILBOX->expectedCRC)
    {
        Boot_Print("[BOOT] ERROR: CRC mismatch!\r\n");
        Boot_Print("[BOOT] Falling back to Slot A\r\n");
        return SLOT_A_CPU_ADDR;
    }

    Boot_Print("[BOOT] CRC valid!\r\n");
    Boot_Print("[BOOT] Writing to Slot B...\r\n");

    status = Boot_WriteFirmwareToFlash(SLOT_B_FLASH_ADDR,
                                       OTA_MAILBOX->fwData,
                                       OTA_MAILBOX->fwSize);

    if (status != OTA_BOOT_OK)
    {
        Boot_Print("[BOOT] ERROR: Flash write failed!\r\n");
        Boot_Print("[BOOT] Falling back to Slot A\r\n");
        return SLOT_A_CPU_ADDR;
    }

    OTA_MAILBOX->magic = 0;

    Boot_Print("[BOOT] *** UPDATE SUCCESSFUL ***\r\n");
    Boot_Print("[BOOT] Booting Slot B\r\n");
    Boot_Print("========================================\r\n\r\n");

    return SLOT_B_CPU_ADDR;
}

/**
 * @brief  Jump to application - uses same method as original bootloader
 * @param  appAddr: SLOT_A_CPU_ADDR (0x70000000) or SLOT_B_CPU_ADDR (0x71000000)
 */
void OTA_Bootloader_JumpToApp(uint32_t appAddr)
{
    uint32_t primask_bit;
    typedef void (*pFunction)(void);
    pFunction JumpToApp;
    uint32_t Application_vector;

    EXTMEM_StatusTypeDef res =  EXTMEM_MemoryMappedMode(EXTMEMORY_1, EXTMEM_ENABLE);
    if( res != EXTMEM_OK )
    {
    	 Boot_Print("[BOOT] could not put ext memory in memory mapped mode\r\n");
    	return;
    }

    Boot_PrintHex32("[BOOT] Preparing jump to: ", appAddr);

    /* Get the base memory-mapped address from ExtMemManager */
    if (EXTMEM_OK != EXTMEM_GetMapAddress(EXTMEMORY_1, &Application_vector))
    {
        Boot_Print("[BOOT] ERROR: Failed to get map address!\r\n");
        /* Fall back to hardcoded address */
        Application_vector = 0x70000000;
    }

    Boot_PrintHex32("[BOOT] Base map address: ", Application_vector);

    /* If booting Slot B, add the offset */
    if (appAddr == SLOT_B_CPU_ADDR)
    {
        Application_vector += SLOT_B_OFFSET;
        Boot_Print("[BOOT] Adding Slot B offset\r\n");
    }

    /* Add image and header offsets (same as original bootloader) */
#if defined(EXTMEM_XIP_IMAGE_OFFSET)
    Application_vector += EXTMEM_XIP_IMAGE_OFFSET;
#endif

#if defined(EXTMEM_HEADER_OFFSET)
    Application_vector += EXTMEM_HEADER_OFFSET;
#endif

    Boot_PrintHex32("[BOOT] Final vector address: ", Application_vector);

    HAL_Delay(50);

    /* Suspend SysTick */
    HAL_SuspendTick();

    /* Disable I-Cache if enabled */
#if defined(__ICACHE_PRESENT) && (__ICACHE_PRESENT == 1U)
    if (SCB->CCR & SCB_CCR_IC_Msk)
    {
        SCB_DisableICache();
    }
#endif

    /* Disable D-Cache if enabled */
#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
    if (SCB->CCR & SCB_CCR_DC_Msk)
    {
        SCB_DisableDCache();
    }
#endif

    /* Save and disable interrupts */
    primask_bit = __get_PRIMASK();
    __disable_irq();

    /* Set vector table offset */
    SCB->VTOR = Application_vector;

    /* Get reset handler from vector table */
    JumpToApp = (pFunction)(*(__IO uint32_t *)(Application_vector + 4u));

    /* On ARM v8m, set MSPLIM before MSP to avoid stack overflow faults */
#if ((defined(__ARM_ARCH_8M_MAIN__) && (__ARM_ARCH_8M_MAIN__ == 1)) || \
     (defined(__ARM_ARCH_8_1M_MAIN__) && (__ARM_ARCH_8_1M_MAIN__ == 1)) || \
     (defined(__ARM_ARCH_8M_BASE__) && (__ARM_ARCH_8M_BASE__ == 1)))
    __set_MSPLIM(0x00000000);
#endif

    /* Set main stack pointer */
    __set_MSP(*(__IO uint32_t *)Application_vector);

    /* Re-enable interrupts */
    __set_PRIMASK(primask_bit);

    /* Jump to application */
    JumpToApp();

    /* Should never reach here */
    while (1);
}
