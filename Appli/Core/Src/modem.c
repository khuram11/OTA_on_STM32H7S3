/**
 ******************************************************************************
 * @file    modem.c
 * @brief   5G Modem AT Command Interface for SIM8262E-M2 - HTTP Test
 ******************************************************************************
 */

#include "modem.h"


/* External declarations */
extern uint8_t USB_CDC_IsReady(void);
extern void USB_CDC_Process(void);
extern void USB_CDC_StartReceive(void);
extern void USB_CDC_ProcessReceive(void);
extern void USB_CDC_FlushRx(void);
extern uint32_t USB_CDC_GetRxAvailable(void);
extern uint32_t USB_CDC_Read(uint8_t *data, uint32_t maxLen);
extern HAL_StatusTypeDef USB_CDC_Transmit(uint8_t *data, uint32_t length, uint32_t timeout);
extern void MX_USB_HOST_Process(void);

extern USBH_HandleTypeDef hUsbHostHS;
extern ApplicationTypeDef Appli_state;

static uint8_t modemInitialized = 0;



/*============================================================================*/
/*                          HELPER FUNCTIONS                                  */
/*============================================================================*/

const char* USBH_GetStateString(USBH_HandleTypeDef *phost)
{
    switch (phost->gState)
    {
        case HOST_IDLE:         return "IDLE";
        case HOST_ENUMERATION:  return "ENUMERATION";
        case HOST_CLASS:        return "CLASS_ACTIVE";
        case HOST_ABORT_STATE:  return "ABORT";
        default:                return "OTHER";
    }
}

void Modem_PollUSB(uint32_t ms)
{
	USBH_CDC_Stop(&hUsbHostHS);
	USB_CDC_StartReceive();
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < ms)
    {

        MX_USB_HOST_Process();

        HAL_Delay(100);
    }
}

static void Modem_Reset(void)
{
    HAL_GPIO_WritePin(MODEM_RESET_GPIO_Port, MODEM_RESET_Pin, GPIO_PIN_SET);
    HAL_Delay(200);
    HAL_GPIO_WritePin(MODEM_RESET_GPIO_Port, MODEM_RESET_Pin, GPIO_PIN_RESET);
    HAL_Delay(200);
}

/*============================================================================*/
/*                          POWER CONTROL                                     */
/*============================================================================*/

Modem_Status_t Modem_PowerOn(void)
{
    HAL_GPIO_WritePin(MODEM_PWR_EN_GPIO_Port, MODEM_PWR_EN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MODEM_PWR_OFF_GPIO_Port, MODEM_PWR_OFF_Pin, GPIO_PIN_RESET);
    HAL_Delay(2000);
    HAL_GPIO_WritePin(MODEM_PWR_OFF_GPIO_Port, MODEM_PWR_OFF_Pin, GPIO_PIN_SET);
    HAL_Delay(2000);
    return MODEM_OK;
}

Modem_Status_t Modem_PowerOff(void)
{
    HAL_GPIO_WritePin(MODEM_PWR_OFF_GPIO_Port, MODEM_PWR_OFF_Pin, GPIO_PIN_RESET);
    HAL_Delay(3000);
    HAL_GPIO_WritePin(MODEM_PWR_EN_GPIO_Port, MODEM_PWR_EN_Pin, GPIO_PIN_RESET);
    modemInitialized = 0;
    return MODEM_OK;
}

uint8_t Modem_IsReady(void)
{
    return modemInitialized && USB_CDC_IsReady();
}






/*============================================================================*/
/*                          AT COMMAND FUNCTIONS                              */
/*============================================================================*/

Modem_Status_t Modem_SendCommandWaitURC(const char *cmd, const char *expectedURC,
                                         char *response, uint32_t maxLen,
                                         uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    uint32_t idx = 0;
    uint32_t lastPoll = 0;
    uint8_t gotOK = 0;

    if (!USB_CDC_IsReady())
        return MODEM_NOT_READY;

    USB_CDC_FlushRx();
    memset(response, 0, maxLen);
    printf("[TX] %s", cmd);
    if (USB_CDC_Transmit((uint8_t*)cmd, strlen(cmd), 1000) != HAL_OK)
        return MODEM_ERROR;


    while ((HAL_GetTick() - start) < timeout)
    {
        /* Process USB */
        MX_USB_HOST_Process();
        USB_CDC_ProcessReceive();

        if ((HAL_GetTick() - lastPoll) >= 50)
        {
            USB_CDC_StartReceive();
            lastPoll = HAL_GetTick();
        }

        uint32_t available = USB_CDC_GetRxAvailable();
        if (available > 0)
        {
            uint8_t buf[256];
            uint32_t len = USB_CDC_Read(buf, sizeof(buf));

            if ((idx + len) < (maxLen - 1))
            {
                memcpy(&response[idx], buf, len);
                idx += len;
                response[idx] = '\0';
            }

            /* Check for OK (command accepted) */
            if (strstr(response, "OK") != NULL)
            {
                gotOK = 1;
            }

            /* Check for expected URC */
            if (strstr(response, expectedURC) != NULL)
            {
                printf("[RX] %s\r\n", response);
                return MODEM_OK;
            }

            /* Check for ERROR */
            if (strstr(response, "ERROR") != NULL)
            {
                printf("[RX ERROR] %s\r\n", response);
                return MODEM_ERROR;
            }

            /* Check for connection closed */
            if (strstr(response, "+CCHCLOSE:") != NULL && strstr(response, expectedURC) == NULL)
            {
                printf("[RX CLOSED] %s\r\n", response);
                return MODEM_ERROR;
            }
        }

        /* Progress indicator every 10 seconds */
        static uint32_t lastProgress = 0;
        if ((HAL_GetTick() - lastProgress) >= 10000)
        {
            printf("    Still waiting... (%lu sec) gotOK=%d\r\n",
                   (HAL_GetTick() - start) / 1000, gotOK);
            lastProgress = HAL_GetTick();
        }

        HAL_Delay(10);
    }

    printf("[RX TIMEOUT] Response length: %lu bytes\r\n", idx);
    return MODEM_TIMEOUT;
}


