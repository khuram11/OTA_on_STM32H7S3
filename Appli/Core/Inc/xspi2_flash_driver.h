/*
 * xspi2_flash_driver.h
 *
 *  Created on: Jan 29, 2026
 *      Author: admin
 */

#ifndef INC_XSPI2_FLASH_DRIVER_H_
#define INC_XSPI2_FLASH_DRIVER_H_

#if defined(__GNUC__)
    #define RAM_FUNC    __attribute__((section(".RamFunc"), noinline, used))
    #define RAM_DATA    __attribute__((section(".RamData")))
#elif defined(__ICCARM__)
    #define RAM_FUNC    __ramfunc
    #define RAM_DATA    __no_init
#elif defined(__ARMCC_VERSION)
    #define RAM_FUNC    __attribute__((section("RamFunc")))
    #define RAM_DATA    __attribute__((section("RamData")))
#else
    #define RAM_FUNC
    #define RAM_DATA
#endif

typedef enum {
    FLASH_OK = 0,
    FLASH_ERROR_TIMEOUT,
    FLASH_ERROR_BUSY,
    FLASH_ERROR_WRITE_ENABLE,
    FLASH_ERROR_PROGRAM,
    FLASH_ERROR_ERASE,
    FLASH_ERROR_VERIFY,
    FLASH_ERROR_INVALID_PARAM
} FlashStatus_t;



RAM_FUNC int Test_Flash_Write(void);

RAM_FUNC FlashStatus_t Store_Firmware(uint8_t *fw_data, uint32_t fw_size);
void store_crc(uint32_t crc);

#endif /* INC_XSPI2_FLASH_DRIVER_H_ */
