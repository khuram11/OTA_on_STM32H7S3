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
