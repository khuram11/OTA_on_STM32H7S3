# ExtMemManager in XIP Application - Complete Guide

## Problem Summary

**Issue**: XSPI2 initialization fails with "busy" error when enabling ExtMemManager in application.

**Root Cause**: Your application executes from external flash (XIP - Execute-In-Place) via XSPI2. The bootloader initializes XSPI2 and puts it in memory-mapped mode, then jumps to your application at address 0x70000000. XSPI2 remains busy serving instruction fetches while your code runs.

**Why You Can't Reinitialize XSPI2**:
- XSPI2 is already in memory-mapped XIP mode
- Your application code is executing FROM the XSPI2 external flash
- Trying to reconfigure XSPI2 while executing from it is like changing a car's engine while driving!

---

## Architecture Overview

```
Boot Flow:
┌─────────────┐
│    Boot     │ 1. Initializes XSPI2
│  (0x08000)  │ 2. Calls MX_EXTMEM_MANAGER_Init()
└──────┬──────┘ 3. Enables memory mapping (0x70000000)
       │        4. Enables RCC_CLOCKPROTECT_XSPI
       │        5. Jumps to 0x70000000
       ▼
┌─────────────┐
│ Application │ ← You are here (executing FROM XSPI2!)
│(0x70000000) │
└─────────────┘

Current State:
- XSPI2: BUSY (memory-mapped mode, serving instruction fetches)
- Clock: Protected (RCC_CLOCKPROTECT_XSPI enabled by Boot)
- ExtMemManager: Initialized by Boot, still active
```

---

## Solutions (Choose Based on Your Needs)

### **Solution 1: Read-Only Access (Recommended for OTA Download)**

If you only need to **read** from external flash (e.g., downloading OTA firmware to a specific region), you don't need ExtMemManager initialization. Just access the memory directly:

```c
/* Example: Read OTA firmware region */
#define OTA_STORAGE_BASE    0x70000000  /* External flash base */
#define OTA_STORAGE_OFFSET  0x00100000  /* 1MB offset for OTA storage */
#define OTA_STORAGE_SIZE    0x00100000  /* 1MB size */

uint8_t *ota_region = (uint8_t*)(OTA_STORAGE_BASE + OTA_STORAGE_OFFSET);

/* Read directly from mapped memory */
void OTA_ReadFirmware(uint8_t *buffer, uint32_t size)
{
    memcpy(buffer, ota_region, size);
}

/* Check firmware validity */
uint32_t OTA_GetFirmwareVersion(void)
{
    /* Read version from first 4 bytes */
    return *(uint32_t*)ota_region;
}
```

**Advantages**:
- No initialization needed
- No mode switching
- Very fast (direct memory access)
- Safe for XIP execution

**Use this if**: You only need to read/verify downloaded OTA data.

---

### **Solution 2: Write Access with RAM-Based Functions**

If you need to **write** to external flash (e.g., flashing OTA firmware), use the provided RAM-based flash writer.

#### Files Created:
- `/home/user/OTA_on_STM32H7S3/Appli/Core/Inc/xspi_flash_writer.h`
- `/home/user/OTA_on_STM32H7S3/Appli/Core/Src/xspi_flash_writer.c`

#### Usage Example:

