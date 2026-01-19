/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : xspi_flash_writer.c
  * @brief          : XSPI flash write functions (RAM-based for XIP safety)
  ******************************************************************************
  * IMPORTANT: These functions MUST execute from RAM, not external flash!
  *            Use __RAM_FUNC attribute to ensure they're placed in RAM.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "xspi_flash_writer.h"
#include <string.h>

/* XSPI handle - reuse the one initialized by Boot */
XSPI_HandleTypeDef hxspi2;

/* Flash commands (Macronix MX25LM51245G) */
#define WRITE_ENABLE_CMD                     0x06
#define WRITE_DISABLE_CMD                    0x04
#define READ_STATUS_REG_CMD                  0x05
#define WRITE_STATUS_REG_CMD                 0x01
#define READ_CFG_REG_CMD                     0x15
#define WRITE_CFG_REG_CMD                    0x01
#define SECTOR_ERASE_CMD                     0x20
#define BLOCK_ERASE_64K_CMD                  0xD8
#define CHIP_ERASE_CMD                       0x60
#define PAGE_PROG_CMD                        0x02
#define OCTA_PAGE_PROG_CMD                   0x12
#define READ_CMD                             0x03
#define FAST_READ_CMD                        0x0B

/* Status register bits */
#define SR_WIP                               0x01  /* Write in progress */
#define SR_WEL                               0x02  /* Write enable latch */

/* Timeouts */
#define XSPI_TIMEOUT_DEFAULT_VALUE           5000
#define FLASH_SECTOR_ERASE_MAX_TIME          3000
#define FLASH_PAGE_PROGRAM_MAX_TIME          5

/**
  * @brief  Initialize XSPI flash writer (reuse Boot's XSPI2 config)
  * @note   This does NOT reinitialize XSPI2 hardware - just prepares the handle
  * @retval XSPI_Flash_Status
  */
__RAM_FUNC XSPI_Flash_Status XSPI_Flash_Init_FromXIP(void)
{
    /* CRITICAL: Do NOT call MX_XSPI2_Init() or HAL_XSPI_Init() here!
     * The XSPI2 is already initialized by Boot and in memory-mapped mode.
     * We just need to ensure we have a valid handle structure.
     */

    /* The hxspi2 handle should already be configured by Boot.
     * If not, we need to manually reconstruct it from Boot's settings.
     */
    hxspi2.Instance = XSPI2;
    hxspi2.Init.MemoryType = HAL_XSPI_MEMTYPE_MACRONIX;
    hxspi2.Init.MemorySize = HAL_XSPI_SIZE_256MB;
    hxspi2.Init.ClockPrescaler = 0;
    hxspi2.Init.ChipSelectHighTime = 2;
    hxspi2.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
    hxspi2.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
    hxspi2.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
    hxspi2.Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_ENABLE;
    hxspi2.Init.ChipSelectBoundary = HAL_XSPI_BONDARYOF_NONE;
    hxspi2.Init.DelayBlockBypass = HAL_XSPI_DELAY_BLOCK_ON;
    hxspi2.Init.Refresh = 0;
    hxspi2.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
    hxspi2.Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;

    return XSPI_FLASH_OK;
}

/**
  * @brief  Exit memory-mapped mode and enter command mode
  * @retval XSPI_Flash_Status
  */
__RAM_FUNC static XSPI_Flash_Status XSPI_ExitMemoryMappedMode(void)
{
    /* Abort any ongoing memory-mapped operation */
    if (HAL_XSPI_Abort(&hxspi2) != HAL_OK)
    {
        return XSPI_FLASH_ERROR;
    }

    return XSPI_FLASH_OK;
}

/**
  * @brief  Re-enter memory-mapped mode (for XIP execution)
  * @retval XSPI_Flash_Status
  */
