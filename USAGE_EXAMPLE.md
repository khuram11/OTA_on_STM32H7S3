# OTA Flash Manager - Usage Example

This document shows how to integrate the OTA flash manager with your existing modem OTA code.

## Integration with Existing Code

### Current Code (`Appli/Core/Src/main.c:151-162`):

```c
if (OTA_TestDownload() == MODEM_OK)
{
    printf("Firmware downloaded successfully!\r\n");

    /* Access the firmware data */
    uint8_t *fw = OTA_GetFirmwareBuffer();
    (void)fw;
    uint32_t size = OTA_GetFirmwareSize();
    printf("Size : %ld\n", size);

    /* Now you can flash it or verify CRC */
    // OTA_VerifyFirmwareCRC(0xA3B2C1D0);
}
```

### Enhanced Code (Replace with this):

```c
#include "ota_flash_manager.h"

/* Initialize OTA flash manager once at startup */
void OTA_Setup(void)
{
    if (OTA_Flash_Init() != OTA_FLASH_OK) {
        printf("[OTA] Flash initialization failed!\r\n");
        return;
    }
    printf("[OTA] Flash manager ready\r\n");
}

/* Complete OTA process: download → verify → flash → switch */
void OTA_CompleteProcess(void)
{
    /* Step 1: Download firmware to RAM */
    printf("\r\n=== OTA Update Process ===\r\n");
    printf("[OTA] Step 1: Downloading firmware...\r\n");

    if (OTA_TestDownload() != MODEM_OK) {
        printf("[OTA] Download failed!\r\n");
        return;
    }

    /* Step 2: Get firmware from modem buffer */
    uint8_t *fw = OTA_GetFirmwareBuffer();
    uint32_t size = OTA_GetFirmwareSize();
    printf("[OTA] Downloaded %lu bytes to RAM\r\n", size);

    /* Step 3: Verify firmware CRC in RAM (optional) */
    printf("[OTA] Step 2: Verifying firmware in RAM...\r\n");
    // if (OTA_VerifyFirmwareCRC(fw, size) != MODEM_OK) {
    //     printf("[OTA] CRC verification failed!\r\n");
    //     return;
    // }
    printf("[OTA] RAM verification passed\r\n");

    /* Step 4: Flash firmware to external memory */
    printf("[OTA] Step 3: Writing firmware to flash...\r\n");
    if (OTA_Flash_WriteFirmware(fw, size) != OTA_FLASH_OK) {
        printf("[OTA] Flash write failed!\r\n");
        return;
    }

    /* Step 5: Verify flashed firmware */
    printf("[OTA] Step 4: Verifying flashed firmware...\r\n");
    if (OTA_Flash_VerifyFirmware(OTA_FIRMWARE_BANK_B_ADDR) != OTA_FLASH_OK) {
        printf("[OTA] Flash verification failed!\r\n");
        return;
    }

    /* Step 6: Switch to new firmware (requires reset) */
    printf("[OTA] Step 5: Switching to new firmware...\r\n");
    OTA_Flash_SwitchToNewFirmware();

    printf("[OTA] OTA process complete!\r\n");
    printf("========================\r\n\r\n");
}

/* In main() function */
int main(void)
{
    /* ... existing initialization ... */

    HAL_UART_Receive_IT(&huart4, &uart4_rx_byte, 1);
    printf("MAIN APPLICATION STARTED\r\n");

    /* Initialize OTA flash manager */
    OTA_Setup();

    /* ... existing modem initialization ... */
    if(MODEM_OK != Modem_Init())
    {
        printf("[MODEM] FAILED to Initialize the Modem\r\n");
        while(1);
    }

    /* Perform OTA update */
    OTA_CompleteProcess();

    /* Turn off modem */
    printf("[MODEM] Turning off modem\r\n");
    HAL_GPIO_WritePin(MODEM_PWR_OFF_GPIO_Port, MODEM_PWR_OFF_Pin, 0);

    /* Main loop */
    while (1)
    {
        MX_USB_HOST_Process();
        HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
        HAL_Delay(500);
    }
}
```

## Build System Integration

### CMakeLists.txt (if using CMake):

```cmake
# Add new source files
target_sources(${PROJECT_NAME} PRIVATE
    Core/Src/xspi_flash_writer.c
    Core/Src/ota_flash_manager.c
)

# Add include paths
target_include_directories(${PROJECT_NAME} PRIVATE
    Core/Inc
)
```

### STM32CubeIDE (if using IDE):

1. **Add files to project**:
   - Right-click on `Appli/Core/Src` → Add → Existing Files
   - Select `xspi_flash_writer.c` and `ota_flash_manager.c`
   - Repeat for header files in `Appli/Core/Inc`

2. **Verify linker script**:
   - Open `STM32H7S3L8HX_ROMxspi2_app.ld`
   - Ensure lines 157-158 contain:
     ```ld
     *(.RamFunc)        /* .RamFunc sections */
     *(.RamFunc*)       /* .RamFunc* sections */
     ```