```c
#include "xspi_flash_writer.h"

void OTA_FlashNewFirmware(uint8_t *firmware_data, uint32_t size)
{
    XSPI_Flash_Status status;
    uint32_t flash_addr = 0x00100000;  /* 1MB offset in external flash */
    uint32_t sector_size = 0x1000;     /* 4KB sectors */

    /* 1. Initialize flash writer (reuses Boot's XSPI2 config) */
    status = XSPI_Flash_Init_FromXIP();
    if (status != XSPI_FLASH_OK) {
        printf("Flash init failed!\r\n");
        return;
    }

    /* 2. Erase sectors (one sector = 4KB) */
    uint32_t num_sectors = (size + sector_size - 1) / sector_size;
    for (uint32_t i = 0; i < num_sectors; i++) {
        status = XSPI_Flash_EraseSector(flash_addr + (i * sector_size));
        if (status != XSPI_FLASH_OK) {
            printf("Erase sector %lu failed!\r\n", i);
            return;
        }
    }

    /* 3. Program pages (one page = 256 bytes) */
    uint32_t offset = 0;
    while (offset < size) {
        uint32_t page_size = (size - offset) > 256 ? 256 : (size - offset);

        status = XSPI_Flash_WritePage(flash_addr + offset,
                                       firmware_data + offset,
                                       page_size);
        if (status != XSPI_FLASH_OK) {
            printf("Write page at 0x%lx failed!\r\n", flash_addr + offset);
            return;
        }

        offset += page_size;
    }

    /* 4. Verify (read back and compare) */
    uint8_t verify_buffer[256];
    offset = 0;
    while (offset < size) {
        uint32_t chunk_size = (size - offset) > 256 ? 256 : (size - offset);

        status = XSPI_Flash_Read(flash_addr + offset, verify_buffer, chunk_size);
        if (status != XSPI_FLASH_OK ||
            memcmp(firmware_data + offset, verify_buffer, chunk_size) != 0) {
            printf("Verification failed at 0x%lx!\r\n", flash_addr + offset);
            return;
        }

        offset += chunk_size;
    }

    printf("Firmware flashed successfully!\r\n");
}
```

#### How It Works:

1. **RAM-Based Execution**: All flash write functions use `__RAM_FUNC` attribute, which places them in RAM (defined in linker script at line 157-158: `.RamFunc` → `>RAM AT> FLASH`)

2. **Mode Switching**:
   - **Exit memory-mapped mode**: `XSPI_ExitMemoryMappedMode()` stops XIP
   - **Perform write operation**: Execute flash command (erase/program)
   - **Re-enter memory-mapped mode**: `XSPI_EnterMemoryMappedMode()` restores XIP

3. **Safety**: Functions execute from RAM, so switching XSPI2 mode doesn't crash the system

**Critical Notes**:
- Write operations temporarily disable XIP (code execution from flash)
- Functions must be in RAM (already configured in linker script)
- Interrupts that access external flash should be disabled during writes
- Total interruption time: ~3ms for sector erase, ~5ms for page program

**Use this if**: You need to write OTA firmware to external flash.

---

### **Solution 3: Hybrid Approach (Recommended)**

Combine both solutions for optimal performance:

```c
#include "xspi_flash_writer.h"

void OTA_Process(void)
{
    /* Step 1: Download firmware via modem to RAM buffer */
    uint8_t *fw_buffer = OTA_GetFirmwareBuffer();
    uint32_t fw_size = OTA_GetFirmwareSize();

    /* Step 2: Verify firmware in RAM */
    if (OTA_VerifyFirmwareCRC(fw_buffer, fw_size) != MODEM_OK) {
        printf("Firmware CRC check failed!\r\n");
        return;
    }

    /* Step 3: Flash to external memory using RAM-based writer */
    OTA_FlashNewFirmware(fw_buffer, fw_size);

    /* Step 4: Verify flashed firmware using direct read */
    uint8_t *flashed_fw = (uint8_t*)0x70100000;  /* 1MB offset */
    if (memcmp(fw_buffer, flashed_fw, fw_size) == 0) {
        printf("Firmware verification successful!\r\n");

        /* Step 5: Update boot configuration to use new firmware */
        /* ... boot vector table update logic ... */
    }
}
```

---

## What NOT to Do

### ❌ **NEVER Do This in Application**:

```c
/* DON'T: Reinitialize XSPI2 from scratch */
void MX_XSPI2_Init(void)  // ← Will fail with "busy" error!
{
    hxspi2.Instance = XSPI2;
    // ... initialization code ...
    HAL_XSPI_Init(&hxspi2);  // ← XSPI2 is busy! This fails!
}

/* DON'T: Call ExtMemManager init like Boot does */
void MX_EXTMEM_MANAGER_Init(void)  // ← Will fail!
{
    // ... this tries to reinitialize XSPI2 internally ...
}

/* DON'T: Try to change XSPI2 configuration directly */
HAL_XSPI_DeInit(&hxspi2);  // ← Crash! You're executing from XSPI2!
```

