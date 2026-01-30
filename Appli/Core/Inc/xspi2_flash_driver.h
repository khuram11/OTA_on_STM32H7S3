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


RAM_FUNC int Test_Flash_Write(void);

#endif /* INC_XSPI2_FLASH_DRIVER_H_ */