__RAM_FUNC static XSPI_Flash_Status XSPI_EnterMemoryMappedMode(void)
{
    XSPI_MemoryMappedTypeDef sMemMappedCfg = {0};
    XSPI_RegularCmdTypeDef sCommand = {0};

    /* Configure memory-mapped read command */
    sCommand.OperationType = HAL_XSPI_OPTYPE_READ_CFG;
    sCommand.Instruction = FAST_READ_CMD;
    sCommand.InstructionMode = HAL_XSPI_INSTRUCTION_1_LINE;
    sCommand.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sCommand.Address = 0;
    sCommand.AddressMode = HAL_XSPI_ADDRESS_1_LINE;
    sCommand.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_DISABLE;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataMode = HAL_XSPI_DATA_1_LINE;
    sCommand.DataLength = 1;
    sCommand.DataDTRMode = HAL_XSPI_DATA_DTR_DISABLE;
    sCommand.DummyCycles = 8;
    sCommand.DQSMode = HAL_XSPI_DQS_DISABLE;
    sCommand.SIOOMode = HAL_XSPI_SIOO_INST_EVERY_CMD;

    /* Configure timeout counter */
    sMemMappedCfg.TimeOutActivation = HAL_XSPI_TIMEOUT_COUNTER_DISABLE;

    if (HAL_XSPI_MemoryMapped(&hxspi2, &sCommand, &sMemMappedCfg) != HAL_OK)
    {
        return XSPI_FLASH_ERROR;
    }

    return XSPI_FLASH_OK;
}

/**
  * @brief  Send write enable command
  * @retval XSPI_Flash_Status
  */
__RAM_FUNC static XSPI_Flash_Status XSPI_WriteEnable(void)
{
    XSPI_RegularCmdTypeDef sCommand = {0};

    sCommand.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.Instruction = WRITE_ENABLE_CMD;
    sCommand.InstructionMode = HAL_XSPI_INSTRUCTION_1_LINE;
    sCommand.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sCommand.AddressMode = HAL_XSPI_ADDRESS_NONE;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataMode = HAL_XSPI_DATA_NONE;
    sCommand.DummyCycles = 0;
    sCommand.DQSMode = HAL_XSPI_DQS_DISABLE;

    if (HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        return XSPI_FLASH_ERROR;
    }

    return XSPI_FLASH_OK;
}

/**
  * @brief  Wait for write operation to complete
  * @param  Timeout: Timeout in ms
  * @retval XSPI_Flash_Status
  */
__RAM_FUNC static XSPI_Flash_Status XSPI_AutoPollingMemReady(uint32_t Timeout)
{
    XSPI_RegularCmdTypeDef sCommand = {0};
    XSPI_AutoPollingTypeDef sConfig = {0};

    /* Configure automatic polling to wait for memory ready */
    sCommand.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.Instruction = READ_STATUS_REG_CMD;
    sCommand.InstructionMode = HAL_XSPI_INSTRUCTION_1_LINE;
    sCommand.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sCommand.AddressMode = HAL_XSPI_ADDRESS_NONE;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataMode = HAL_XSPI_DATA_1_LINE;
    sCommand.DataLength = 1;
    sCommand.DataDTRMode = HAL_XSPI_DATA_DTR_DISABLE;
    sCommand.DummyCycles = 0;
    sCommand.DQSMode = HAL_XSPI_DQS_DISABLE;

    sConfig.MatchValue = 0;
    sConfig.MatchMask = SR_WIP;
    sConfig.MatchMode = HAL_XSPI_MATCH_MODE_AND;
    sConfig.AutomaticStop = HAL_XSPI_AUTOMATIC_STOP_ENABLE;
    sConfig.IntervalTime = 0x10;

    if (HAL_XSPI_AutoPolling(&hxspi2, &sCommand, &sConfig, Timeout) != HAL_OK)
    {
        return XSPI_FLASH_TIMEOUT;
    }

    return XSPI_FLASH_OK;
}

/**
  * @brief  Erase a sector (4KB)
  * @param  sector_address: Sector address to erase
  * @retval XSPI_Flash_Status
  */
__RAM_FUNC XSPI_Flash_Status XSPI_Flash_EraseSector(uint32_t sector_address)
{
    XSPI_RegularCmdTypeDef sCommand = {0};

    /* CRITICAL: Exit memory-mapped mode */
    if (XSPI_ExitMemoryMappedMode() != XSPI_FLASH_OK)
    {
        return XSPI_FLASH_ERROR;
    }

    /* Enable write operations */
    if (XSPI_WriteEnable() != XSPI_FLASH_OK)
    {
        XSPI_EnterMemoryMappedMode();
        return XSPI_FLASH_ERROR;
    }

    /* Erase the sector */
    sCommand.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.Instruction = SECTOR_ERASE_CMD;
    sCommand.InstructionMode = HAL_XSPI_INSTRUCTION_1_LINE;
    sCommand.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sCommand.Address = sector_address;
    sCommand.AddressMode = HAL_XSPI_ADDRESS_1_LINE;
    sCommand.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_DISABLE;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataMode = HAL_XSPI_DATA_NONE;
    sCommand.DummyCycles = 0;
    sCommand.DQSMode = HAL_XSPI_DQS_DISABLE;

    if (HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        XSPI_EnterMemoryMappedMode();
        return XSPI_FLASH_ERROR;
    }

    /* Wait for erase to complete */
    if (XSPI_AutoPollingMemReady(FLASH_SECTOR_ERASE_MAX_TIME) != XSPI_FLASH_OK)
    {
        XSPI_EnterMemoryMappedMode();
        return XSPI_FLASH_TIMEOUT;
    }

    /* CRITICAL: Re-enter memory-mapped mode for XIP */
    if (XSPI_EnterMemoryMappedMode() != XSPI_FLASH_OK)
    {
        return XSPI_FLASH_ERROR;
    }

    return XSPI_FLASH_OK;
}