Modem_Status_t Modem_SendCommand(const char *cmd, char *response, uint32_t maxLen, uint32_t timeout)
{
    if (!USB_CDC_IsReady())
        return MODEM_NOT_READY;

    USB_CDC_FlushRx();

    printf("[TX] %s", cmd);
    if (USB_CDC_Transmit((uint8_t*)cmd, strlen(cmd), 1000) != HAL_OK)
        return MODEM_ERROR;

    uint32_t start = HAL_GetTick();
    uint32_t idx = 0;
    uint32_t lastPoll = 0;

    memset(response, 0, maxLen);

    while ((HAL_GetTick() - start) < timeout)
    {
        USB_CDC_Process();

        if ((HAL_GetTick() - lastPoll) >= 20)
        {
            USB_CDC_StartReceive();
            lastPoll = HAL_GetTick();
        }

        uint32_t available = USB_CDC_GetRxAvailable();
        if (available > 0)
        {
            uint8_t buf[128];
            uint32_t len = USB_CDC_Read(buf, sizeof(buf));

            if ((idx + len) < (maxLen - 1))
            {
                memcpy(&response[idx], buf, len);
                idx += len;
                response[idx] = '\0';
            }

            if (strstr(response, "OK\r\n") != NULL)
            {
            	printf("[RAW] %s", response);
                printf("[RES] OK\r\n");
                return MODEM_OK;
            }
            if (strstr(response, "ERROR") != NULL)
            {
            	printf("[RAW] %s", response);
                printf("[RX] ERROR\r\n");
                return MODEM_ERROR;
            }
        }

        HAL_Delay(5);
    }

    printf("[RX] TIMEOUT\r\n");
    return MODEM_TIMEOUT;
}

void Modem_SendRaw(const char *cmd)
{
    if (!USB_CDC_IsReady())
        return;

    printf("[TX] %s", cmd);
    USB_CDC_Transmit((uint8_t*)cmd, strlen(cmd), 1000);
}

/*============================================================================*/
/*                          NETWORK FUNCTIONS                                 */
/*============================================================================*/

Modem_Status_t Modem_CheckNetwork(void)
{
    char response[256];

    printf("\r\n=== NETWORK STATUS ===\r\n");

    Modem_SendCommand("AT+CPIN?\r\n", response, sizeof(response), 2000);
    Modem_SendCommand("AT+CSQ\r\n", response, sizeof(response), 2000);
    Modem_SendCommand("AT+CGREG?\r\n", response, sizeof(response), 2000);
    Modem_SendCommand("AT+CEREG?\r\n", response, sizeof(response), 1000);
    Modem_SendCommand("AT+COPS?\r\n", response, sizeof(response), 2000);
    Modem_SendCommand("AT+CPSI?\r\n", response, sizeof(response), 2000);
    Modem_SendCommand("AT+CGACT=1,1\r\n", response, sizeof(response), 2000);
    HAL_Delay(2000);
    Modem_SendCommand("AT+CGACT?\r\n", response, sizeof(response), 2000);

    printf("======================\r\n\r\n");

    return MODEM_OK;
}

Modem_Status_t Modem_SetupDataConnection(const char *apn)
{
    char response[256];
    char cmd[128];

    printf("\r\n=== SETUP DATA CONNECTION ===\r\n");

    /* Deactivate existing PDP */
    printf("[1] Deactivating existing PDP...\r\n");
    Modem_SendCommand("AT+CGACT=0,1\r\n", response, sizeof(response), 5000);
    Modem_PollUSB(1000);

    /* Set APN */
    printf("[2] Setting APN: %s\r\n", apn);
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"\r\n", apn);
    if (Modem_SendCommand(cmd, response, sizeof(response), 2000) != MODEM_OK)
    {
        printf("[ERROR] Failed to set APN!\r\n");
        return MODEM_ERROR;
    }

    /* Activate PDP */
    printf("[3] Activating PDP...\r\n");
    if (Modem_SendCommand("AT+CGACT=1,1\r\n", response, sizeof(response), 30000) != MODEM_OK)
    {
        printf("[ERROR] Failed to activate PDP!\r\n");
        return MODEM_ERROR;
    }

    /* Get IP address */
    printf("[4] Getting IP address...\r\n");
    Modem_SendCommand("AT+CGPADDR=1\r\n", response, sizeof(response), 2000);

    printf("=============================\r\n\r\n");

    return MODEM_OK;
}

/*============================================================================*/
/*                          HTTP FUNCTIONS                                    */
/*============================================================================*/

Modem_Status_t Modem_HTTP_GET(const char *url, char *response, uint32_t maxLen)
{
//    char cmd[256];

    printf("\r\n=== HTTP GET ===\r\n");
    printf("URL: %s\r\n", url);

    /* Initialize HTTP */
    printf("[1] Init HTTP...\r\n");
    Modem_SendCommand("AT+HTTPINIT\r\n", response, maxLen, 2000);

    Modem_SendCommand("AT+HTTPPARA=\"URL\",\"https://temp.staticsave.com/695294fc3309c.css", response, maxLen, 2000);
    Modem_SendCommand("AT+HTTPACTION=0\r\n", response, maxLen, 2000);
    Modem_SendCommand("AT+HTTPHEAD\r\n", response, maxLen, 2000);
    Modem_SendCommand("AT+HTTPREAD=0,28\r\n", response, maxLen, 2000);

    return  MODEM_OK;
}


/**
 * @brief  Print any data in the receive buffer
 */
static void Modem_PrintRxBuffer(void)
{
    uint8_t buf[256];
    uint32_t available = USB_CDC_GetRxAvailable();

    while (available > 0)
    {
        uint32_t len = USB_CDC_Read(buf, sizeof(buf) - 1);
        if (len == 0) break;

        buf[len] = '\0';

        printf("[RX]: ");
        for (uint32_t i = 0; i < len; i++)
        {
            char c = buf[i];
            if (c >= 32 && c <= 126)
                printf("%c", c);
            else if (c == '\r')
                printf("<CR>");
            else if (c == '\n')
                printf("<LF>\r\n");
            else
                printf("[%02X]", c);
        }
        printf("\r\n");

        available = USB_CDC_GetRxAvailable();
    }
}

