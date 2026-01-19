#ifndef __MODEM_H
#define __MODEM_H

#include "main.h"
#include "usb_host.h"
#include <string.h>
#include <stdio.h>
#include "usbh_def.h"
#include "usbh_cdc.h"


typedef enum {
    MODEM_OK = 0,
    MODEM_ERROR,
    MODEM_TIMEOUT,
    MODEM_NOT_READY,
    MODEM_CME_ERROR
} Modem_Status_t;


/* Functions */
Modem_Status_t Modem_Init(void);
Modem_Status_t Modem_PowerOn(void);
Modem_Status_t Modem_PowerOff(void);
uint8_t Modem_IsReady(void);

Modem_Status_t Modem_SendCommand(const char *cmd, char *response, uint32_t maxLen, uint32_t timeout);
void Modem_SendRaw(const char *cmd);
void Modem_PollUSB(uint32_t ms);

Modem_Status_t Modem_CheckNetwork(void);
Modem_Status_t Modem_SetupDataConnection(const char *apn);
Modem_Status_t Modem_HTTP_GET(const char *url, char *response, uint32_t maxLen);
Modem_Status_t Modem_TestHTTP(void);

const char* USBH_GetStateString(USBH_HandleTypeDef *phost);
Modem_Status_t Modem_WaitForHTTPAction(int method, uint32_t timeout, int *httpStatus, uint32_t *dataLen);


Modem_Status_t Modem_SendCommandWaitURC(const char *cmd, const char *expectedURC,
                                         char *response, uint32_t maxLen,
                                         uint32_t timeout);

/* OTA Status codes */
typedef enum {
    OTA_OK = 0,
    OTA_ERROR,
    OTA_TIMEOUT,
    OTA_HTTP_ERROR,
    OTA_SIZE_ERROR,
    OTA_FLASH_ERROR
} OTA_Status_t;

/* Function prototypes */
Modem_Status_t Modem_SSL_HTTPS_GET(const char *url, uint8_t *dataBuffer, uint32_t bufferSize,
                                    uint32_t *totalReceived, uint8_t followRedirects);
Modem_Status_t OTA_TestDownload(void);
OTA_Status_t OTA_DownloadFirmware_v2(const char *url, uint32_t *downloadedSize);






void Modem_TestHTTPS_OTA(void);
uint8_t* OTA_GetFirmwareBuffer(void);
uint32_t OTA_GetFirmwareSize(void);
void OTA_TestChunkSizes(void);

#endif