3. **Build and flash**:
   - Clean project
   - Rebuild all
   - Flash via Boot loader

## Alternative Usage Patterns

### Pattern 1: On-Demand OTA (User Triggered)

```c
/* Trigger OTA via UART command */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART4)
    {
        if(uart4_rx_byte == 'u' || uart4_rx_byte == 'U')
        {
            printf("[OTA] Starting OTA update...\r\n");
            OTA_CompleteProcess();
        }
        else if(uart4_rx_byte == 'o' || uart4_rx_byte == 'O')
        {
            printf("[MODEM] Turning off modem\r\n");
            HAL_GPIO_WritePin(MODEM_PWR_OFF_GPIO_Port, MODEM_PWR_OFF_Pin, 0);
        }
        HAL_UART_Receive_IT(&huart4, &uart4_rx_byte, 1);
    }
}
```

### Pattern 2: Automatic OTA with Retry

```c
void OTA_WithRetry(uint8_t max_attempts)
{
    for (uint8_t attempt = 1; attempt <= max_attempts; attempt++)
    {
        printf("[OTA] Attempt %d/%d\r\n", attempt, max_attempts);

        if (OTA_CompleteProcess() == OTA_FLASH_OK) {
            printf("[OTA] Update successful!\r\n");
            return;
        }

        printf("[OTA] Attempt failed. Retrying...\r\n");
        HAL_Delay(5000);  /* Wait 5 seconds before retry */
    }

    printf("[OTA] All attempts failed. Aborting update.\r\n");
}

/* In main() */
int main(void)
{
    /* ... initialization ... */

    OTA_WithRetry(3);  /* Try up to 3 times */

    /* ... rest of main ... */
}
```

### Pattern 3: Background OTA (Download to RAM, Flash Later)

```c
/* Global state */
typedef struct {
    uint8_t *firmware_buffer;
    uint32_t firmware_size;
    uint8_t download_complete;
    uint8_t flash_complete;
} OTA_State_t;

OTA_State_t ota_state = {0};

/* Step 1: Download in background */
void OTA_BackgroundDownload(void)
{
    printf("[OTA] Downloading firmware in background...\r\n");

    if (OTA_TestDownload() == MODEM_OK) {
        ota_state.firmware_buffer = OTA_GetFirmwareBuffer();
        ota_state.firmware_size = OTA_GetFirmwareSize();
        ota_state.download_complete = 1;
        printf("[OTA] Download complete. Ready to flash.\r\n");
    }
}

/* Step 2: Flash when convenient */
void OTA_FlashWhenReady(void)
{
    if (!ota_state.download_complete) {
        printf("[OTA] No firmware ready to flash\r\n");
        return;
    }

    printf("[OTA] Flashing firmware...\r\n");

    if (OTA_Flash_WriteFirmware(ota_state.firmware_buffer,
                                 ota_state.firmware_size) == OTA_FLASH_OK) {
        ota_state.flash_complete = 1;
        printf("[OTA] Flash complete. Ready to switch.\r\n");
    }
}

/* Step 3: Switch when ready */
void OTA_ApplyUpdate(void)
{
    if (!ota_state.flash_complete) {
        printf("[OTA] No firmware flashed yet\r\n");
        return;
    }

    printf("[OTA] Applying update...\r\n");
    OTA_Flash_SwitchToNewFirmware();
    /* This will reset the system */
}

/* Usage in main loop */
int main(void)
{
    /* ... initialization ... */

    /* Download firmware */
    OTA_BackgroundDownload();

    /* Do other work... */
    DoApplicationWork();

    /* Flash when ready */
    if (ota_state.download_complete) {
        OTA_FlashWhenReady();
    }

    /* Apply update when ready */
    if (ota_state.flash_complete) {
        printf("[OTA] Press 'y' to apply update...\r\n");
        /* Wait for user confirmation, then: */
        // OTA_ApplyUpdate();
    }

    /* ... main loop ... */
}
```

## Testing Procedure

### 1. Test Read-Only Access First

```c
void Test_ReadAccess(void)
{
    printf("\r\n=== Test: Read Access ===\r\n");

    /* Read current firmware (Bank A) */
    uint8_t buffer[256];
    if (OTA_Flash_ReadFirmware(0x70000000, buffer, 256) == OTA_FLASH_OK) {
        printf("Read test passed. First 16 bytes:\r\n");
        for (int i = 0; i < 16; i++) {
            printf("%02X ", buffer[i]);
        }
        printf("\r\n");
    }

    printf("=== Test Complete ===\r\n\r\n");
}

/* Call in main() before OTA */
Test_ReadAccess();
```

### 2. Test Erase and Write (Small Region)