/*============================================================================*/
/*                          INITIALIZATION                                    */
/*============================================================================*/

/**
 * @brief  Wait for modem to respond to AT commands
 *         Sends AT every 2 seconds until OK received
 */
Modem_Status_t Modem_WaitForATReady(uint32_t timeout)
{
    char response[128];
    uint32_t start = HAL_GetTick();
    int attempts = 0;

    printf("[STEP 5] Waiting for modem AT response...\r\n");

    while ((HAL_GetTick() - start) < timeout)
    {
        attempts++;
        printf("  Attempt %d (%lu ms)...\r\n", attempts, HAL_GetTick() - start);

        /* Flush any pending data */
        USB_CDC_FlushRx();

        /* Send AT command */
        if (USB_CDC_Transmit((uint8_t*)"AT\r\n", 4, 500) == HAL_OK)
        {
            /* Wait for response */
            uint32_t waitStart = HAL_GetTick();
            memset(response, 0, sizeof(response));
            uint32_t idx = 0;

            while ((HAL_GetTick() - waitStart) < 1500)  /* 1.5 sec wait for response */
            {
                MX_USB_HOST_Process();
                USB_CDC_StartReceive();
                HAL_Delay(50);
                MX_USB_HOST_Process();
                USB_CDC_ProcessReceive();

                uint32_t available = USB_CDC_GetRxAvailable();
                if (available > 0)
                {
                    uint8_t buf[64];
                    uint32_t len = USB_CDC_Read(buf, sizeof(buf) - 1);
                    buf[len] = '\0';

                    if ((idx + len) < sizeof(response) - 1)
                    {
                        memcpy(&response[idx], buf, len);
                        idx += len;
                        response[idx] = '\0';
                    }

                    /* Check for OK */
                    if (strstr(response, "OK") != NULL)
                    {
                        printf("  Modem ready! (attempt %d, %lu ms)\r\n",
                               attempts, HAL_GetTick() - start);
                        printf("[STEP 5] Done\r\n\r\n");
                        return MODEM_OK;
                    }
                }
            }
        }

        /* Wait remaining time to make 2 second interval */
        uint32_t elapsed = HAL_GetTick() - start;
        uint32_t nextAttempt = attempts * 2000;
        if (nextAttempt > elapsed)
        {
            HAL_Delay(nextAttempt - elapsed);
        }
    }

    printf("  Timeout - no response after %d attempts!\r\n", attempts);
    printf("[STEP 5] FAILED\r\n\r\n");
    return MODEM_TIMEOUT;
}

Modem_Status_t Modem_Init(void)
{
    char response[256];

    printf("\r\n");
    printf("##################################################\r\n");
    printf("#            MODEM INIT START                    #\r\n");
    printf("##################################################\r\n\r\n");

    /*--- Step 1: Power On ---*/
    printf("[STEP 1] Powering on modem...\r\n");
    Modem_PowerOn();
    printf("[STEP 1] Done\r\n\r\n");

    /*--- Step 2: Reset ---*/
    printf("[STEP 2] Resetting modem...\r\n");
    Modem_Reset();
    printf("[STEP 2] Done\r\n\r\n");

    /*--- Step 3: Disable Airplane Mode ---*/
    printf("[STEP 3] Disabling airplane mode...\r\n");
    HAL_GPIO_WritePin(MODEM_W_DIS1_GPIO_Port, MODEM_W_DIS1_Pin, GPIO_PIN_SET);
    HAL_Delay(500);
    printf("[STEP 3] Done\r\n\r\n");

    /*--- Step 4: Wait for USB CDC ---*/
    printf("[STEP 4] Waiting for USB CDC...\r\n");
    uint32_t startTick = HAL_GetTick();

    while (!USB_CDC_IsReady())
    {
        MX_USB_HOST_Process();

        if ((HAL_GetTick() - startTick) > 30000)
        {
            printf("[ERROR] USB CDC timeout!\r\n");
            printf("##################################################\r\n");
            printf("#            MODEM INIT FAILED                   #\r\n");
            printf("##################################################\r\n\r\n");
            return MODEM_TIMEOUT;
        }

        if ((HAL_GetTick() - startTick) % 5000 < 10)
        {
            printf("  Waiting... (%lu ms) State: %s\r\n",
                   HAL_GetTick() - startTick,
                   USBH_GetStateString(&hUsbHostHS));
        }

        HAL_Delay(10);
    }
    printf("[STEP 4] USB CDC ready! (%lu ms)\r\n\r\n", HAL_GetTick() - startTick);

    /*--- Step 5: Wait for Modem AT Ready ---*/
    if (Modem_WaitForATReady(60000) != MODEM_OK)  /* 60 second timeout */
    {
        printf("[ERROR] Modem not responding to AT commands!\r\n");
        printf("##################################################\r\n");
        printf("#            MODEM INIT FAILED                   #\r\n");
        printf("##################################################\r\n\r\n");
        return MODEM_TIMEOUT;
    }

    /*--- Step 6: Configure and Test ---*/
    printf("##################################################\r\n");
    printf("#              AT COMMAND TESTS                  #\r\n");
    printf("##################################################\r\n\r\n");

    /* Disable echo */
    printf("--- Test 1: ATE0 (Disable Echo) ---\r\n");
    Modem_SendCommand("ATE0\r\n", response, sizeof(response), 2000);
    printf("\r\n");

    /* Basic AT */
    printf("--- Test 2: AT ---\r\n");
    Modem_SendCommand("AT\r\n", response, sizeof(response), 1000);
    printf("\r\n");

    /* Module Info */
    printf("--- Test 3: ATI (Module Info) ---\r\n");
    if (Modem_SendCommand("ATI\r\n", response, sizeof(response), 2000) == MODEM_OK)
    {
        printf("  Info: %s\r\n", response);
    }
    printf("\r\n");

    /* IMEI */
    printf("--- Test 4: AT+CGSN (IMEI) ---\r\n");
    if (Modem_SendCommand("AT+CGSN\r\n", response, sizeof(response), 1000) == MODEM_OK)
    {
        printf("  IMEI: %s\r\n", response);
    }
    printf("\r\n");

    /* SIM Status */
    printf("--- Test 5: AT+CPIN? (SIM Status) ---\r\n");
    if (Modem_SendCommand("AT+CPIN?\r\n", response, sizeof(response), 1000) == MODEM_OK)
    {
        if (strstr(response, "READY"))
            printf("  SIM: READY\r\n");
        else
            printf("  SIM: %s\r\n", response);
    }
    printf("\r\n");

    /* Signal Strength */
    printf("--- Test 6: AT+CSQ (Signal) ---\r\n");
    if (Modem_SendCommand("AT+CSQ\r\n", response, sizeof(response), 1000) == MODEM_OK)
    {
        int rssi = 0, ber = 0;
        char *p = strstr(response, "+CSQ:");
        if (p && sscanf(p, "+CSQ: %d,%d", &rssi, &ber) == 2)
        {
            int dbm = (rssi == 99) ? -999 : (-113 + rssi * 2);
            printf("  Signal: %d dBm (rssi=%d)\r\n", dbm, rssi);
        }
    }
    printf("\r\n");

    /* Network Registration */
    printf("--- Test 7: AT+CREG? (Network) ---\r\n");
    Modem_SendCommand("AT+CREG?\r\n", response, sizeof(response), 1000);
    printf("\r\n");

    /* Operator */
    printf("--- Test 8: AT+COPS? (Operator) ---\r\n");
    if (Modem_SendCommand("AT+COPS?\r\n", response, sizeof(response), 2000) == MODEM_OK)
    {
        char *start = strstr(response, "\"");
        if (start)
        {
            start++;
            char *end = strstr(start, "\"");
            if (end)
            {
                *end = '\0';
                printf("  Operator: %s\r\n", start);
            }
        }
    }
    printf("\r\n");

    /*--- Complete ---*/
    modemInitialized = 1;

    printf("##################################################\r\n");
    printf("#            MODEM INIT COMPLETE                 #\r\n");
    printf("##################################################\r\n\r\n");

    return MODEM_OK;
}