/**
  * @brief  Write a page (up to 256 bytes)
  * @param  address: Write address
  * @param  data: Pointer to data buffer
  * @param  size: Size of data (max 256 bytes)
  * @retval XSPI_Flash_Status
  */
__RAM_FUNC XSPI_Flash_Status XSPI_Flash_WritePage(uint32_t address, uint8_t *data, uint32_t size)
{
    XSPI_RegularCmdTypeDef sCommand = {0};

    if (size > 256)
    {
        return XSPI_FLASH_ERROR;
    }

    /* CRITICAL: Exit memory-mapped mode */
    if (XSPI_ExitMemoryMappedMode() != XSPI_FLASH_OK)
    {
        return XSPI_FLASH_ERROR;
    }

    /* Enable write operations */
    if (XSPI_WriteEnable() != XSPI_FLASH_OK)
    {
        XSPI_EnterMemoryMappedMode();
        return XSPI_FLASH_ERROR;
    }

    /* Program the page */
    sCommand.OperationType = HAL_XSPI_OPTYPE_COMMON_CFG;
    sCommand.Instruction = PAGE_PROG_CMD;
    sCommand.InstructionMode = HAL_XSPI_INSTRUCTION_1_LINE;
    sCommand.InstructionWidth = HAL_XSPI_INSTRUCTION_8_BITS;
    sCommand.InstructionDTRMode = HAL_XSPI_INSTRUCTION_DTR_DISABLE;
    sCommand.Address = address;
    sCommand.AddressMode = HAL_XSPI_ADDRESS_1_LINE;
    sCommand.AddressWidth = HAL_XSPI_ADDRESS_32_BITS;
    sCommand.AddressDTRMode = HAL_XSPI_ADDRESS_DTR_DISABLE;
    sCommand.AlternateBytesMode = HAL_XSPI_ALT_BYTES_NONE;
    sCommand.DataMode = HAL_XSPI_DATA_1_LINE;
    sCommand.DataLength = size;
    sCommand.DataDTRMode = HAL_XSPI_DATA_DTR_DISABLE;
    sCommand.DummyCycles = 0;
    sCommand.DQSMode = HAL_XSPI_DQS_DISABLE;

    if (HAL_XSPI_Command(&hxspi2, &sCommand, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        XSPI_EnterMemoryMappedMode();
        return XSPI_FLASH_ERROR;
    }

    if (HAL_XSPI_Transmit(&hxspi2, data, HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
        XSPI_EnterMemoryMappedMode();
        return XSPI_FLASH_ERROR;
    }

    /* Wait for write to complete */
    if (XSPI_AutoPollingMemReady(FLASH_PAGE_PROGRAM_MAX_TIME) != XSPI_FLASH_OK)
    {
        XSPI_EnterMemoryMappedMode();
        return XSPI_FLASH_TIMEOUT;
    }

    /* CRITICAL: Re-enter memory-mapped mode for XIP */
    if (XSPI_EnterMemoryMappedMode() != XSPI_FLASH_OK)
    {
        return XSPI_FLASH_ERROR;
    }

    return XSPI_FLASH_OK;
}

/**
  * @brief  Read from flash (uses memory-mapped read)
  * @param  address: Read address (relative to flash base, e.g., 0x0 for first byte)
  * @param  data: Pointer to data buffer
  * @param  size: Size of data to read
  * @retval XSPI_Flash_Status
  */
__RAM_FUNC XSPI_Flash_Status XSPI_Flash_Read(uint32_t address, uint8_t *data, uint32_t size)
{
    /* Simple memory-mapped read (no mode switch needed) */
    uint8_t *flash_ptr = (uint8_t*)(0x70000000 + address);
    memcpy(data, flash_ptr, size);

    return XSPI_FLASH_OK;
}