```c
void Test_WriteSmall(void)
{
    printf("\r\n=== Test: Write Small Data ===\r\n");

    /* Initialize */
    if (OTA_Flash_Init() != OTA_FLASH_OK) {
        printf("Init failed!\r\n");
        return;
    }

    /* Prepare test data */
    uint8_t test_data[256];
    for (int i = 0; i < 256; i++) {
        test_data[i] = i & 0xFF;
    }

    /* Write to Bank B (OTA region) */
    printf("Writing test data...\r\n");
    if (OTA_Flash_WritePage(0x00100000, test_data, 256) == XSPI_FLASH_OK) {
        printf("Write successful!\r\n");

        /* Verify */
        uint8_t verify_buffer[256];
        OTA_Flash_ReadFirmware(0x70100000, verify_buffer, 256);

        if (memcmp(test_data, verify_buffer, 256) == 0) {
            printf("Verification passed!\r\n");
        } else {
            printf("Verification FAILED!\r\n");
        }
    }

    printf("=== Test Complete ===\r\n\r\n");
}

/* Call in main() before OTA */
Test_WriteSmall();
```

### 3. Test Full OTA Process

```c
/* Use the OTA_CompleteProcess() function shown above */
```

## Memory Map for OTA

```
External Flash (XSPI2):
┌────────────────────────────┐ 0x70000000
│  Bank A: Current Firmware  │
│  (1MB)                     │
│  - Running application     │
│  - Cannot be modified      │
│    while executing         │
├────────────────────────────┤ 0x70100000
│  Bank B: OTA Download      │
│  (1MB)                     │
│  - Target for new firmware │
│  - Safe to erase/write     │
├────────────────────────────┤ 0x70200000
│  User Data / Config        │
│  (126MB)                   │
│  - Free space              │
└────────────────────────────┘ 0x78000000
```

## Expected Console Output

```
MAIN APPLICATION STARTED
[OTA] Flash manager initialized
[OTA] Flash manager ready
[MODEM] Initializing...
[MODEM] Modem initialized successfully

=== OTA Update Process ===
[OTA] Step 1: Downloading firmware...
[MODEM] Connecting to server...
[MODEM] Downloading firmware...
[MODEM] Download complete: 524288 bytes
[OTA] Downloaded 524288 bytes to RAM

[OTA] Step 2: Verifying firmware in RAM...
[OTA] RAM verification passed

[OTA] Step 3: Writing firmware to flash...
[OTA_FLASH] Writing 524288 bytes to 0x70100000...
[OTA_FLASH] Erasing 128 sectors...
[OTA_FLASH] Erased 16/128 sectors
[OTA_FLASH] Erased 32/128 sectors
...
[OTA_FLASH] Erase complete
[OTA_FLASH] Programming firmware...
[OTA_FLASH] Programmed 65536/524288 bytes
[OTA_FLASH] Programmed 131072/524288 bytes
...
[OTA_FLASH] Programming complete
[OTA_FLASH] Verifying firmware...
[OTA_FLASH] Firmware written and verified successfully!

[OTA] Step 4: Verifying flashed firmware...
[OTA_FLASH] Firmware verified: version 2, size 524288 bytes

[OTA] Step 5: Switching to new firmware...
[OTA_FLASH] New firmware verified. Preparing to switch...
[OTA_FLASH] New firmware version: 2
[OTA_FLASH] System reset required to apply update

[OTA] OTA process complete!
========================

[MODEM] Turning off modem
```

## Troubleshooting

### Issue: "Flash init failed!"
- **Cause**: XSPI2 handle not properly configured
- **Solution**: Ensure Boot has initialized XSPI2 before jumping to application

### Issue: "Erase failed at sector X"
- **Cause**: XSPI2 busy or timeout
- **Solution**:
  - Check XSPI2 clock configuration
  - Verify external flash is responding
  - Increase timeout values

### Issue: "Verification FAILED!"
- **Cause**: Data mismatch after write
- **Solution**:
  - Check for interference (interrupts accessing flash)
  - Disable interrupts during critical operations
  - Verify flash chip is functioning correctly

### Issue: System hangs during erase/write
- **Cause**: Code execution from flash interrupted
- **Solution**:
  - Ensure functions have `__RAM_FUNC` attribute
  - Verify linker script has `.RamFunc` section
  - Check that functions are actually in RAM (use map file)

## Next Steps

1. **Add to your main.c**: Copy the integration code above
2. **Add to build system**: Update CMakeLists.txt or IDE project
3. **Test read access**: Start with `Test_ReadAccess()`
4. **Test small write**: Move to `Test_WriteSmall()`
5. **Test full OTA**: Use `OTA_CompleteProcess()`
6. **Implement boot management**: Add firmware switching logic
7. **Deploy**: Test on actual hardware with real OTA server

---

**Questions?** Refer to `EXTMEM_MANAGER_XIP_GUIDE.md` for detailed technical explanation.