/**
 * @brief  Read HTTP response data with proper parsing
 */
Modem_Status_t Modem_HTTP_ReadData(uint32_t offset, uint32_t length, char *data, uint32_t maxLen)
{
    char cmd[64];
    char response[2048];
    uint32_t idx = 0;

    snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=%lu,%lu\r\n", offset, length);
    printf("[TX] %s", cmd);

    USB_CDC_FlushRx();

    if (USB_CDC_Transmit((uint8_t*)cmd, strlen(cmd), 1000) != HAL_OK)
        return MODEM_ERROR;

    memset(response, 0, sizeof(response));

    /* Wait for complete response including +HTTPREAD: 0 */
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < 10000)
    {
        MX_USB_HOST_Process();
        USB_CDC_StartReceive();
        HAL_Delay(100);
        MX_USB_HOST_Process();
        USB_CDC_ProcessReceive();

        uint32_t available = USB_CDC_GetRxAvailable();
        if (available > 0)
        {
            uint8_t buf[256];
            uint32_t len = USB_CDC_Read(buf, sizeof(buf) - 1);
            buf[len] = '\0';

            if ((idx + len) < sizeof(response) - 1)
            {
                memcpy(&response[idx], buf, len);
                idx += len;
                response[idx] = '\0';
            }

            /* Check for completion: +HTTPREAD: 0 indicates end of data */
            if (strstr(response, "+HTTPREAD: 0") != NULL)
            {
                break;
            }

            /* Also check for OK at end */
            if (strstr(response, "\r\nOK\r\n") != NULL && idx > 50)
            {
                break;
            }
        }
    }

    printf("[RAW Response] %s\r\n", response);

    /* Parse the data - find +HTTPREAD: DATA,<len> then extract data */
    char *dataStart = strstr(response, "+HTTPREAD: DATA,");
    if (dataStart)
    {
        /* Skip to after the header line */
        char *lineEnd = strstr(dataStart, "\r\n");
        if (lineEnd)
        {
            lineEnd += 2;  /* Skip \r\n */

            /* Find +HTTPREAD: 0 which marks end of data */
            char *dataEnd = strstr(lineEnd, "\r\n+HTTPREAD: 0");
            if (dataEnd)
            {
                uint32_t dataLen = dataEnd - lineEnd;
                if (dataLen > maxLen - 1) dataLen = maxLen - 1;

                memcpy(data, lineEnd, dataLen);
                data[dataLen] = '\0';

                return MODEM_OK;
            }
        }
    }

    /* If parsing failed, just copy what we got */
    strncpy(data, response, maxLen - 1);
    data[maxLen - 1] = '\0';

    return MODEM_OK;
}



Modem_Status_t Modem_WaitForHTTPAction(int method, uint32_t timeout, int *httpStatus, uint32_t *dataLen)
{
    char response[512];
    uint32_t start = HAL_GetTick();
    uint32_t idx = 0;

    *httpStatus = 0;
    *dataLen = 0;

    /* Send command */
    USB_CDC_FlushRx();

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+HTTPACTION=%d\r\n", method);
    printf("[TX] %s", cmd);

    if (USB_CDC_Transmit((uint8_t*)cmd, strlen(cmd), 1000) != HAL_OK)
    {
        return MODEM_ERROR;
    }

    memset(response, 0, sizeof(response));

    /* Wait for +HTTPACTION URC */
    while ((HAL_GetTick() - start) < timeout)
    {
        MX_USB_HOST_Process();
        USB_CDC_StartReceive();
        HAL_Delay(500);
        MX_USB_HOST_Process();
        USB_CDC_ProcessReceive();

        uint32_t available = USB_CDC_GetRxAvailable();
        if (available > 0)
        {
            uint8_t buf[128];
            uint32_t len = USB_CDC_Read(buf, sizeof(buf) - 1);
            buf[len] = '\0';

            if ((idx + len) < sizeof(response) - 1)
            {
                memcpy(&response[idx], buf, len);
                idx += len;
                response[idx] = '\0';
            }

            printf("[RX] %s\r\n", buf);

            /* Parse +HTTPACTION URC */
            char *p = strstr(response, "+HTTPACTION:");
            if (p)
            {
                int m;
                if (sscanf(p, "+HTTPACTION: %d,%d,%lu", &m, httpStatus, dataLen) >= 2)
                {
                    printf("    Status: %d, Length: %lu\r\n", *httpStatus, *dataLen);
                    return MODEM_OK;  /* Return OK even if HTTP status != 200 */
                }
            }
        }

        /* Progress indicator */
        static uint32_t lastPrint = 0;
        if ((HAL_GetTick() - lastPrint) > 5000)
        {
            printf("    Waiting... (%lu sec)\r\n", (HAL_GetTick() - start) / 1000);
            lastPrint = HAL_GetTick();
        }
    }

    return MODEM_TIMEOUT;
}