**Why**: Your application code is executing FROM the XSPI2 external flash. Deinitializing or reconfiguring it will crash the system.

---

## Configuration Files Reference

### Boot Configuration (Working ✓):
- **extmem_manager.c** (`/home/user/OTA_on_STM32H7S3/Boot/Core/Src/extmem_manager.c:63`):
  ```c
  HAL_RCCEx_EnableClockProtection(RCC_CLOCKPROTECT_XSPI);  // Protects clock
  ```

- **main.c** (`/home/user/OTA_on_STM32H7S3/Boot/Core/Src/main.c:257-300`):
  - Initializes XSPI2 hardware
  - Calls `MX_EXTMEM_MANAGER_Init()`
  - Maps memory to 0x70000000
  - Jumps to application

### Application Configuration (Current):
- **main.c** (`/home/user/OTA_on_STM32H7S3/Appli/Core/Src/main.c`):
  - No XSPI2 initialization (correct!)
  - No ExtMemManager initialization (correct!)
  - Executes from 0x70000000 (XIP mode)

### Linker Script:
- **STM32H7S3L8HX_ROMxspi2_app.ld** (lines 157-158):
  ```ld
  *(.RamFunc)        /* .RamFunc sections */
  *(.RamFunc*)       /* .RamFunc* sections */
  ```
  - Ensures `__RAM_FUNC` functions execute from RAM
  - Critical for flash write operations during XIP

---

## Memory Map

```
0x00000000 - 0x0000FFFF : ITCM RAM (64KB)
0x08000000 - 0x0801FFFF : Internal Flash (128KB) - Boot code
0x20000000 - 0x2000FFFF : DTCM RAM (64KB)
0x24000000 - 0x24071BFF : AXI SRAM (456KB) - Application RAM
0x70000000 - 0x77FFFFFF : External Flash (128MB) - Application code & data
  ├─ 0x70000000 - 0x700FFFFF : Application code (1MB)
  ├─ 0x70100000 - 0x701FFFFF : OTA firmware storage (1MB) ← Suggested
  └─ 0x70200000 - 0x77FFFFFF : Free space (126MB)
```

---

## Debugging Tips

### Check XSPI2 State:
```c
void Debug_CheckXSPI2State(void)
{
    printf("XSPI2 CR: 0x%lx\r\n", XSPI2->CR);
    printf("XSPI2 SR: 0x%lx\r\n", XSPI2->SR);

    if (XSPI2->SR & XSPI_SR_BUSY) {
        printf("XSPI2 is BUSY (expected in XIP mode)\r\n");
    }

    if (XSPI2->CR & XSPI_CR_FMODE) {
        printf("XSPI2 in memory-mapped mode\r\n");
    }
}
```

### Verify Boot Initialization:
```c
void Debug_CheckBootConfig(void)
{
    /* Check if clock protection is enabled (set by Boot) */
    if (RCC->CKPROTR & RCC_CKPROTR_XSPI1P) {
        printf("XSPI clock protection: ENABLED\r\n");
    }

    /* Verify external flash is accessible */
    volatile uint32_t *flash_base = (uint32_t*)0x70000000;
    printf("External flash first word: 0x%lx\r\n", *flash_base);
}
```

---

## Integration with Your OTA Code

### Current OTA Flow (`main.c:151-162`):
```c
if (OTA_TestDownload() == MODEM_OK)
{
    printf("Firmware downloaded successfully!\r\n");

    uint8_t *fw = OTA_GetFirmwareBuffer();  // ← In RAM
    uint32_t size = OTA_GetFirmwareSize();
    printf("Size : %ld\n", size);

    // TODO: Flash to external memory
}
```