Modem_Status_t Modem_HTTP_SimpleTest(void)
{
    char response[512];
    char httpData[1024];
    int httpStatus = 0;
    uint32_t dataLen = 0;

    printf("\r\n========== HTTP GET TEST ==========\r\n");

    /* Cleanup */
    printf("[0] Cleanup...\r\n");
    Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 2000);
    HAL_Delay(500);

    /* HTTPINIT */
    printf("[1] AT+HTTPINIT\r\n");
    if (Modem_SendCommand("AT+HTTPINIT\r\n", response, sizeof(response), 2000) != MODEM_OK)
    {
        printf("    FAILED!\r\n");
        return MODEM_ERROR;
    }
    HAL_Delay(300);

    /* Set URL */
    printf("[2] AT+HTTPPARA URL\r\n");
    if (Modem_SendCommand("AT+HTTPPARA=\"URL\",\"https://raw.githubusercontent.com/khuram11/ota_test/main/fw_with_crc.bin\"\r\n",
                          response, sizeof(response), 2000) != MODEM_OK)
    {
        printf("    FAILED!\r\n");
        Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 1000);
        return MODEM_ERROR;
    }
    HAL_Delay(300);

    /* HTTP GET */
    printf("[3] AT+HTTPACTION=0\r\n");
    if (Modem_WaitForHTTPAction(0, 60000, &httpStatus, &dataLen) != MODEM_OK)
    {
        printf("    TIMEOUT!\r\n");
        Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 1000);
        return MODEM_TIMEOUT;
    }

    printf("    HTTP Status: %d\r\n", httpStatus);
    printf("    Data Length: %lu bytes\r\n", dataLen);

    if (httpStatus != 200)
    {
        printf("    HTTP Error!\r\n");
        Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 1000);
        return MODEM_ERROR;
    }
    HAL_Delay(2000);

    /* Read headers */
    printf("\n\n\n[4] AT+HTTPHEAD\r\n");
    Modem_SendCommand("AT+HTTPHEAD\r\n", response, sizeof(response), 5000);
    printf("--- HEADERS ---\r\n%s\r\n---------------\r\n", response);
    HAL_Delay(5000);

    printf("---------- Checking buffer len --------------");

    Modem_SendCommandWaitURC("AT+HTTPREAD?\r\n", "+HTTPREAD",  response, sizeof(response), 5000);
    HAL_Delay(5000);
    printf("----------[4] Reading the buffer --------------\n");

    // Manual test - don't use Modem_SendCommandWaitURC for binary data
    USB_CDC_FlushRx();
    printf("[TX] AT+HTTPREAD=0,64\r\n");  // Try smaller chunk first
    USB_CDC_Transmit((uint8_t*)"AT+HTTPREAD=0,64\r\n", sizeof "AT+HTTPREAD=0,64\r\n", 1000);

    // Wait and collect raw bytes
    uint8_t rawBuf[1250];
    uint32_t rawIdx = 0;
    uint32_t startTime = HAL_GetTick();

    while ((HAL_GetTick() - startTime) < 5000)  // 5 second timeout
    {
        MX_USB_HOST_Process();
        USB_CDC_StartReceive();
        HAL_Delay(100);
        MX_USB_HOST_Process();
        USB_CDC_ProcessReceive();

        uint32_t available = USB_CDC_GetRxAvailable();
        if (available > 0)
        {
            uint8_t buf[1024];
            uint32_t len = USB_CDC_Read(buf, sizeof(buf));

            if ((rawIdx + len) < sizeof(rawBuf))
            {
                memcpy(&rawBuf[rawIdx], buf, len);
                rawIdx += len;
            }

            printf("[GOT %lu bytes, total %lu]\r\n", len, rawIdx);

            // Print what we got as hex
            for (uint32_t i = 0; i < len && i < 32; i++)
            {
                printf("%02X ", buf[i]);
            }
            printf("\r\n");

            // Check if we have enough (should be ~100+ bytes for 64 byte read)
            if (rawIdx > 100)
            {
                break;
            }
        }
    }

    printf("\r\n[FINAL] Total received: %lu bytes\r\n", rawIdx);
    printf("[FINAL HEX DUMP]:\r\n");
    for (uint32_t i = 0; i < rawIdx && i < 150; i++)
    {
        if (i % 16 == 0) printf("\r\n%04lX: ", i);
        printf("%02X ", rawBuf[i]);
    }
    printf("\r\n");

//    /* Read data with proper parsing */
//    printf("\n\n\n[5] AT+HTTPREAD\r\n");
//    uint32_t toRead = (dataLen > 512) ? 512 : dataLen;
//
//    if (Modem_HTTP_ReadData(0, toRead, httpData, sizeof(httpData)) == MODEM_OK)
//    {
//        printf("\r\n========== HTTP RESPONSE DATA ==========\r\n");
//        printf("%s\r\n", httpData);
//        printf("=========================================\r\n\r\n");
//    }
//    HAL_Delay(300);

    /* Terminate */
    printf("[6] AT+HTTPTERM\r\n");
    Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 2000);

    printf("========== TEST COMPLETE ==========\r\n\r\n");

    return MODEM_OK;
}



Modem_Status_t Modem_TestHTTP(void)
{

    printf("\r\n##################################################\r\n");
    printf("#              HTTP TEST START                   #\r\n");
    printf("##################################################\r\n\r\n");

    /* Step 1: Check network */
    printf("=== Step 1: Check Network ===\r\n");
    Modem_CheckNetwork();


    printf("Test 1\r\n");
    HAL_Delay(100);
    printf("Test 2\r\n");
    HAL_Delay(100);
    printf("Test 3\r\n");
    char response[512];

    Modem_SendCommand("AT+GARBAGE\r\n", response, sizeof(response), 3000);

    printf("Test 4\r\n");
    HAL_Delay(100);
    printf("Test 5\r\n");

    /* Step 2: Simple HTTP test */
    printf("=== Step 2: Simple HTTP Test ===\r\n");
    Modem_HTTP_SimpleTest();

    printf("\r\n##################################################\r\n");
    printf("#              HTTP TEST COMPLETE                #\r\n");
    printf("##################################################\r\n\r\n");

    return MODEM_OK;
}


/*============================================================================*/
/*                          OTA FIRMWARE DOWNLOAD                             */
/*============================================================================*/

#define OTA_CHUNK_SIZE      330
#define OTA_MAX_FW_SIZE     (50 * 1024)  /* 400KB max firmware */
#define OTA_READ_TIMEOUT    10000

/* Global firmware buffer - allocate in RAM */
static uint8_t g_fwBuffer[OTA_MAX_FW_SIZE];
static uint32_t g_fwSize = 0;
static uint32_t g_fwDownloaded = 0;

/**
 * @brief  Get pointer to firmware buffer
 */
uint8_t* OTA_GetFirmwareBuffer(void)
{
    return g_fwBuffer;
}

/**
 * @brief  Get downloaded firmware size
 */
uint32_t OTA_GetFirmwareSize(void)
{
    return g_fwDownloaded;
}

/**
 * @brief  Find byte pattern in buffer (memcmp based search)
 * @param  haystack: Buffer to search in
 * @param  haystackLen: Length of buffer
 * @param  needle: Pattern to find
 * @param  needleLen: Length of pattern
 * @retval Pointer to found pattern, or NULL if not found
 */
static uint8_t* OTA_FindPattern(uint8_t *haystack, uint32_t haystackLen,
                                 const char *needle, uint32_t needleLen)
{
    if (haystackLen < needleLen)
    {
        return NULL;
    }

    uint32_t searchLen = haystackLen - needleLen + 1;

    for (uint32_t i = 0; i < searchLen; i++)
    {
        if (memcmp(&haystack[i], needle, needleLen) == 0)
        {
            return &haystack[i];
        }
    }

    return NULL;
}

/**
 * @brief  Find \r\n sequence in buffer
 * @param  buffer: Buffer to search
 * @param  maxLen: Maximum search length
 * @retval Pointer to \r\n, or NULL if not found
 */
static uint8_t* OTA_FindLineEnd(uint8_t *buffer, uint32_t maxLen)
{
    for (uint32_t i = 0; i < maxLen - 1; i++)
    {
        if (buffer[i] == '\r' && buffer[i + 1] == '\n')
        {
            return &buffer[i];
        }
    }

    return NULL;
}

/**
 * @brief  Parse chunk length from +HTTPREAD response
 * @param  dataMarker: Pointer to +HTTPREAD: in response
 * @retval Parsed length, or 0 on error
 */
static uint32_t OTA_ParseChunkLength(uint8_t *dataMarker)
{
    uint32_t chunkLen = 0;
    char *p = (char*)dataMarker;

    /* Skip "+HTTPREAD: " or "+HTTPREAD: DATA," */
    p = strchr(p, ':');
    if (p == NULL)
    {
        return 0;
    }
    p++;  /* Skip ':' */

    /* Skip spaces */
    while (*p == ' ')
    {
        p++;
    }

    /* Check for "DATA," format */
    if (strncmp(p, "DATA,", 5) == 0)
    {
        p += 5;
    }

    /* Parse number */
    chunkLen = atoi(p);

    return chunkLen;
}