### Enhanced OTA Flow (Add This):
```c
#include "xspi_flash_writer.h"

if (OTA_TestDownload() == MODEM_OK)
{
    printf("Firmware downloaded successfully!\r\n");

    /* Get firmware from RAM buffer */
    uint8_t *fw = OTA_GetFirmwareBuffer();
    uint32_t size = OTA_GetFirmwareSize();
    printf("Firmware size: %ld bytes\r\n", size);

    /* Verify CRC in RAM */
    if (OTA_VerifyFirmwareCRC(fw, size) != MODEM_OK) {
        printf("CRC verification failed!\r\n");
    } else {
        printf("CRC verification passed!\r\n");

        /* Flash to external memory */
        printf("Flashing firmware to external flash...\r\n");
        OTA_FlashNewFirmware(fw, size);  // Use Solution 2 function

        /* Verify flashed firmware */
        uint8_t *flashed_fw = (uint8_t*)0x70100000;
        if (memcmp(fw, flashed_fw, size) == 0) {
            printf("Firmware flash verification OK!\r\n");

            /* Mark new firmware as valid and reboot */
            // ... add your boot management logic here ...
        }
    }
}
```

---

## Performance Considerations

### Read Performance (Memory-Mapped):
- **Direct read**: ~200 MB/s (depends on XSPI clock)
- **No overhead**: CPU fetches instructions/data directly via AHB matrix
- **Cache benefits**: I-Cache and D-Cache improve performance

### Write Performance (Command Mode):
- **Sector erase (4KB)**: ~100-400ms
- **Page program (256 bytes)**: ~0.5-5ms
- **Mode switching overhead**: ~1-2ms per operation

### Optimization Tips:
1. **Batch operations**: Erase multiple sectors before programming
2. **Use DMA**: For large data transfers (requires additional setup)
3. **Disable interrupts**: During critical write operations
4. **Verify in chunks**: Don't load entire firmware into RAM for verification

---

## Summary

| Use Case | Solution | Requires Mode Switch | Execution Location |
|----------|----------|----------------------|-------------------|
| Read OTA firmware | Direct memory access | No | XIP (Flash) |
| Verify firmware | Direct memory access | No | XIP (Flash) |
| Write OTA firmware | RAM-based flash writer | Yes | RAM |
| Erase sectors | RAM-based flash writer | Yes | RAM |

**Recommended Approach**:
1. Download firmware to RAM via modem
2. Verify CRC in RAM
3. Flash to external memory using RAM-based writer (Solution 2)
4. Verify flashed firmware using direct read (Solution 1)
5. Update boot configuration
6. Reboot to new firmware

---

## Next Steps

1. **Add xspi_flash_writer.c to your build system**:
   - Add to CMakeLists.txt or IDE project
   - Verify `.RamFunc` section is linked correctly

2. **Test read-only access first**:
   ```c
   uint8_t test_data[256];
   XSPI_Flash_Read(0, test_data, 256);  // Read first 256 bytes
   ```

3. **Test write operations**:
   - Start with a small test (1 sector erase + 1 page write)
   - Verify immediately after write
   - Expand to full OTA firmware flashing

4. **Implement boot management**:
   - Dual-bank firmware (current + new)
   - Boot counter for rollback
   - CRC verification before boot

---

## Additional Resources

- **STM32H7S3 Reference Manual**: Chapter "XSPI" for peripheral details
- **AN5188**: "Using the external XSPI memory in execute-in-place mode"
- **Codebase Reference**:
  - Boot XSPI2 init: `/home/user/OTA_on_STM32H7S3/Boot/Core/Src/main.c:257-300`
  - ExtMemManager: `/home/user/OTA_on_STM32H7S3/Boot/Core/Src/extmem_manager.c`
  - Linker script: `/home/user/OTA_on_STM32H7S3/Appli/STM32H7S3L8HX_ROMxspi2_app.ld`

---

**Last Updated**: 2026-01-19
**Author**: Generated by Claude Code Agent