Modem_Status_t OTA_ReadBinaryChunk(uint32_t offset, uint32_t length, uint8_t *buffer, uint32_t *bytesRead)
{
    char cmd[64];
    uint8_t rxBuffer[1024];
    uint32_t rxIdx = 0;
    uint32_t start = HAL_GetTick();
    uint8_t endMarkerFound = 0;

    *bytesRead = 0;

    /* Send HTTPREAD command */

    snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=%lu,%lu\r\n", offset, length);

    USB_CDC_FlushRx();
    memset(rxBuffer, 0, sizeof(rxBuffer));

    printf("[TX] %s", cmd);
    if (USB_CDC_Transmit((uint8_t*)cmd, strlen(cmd), 1000) != HAL_OK)
    {
        return MODEM_ERROR;
    }

    /* Collect response - wait for +HTTPREAD: 0 end marker */
    while ((HAL_GetTick() - start) < 10000 && !endMarkerFound)
    {
        MX_USB_HOST_Process();
        USB_CDC_StartReceive();
        HAL_Delay(2);
        MX_USB_HOST_Process();
        USB_CDC_ProcessReceive();

        uint32_t available = USB_CDC_GetRxAvailable();
        if (available > 0)
        {
            uint8_t buf[1024];
            uint32_t len = USB_CDC_Read(buf, sizeof(buf));

            if ((rxIdx + len) < sizeof(rxBuffer))
            {
                memcpy(&rxBuffer[rxIdx], buf, len);
                rxIdx += len;
            }

            /* Look for +HTTPREAD: 0 end marker using binary-safe search */
            if (rxIdx >= 14)  /* Minimum length for "\r\n+HTTPREAD: 0" */
            {
                if (OTA_FindPattern(rxBuffer, rxIdx, "+HTTPREAD: 0", 12) != NULL)
                {
                    endMarkerFound = 1;
                }
            }
        }
    }

    if (!endMarkerFound)
    {
        printf("[OTA] Timeout - no end marker. Received %lu bytes\r\n", rxIdx);
        /* Debug: print what we got */
        printf("[OTA] First 64 bytes: ");
        for (uint32_t i = 0; i < 64 && i < rxIdx; i++)
        {
            printf("%02X ", rxBuffer[i]);
        }
        printf("\r\n");
        return MODEM_TIMEOUT;
    }

    /*
     * Parse response format:
     * \r\nOK\r\n\r\n+HTTPREAD: DATA,<len>\r\n<binary_data>\r\n+HTTPREAD: 0\r\n
     */

    /* Find "+HTTPREAD: DATA," or "+HTTPREAD: " with length */
    uint8_t *dataMarker = OTA_FindPattern(rxBuffer, rxIdx, "+HTTPREAD: DATA,", 16);

    if (dataMarker == NULL)
    {
        /* Try alternate format: +HTTPREAD: <len> */
        dataMarker = OTA_FindPattern(rxBuffer, rxIdx, "+HTTPREAD: ", 11);

        if (dataMarker != NULL)
        {
            /* Make sure it's not "+HTTPREAD: 0" (end marker) or "+HTTPREAD: LEN" */
            char nextChar = (char)dataMarker[11];
            if (nextChar == '0' || nextChar == 'L')
            {
                /* This is the end marker or LEN response, not data - search for another */
                uint8_t *searchStart = dataMarker + 1;
                uint32_t remaining = rxIdx - (searchStart - rxBuffer);
                dataMarker = OTA_FindPattern(searchStart, remaining, "+HTTPREAD: ", 11);

                if (dataMarker != NULL)
                {
                    nextChar = (char)dataMarker[11];
                    if (nextChar == '0' || nextChar == 'L')
                    {
                        dataMarker = NULL;  /* Still not the right one */
                    }
                }
            }
        }
    }

    if (dataMarker == NULL)
    {
        printf("[OTA] Could not find data marker in %lu bytes\r\n", rxIdx);
        printf("[OTA] Buffer dump: ");
        for (uint32_t i = 0; i < 100 && i < rxIdx; i++)
        {
            printf("%02X ", rxBuffer[i]);
        }
        printf("\r\n");
        return MODEM_ERROR;
    }

    /* Parse the chunk length */
    uint32_t chunkLen = 0;
    char *lenStart = (char*)dataMarker;

    /* Skip to the number */
    lenStart = strchr(lenStart, ',');  /* Find comma after DATA */
    if (lenStart == NULL)
    {
        lenStart = strchr((char*)dataMarker + 11, ' ');  /* Or space in +HTTPREAD: <len> format */
        if (lenStart != NULL) lenStart++;  /* Skip space */
    }
    else
    {
        lenStart++;  /* Skip comma */
    }

    if (lenStart != NULL)
    {
        chunkLen = atoi(lenStart);
    }

    if (chunkLen == 0 || chunkLen > length)
    {
        printf("[OTA] Invalid chunk length: %lu (expected max %lu)\r\n", chunkLen, length);
        return MODEM_ERROR;
    }

    /* Find end of header line (\r\n after the length) */
    uint8_t *lineEnd = OTA_FindLineEnd(dataMarker, rxIdx - (dataMarker - rxBuffer));
    if (lineEnd == NULL)
    {
        printf("[OTA] Could not find header line end\r\n");
        return MODEM_ERROR;
    }

    uint8_t *binaryStart = lineEnd + 2;  /* Skip \r\n */

    /* Calculate available binary data */
    uint8_t *endMarker = OTA_FindPattern(binaryStart, rxIdx - (binaryStart - rxBuffer), "\r\n+HTTPREAD: 0", 14);

    uint32_t availableData;
    if (endMarker != NULL)
    {
        availableData = endMarker - binaryStart;
    }
    else
    {
        /* Fallback: use chunkLen */
        availableData = rxIdx - (binaryStart - rxBuffer);
    }

    if (availableData < chunkLen)
    {
        printf("[OTA] Not enough data: have %lu, need %lu\r\n", availableData, chunkLen);
        return MODEM_ERROR;
    }

    /* Copy binary data */
    memcpy(buffer, binaryStart, chunkLen);
    *bytesRead = chunkLen;

    return MODEM_OK;
}
/**
 * @brief  Download complete firmware file via HTTP
 * @param  url: URL to firmware binary
 * @retval MODEM_OK on success
 */
Modem_Status_t OTA_DownloadFirmware(const char *url)
{
    char response[512];
    char cmd[512];
    int httpStatus = 0;
    uint32_t totalSize = 0;
    uint32_t downloaded = 0;
    uint32_t bytesRead = 0;
    Modem_Status_t result = MODEM_ERROR;

    printf("\r\n##################################################\r\n");
    printf("#              OTA FIRMWARE DOWNLOAD             #\r\n");
    printf("##################################################\r\n\r\n");

    g_fwDownloaded = 0;
    g_fwSize = 0;

    /* Step 1: Initialize HTTP */
    printf("[OTA] Step 1: Initialize HTTP\r\n");
    Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 2000);
    HAL_Delay(500);

    if (Modem_SendCommand("AT+HTTPINIT\r\n", response, sizeof(response), 2000) != MODEM_OK)
    {
        printf("[OTA] HTTPINIT failed!\r\n");
        return MODEM_ERROR;
    }
    HAL_Delay(300);

    /* Step 2: Set URL */
    printf("[OTA] Step 2: Set URL\r\n");
    printf("       %s\r\n", url);

    snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",\"%s\"\r\n", url);

    if (Modem_SendCommand(cmd, response, sizeof(response), 2000) != MODEM_OK)
    {
        printf("[OTA] Set URL failed!\r\n");
        Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 1000);
        return MODEM_ERROR;
    }
    HAL_Delay(300);

    /* Step 3: Execute GET request */
    printf("[OTA] Step 3: HTTP GET request\r\n");

    if (Modem_WaitForHTTPAction(0, 60000, &httpStatus, &totalSize) != MODEM_OK)
    {
        printf("[OTA] HTTP request timeout!\r\n");
        Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 1000);
        return MODEM_TIMEOUT;
    }

    printf("[OTA] HTTP Status: %d\r\n", httpStatus);
    printf("[OTA] File Size: %lu bytes\r\n", totalSize);

    if (httpStatus != 200)
    {
        printf("[OTA] HTTP Error: %d\r\n", httpStatus);
        Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 1000);
        return MODEM_ERROR;
    }

    if (totalSize > OTA_MAX_FW_SIZE)
    {
        printf("[OTA] File too large! Max: %d bytes\r\n", OTA_MAX_FW_SIZE);
        Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 1000);
        return MODEM_ERROR;
    }


    Modem_SendCommand("AT+HTTPHEAD\r\n", response, sizeof(response), 5000);
	printf("--- HEADERS ---\r\n%s\r\n---------------\r\n", response);
	HAL_Delay(5000);


    g_fwSize = totalSize;
    HAL_Delay(2000);  /* Wait for data to be ready */

    /* Step 4: Check available data */
    printf("[OTA] Step 4: Check buffer\r\n");
    Modem_SendCommandWaitURC("AT+HTTPREAD?\r\n", "+HTTPREAD", response, sizeof(response), 5000);
    HAL_Delay(5000);

    /* Step 5: Download in chunks */
    printf("[OTA] Step 5: Downloading %lu bytes in %lu chunks\r\n",
           totalSize, (totalSize + OTA_CHUNK_SIZE - 1) / OTA_CHUNK_SIZE);

    result = MODEM_OK;

    while (downloaded < totalSize && result == MODEM_OK)
    {
        uint32_t remaining = totalSize - downloaded;
        uint32_t chunkSize = (remaining > OTA_CHUNK_SIZE) ? OTA_CHUNK_SIZE : remaining;

        /* Read chunk */
        result = OTA_ReadBinaryChunk(downloaded, chunkSize, &g_fwBuffer[downloaded], &bytesRead);

        if (result != MODEM_OK)
        {
            printf("[OTA] Failed to read chunk at offset %lu\r\n", downloaded);
        }
        else if (bytesRead == 0)
        {
            printf("[OTA] Zero bytes read at offset %lu\r\n", downloaded);
            result = MODEM_ERROR;
        }
        else
        {
            downloaded += bytesRead;
            g_fwDownloaded = downloaded;

            /* Progress */
            uint32_t percent = (downloaded * 100) / totalSize;
            printf("[OTA] Progress: %lu / %lu bytes (%lu%%)\r\n", downloaded, totalSize, percent);

            HAL_Delay(1);
        }
    }

    /* Cleanup */
    printf("[OTA] Step 6: Cleanup\r\n");
    Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 2000);

    if (result == MODEM_OK)
    {
        printf("\r\n[OTA] Download complete!\r\n");
        printf("[OTA] Total bytes: %lu\r\n", g_fwDownloaded);

        /* Print first 32 bytes as hex for verification */
        printf("[OTA] First 32 bytes: ");
        for (uint32_t i = 0; i < 32 && i < g_fwDownloaded; i++)
        {
            printf("%02X ", g_fwBuffer[i]);
        }
        printf("\r\n");

        printf("\r\n##################################################\r\n");
        printf("#           OTA DOWNLOAD COMPLETE                #\r\n");
        printf("##################################################\r\n\r\n");
    }

    return result;
}

/**
 * @brief  Verify downloaded firmware CRC
 * @param  expectedCRC: Expected CRC32 value
 * @retval MODEM_OK if CRC matches
 */
Modem_Status_t OTA_VerifyFirmwareCRC(uint32_t expectedCRC)
{
    uint32_t crc = 0xFFFFFFFF;

    for (uint32_t i = 0; i < g_fwDownloaded; i++)
    {
        crc ^= g_fwBuffer[i];
        for (uint32_t j = 0; j < 8; j++)
        {
            if (crc & 1)
            {
                crc = (crc >> 1) ^ 0xEDB88320;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    crc ^= 0xFFFFFFFF;

    printf("[OTA] Calculated CRC: 0x%08lX\r\n", crc);
    printf("[OTA] Expected CRC:   0x%08lX\r\n", expectedCRC);

    if (crc == expectedCRC)
    {
        printf("[OTA] CRC VALID!\r\n");
        return MODEM_OK;
    }

    printf("[OTA] CRC MISMATCH!\r\n");
    return MODEM_ERROR;
}

/**
 * @brief  Quick test function
 */
Modem_Status_t OTA_TestDownload(void)
{
    return OTA_DownloadFirmware("https://raw.githubusercontent.com/khuram11/ota_test/main/fw_with_crc.bin");
}



void OTA_TestChunkSizes(void)
{
    char response[256];
    uint8_t testBuf[2048];
    uint32_t bytesRead;

    printf("\r\n=== CHUNK SIZE TEST ===\r\n");

    /* Setup HTTP */
    Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 2000);
    HAL_Delay(300);
    Modem_SendCommand("AT+HTTPINIT\r\n", response, sizeof(response), 2000);
    Modem_SendCommand("AT+HTTPPARA=\"URL\",\"https://raw.githubusercontent.com/khuram11/ota_test/main/fw_with_crc.bin\"\r\n",
                      response, sizeof(response), 2000);

    int httpStatus;
    uint32_t totalSize;
    Modem_WaitForHTTPAction(0, 60000, &httpStatus, &totalSize);

    if (httpStatus != 200)
    {
        printf("HTTP failed!\r\n");
        return;
    }

    HAL_Delay(2000);

    /* Test different chunk sizes */
    uint32_t testSizes[] = {256, 332, 512, 768, 1024, 1460};

    for (int i = 0; i < 6; i++)
    {
        uint32_t chunkSize = testSizes[i];
        uint32_t start = HAL_GetTick();

        Modem_Status_t result = OTA_ReadBinaryChunk(0, chunkSize, testBuf, &bytesRead);

        uint32_t elapsed = HAL_GetTick() - start;

        printf("Chunk %lu: %s, got %lu bytes in %lu ms\r\n",
               chunkSize,
               (result == MODEM_OK) ? "OK" : "FAIL",
               bytesRead,
               elapsed);

        if (result == MODEM_OK && bytesRead > 0)
        {
            /* Verify first bytes are correct (magic number) */
            if (testBuf[0] == 0x31 && testBuf[1] == 0x41)
            {
                printf("  Data valid (magic OK)\r\n");
            }
            else
            {
                printf("  Data INVALID! First 4: %02X %02X %02X %02X\r\n",
                       testBuf[0], testBuf[1], testBuf[2], testBuf[3]);
            }
        }

        HAL_Delay(500);
    }

    Modem_SendCommand("AT+HTTPTERM\r\n", response, sizeof(response), 2000);
    printf("=== TEST COMPLETE ===\r\n");
}


