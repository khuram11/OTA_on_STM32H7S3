/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file            : usb_host.c
  * @brief           : USB Host for SIM8262E-M2 5G Modem
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "usb_host.h"
#include "usbh_core.h"
#include "usbh_cdc.h"

/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
extern volatile uint8_t ota_started;
/* Buffer sizes */
#define CDC_RX_BUFFER_SIZE  2048
#define CDC_TX_BUFFER_SIZE  512
#define RING_BUFFER_SIZE    2048

/* CDC Buffers */
static uint8_t CDC_RxBuffer[CDC_RX_BUFFER_SIZE];
static uint8_t CDC_TxBuffer[CDC_TX_BUFFER_SIZE];

/* Flags */
static volatile uint8_t CDC_TxComplete = 1;
static volatile uint8_t CDC_RxComplete = 0;
static volatile uint32_t CDC_RxLength = 0;

/* Ring Buffer for received data */
typedef struct {
    uint8_t buffer[RING_BUFFER_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
} RingBuffer_t;

static RingBuffer_t rxRingBuffer = {0};

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static uint32_t RingBuffer_Write(uint8_t *data, uint32_t len);
static uint32_t RingBuffer_Available(void);
static void RingBuffer_Flush(void);
/* USER CODE END PFP */

/* USB Host core handle declaration */
USBH_HandleTypeDef hUsbHostHS;
ApplicationTypeDef Appli_state = APPLICATION_IDLE;

/*
 * -- Insert your variables declaration here --
 */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*
 * user callback declaration
 */
static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id);

/*
 * -- Insert your external function declaration here --
 */
/* USER CODE BEGIN 1 */

/*============================================================================*/
/*                          RING BUFFER FUNCTIONS                             */
/*============================================================================*/

static uint32_t RingBuffer_Write(uint8_t *data, uint32_t len)
{
    uint32_t written = 0;

    for (uint32_t i = 0; i < len; i++)
    {
        uint32_t nextHead = (rxRingBuffer.head + 1) % RING_BUFFER_SIZE;

        if (nextHead != rxRingBuffer.tail)
        {
            rxRingBuffer.buffer[rxRingBuffer.head] = data[i];
            rxRingBuffer.head = nextHead;
            written++;
        }
        else
        {
            break;  /* Buffer full */
        }
    }

    return written;
}

uint32_t RingBuffer_Read(uint8_t *data, uint32_t maxLen)
{
    uint32_t readCount = 0;

    while ((rxRingBuffer.tail != rxRingBuffer.head) && (readCount < maxLen))
    {
        data[readCount++] = rxRingBuffer.buffer[rxRingBuffer.tail];
        rxRingBuffer.tail = (rxRingBuffer.tail + 1) % RING_BUFFER_SIZE;
    }

    return readCount;
}

static uint32_t RingBuffer_Available(void)
{
    if (rxRingBuffer.head >= rxRingBuffer.tail)
    {
        return rxRingBuffer.head - rxRingBuffer.tail;
    }
    else
    {
        return RING_BUFFER_SIZE - rxRingBuffer.tail + rxRingBuffer.head;
    }
}

static void RingBuffer_Flush(void)
{
    rxRingBuffer.head = 0;
    rxRingBuffer.tail = 0;
}

/*============================================================================*/
/*                          PUBLIC API FUNCTIONS                              */
/*============================================================================*/

/**
 * @brief  Check if USB CDC is ready for communication
 */
uint8_t USB_CDC_IsReady(void)
{
    return (Appli_state == APPLICATION_READY) ? 1 : 0;
}

/**
 * @brief  Get number of bytes available in receive buffer
 */
uint32_t USB_CDC_GetRxAvailable(void)
{
    return RingBuffer_Available();
}

/**
 * @brief  Flush receive buffer
 */
void USB_CDC_FlushRx(void)
{
    RingBuffer_Flush();
    CDC_RxComplete = 0;
    CDC_RxLength = 0;
}

/**
 * @brief  Start a single receive operation (non-blocking)
 *         Call this periodically to poll for incoming data
 */
void USB_CDC_StartReceive(void)
{
    if (Appli_state != APPLICATION_READY)
        return;

    /* Only start if not already waiting for data */
    if (CDC_RxComplete == 0)
    {
        USBH_CDC_Receive(&hUsbHostHS, CDC_RxBuffer, CDC_RX_BUFFER_SIZE);
    }
}

/**
 * @brief  Process any received data and store in ring buffer
 *         Call this after USB_CDC_StartReceive() or in main loop
 */
void USB_CDC_ProcessReceive(void)
{
    if (CDC_RxComplete)
    {
        if (CDC_RxLength > 0)
        {
            RingBuffer_Write(CDC_RxBuffer, CDC_RxLength);
        }

        CDC_RxComplete = 0;
        CDC_RxLength = 0;
    }
}

/**
 * @brief  Transmit data via USB CDC (blocking with timeout)
 */
HAL_StatusTypeDef USB_CDC_Transmit(uint8_t *data, uint32_t length, uint32_t timeout)
{
    if (Appli_state != APPLICATION_READY)
    {
        return HAL_ERROR;
    }

    if (length > CDC_TX_BUFFER_SIZE)
    {
        return HAL_ERROR;
    }

    /* Copy data to TX buffer */
    memcpy(CDC_TxBuffer, data, length);
    CDC_TxComplete = 0;

    /* Start transmit */
    USBH_CDC_Transmit(&hUsbHostHS, CDC_TxBuffer, length);

    /* Wait for completion */
    uint32_t startTick = HAL_GetTick();
    while (!CDC_TxComplete)
    {
        MX_USB_HOST_Process();

        if ((HAL_GetTick() - startTick) > timeout)
        {
            return HAL_TIMEOUT;
        }

        HAL_Delay(1);
    }

    return HAL_OK;
}

/**
 * @brief  Read data from receive ring buffer
 */
uint32_t USB_CDC_Read(uint8_t *data, uint32_t maxLen)
{
    return RingBuffer_Read(data, maxLen);
}

/**
 * @brief  Process USB host (call from main loop)
 */
void USB_CDC_Process(void)
{
    MX_USB_HOST_Process();
    USB_CDC_ProcessReceive();
}

/* USER CODE END 1 */

/**
  * Init USB host library, add supported class and start the library
  * @retval None
  */
void MX_USB_HOST_Init(void)
{
  /* USER CODE BEGIN USB_HOST_Init_PreTreatment */

  /* USER CODE END USB_HOST_Init_PreTreatment */

  /* Init host Library, add supported class and start the library. */
  if (USBH_Init(&hUsbHostHS, USBH_UserProcess, HOST_HS) != USBH_OK)
  {
    Error_Handler();
  }
  if (USBH_RegisterClass(&hUsbHostHS, USBH_CDC_CLASS) != USBH_OK)
  {
    Error_Handler();
  }
  if (USBH_Start(&hUsbHostHS) != USBH_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_HOST_Init_PostTreatment */

  /* USER CODE END USB_HOST_Init_PostTreatment */
}

/*
 * Background task
 */
void MX_USB_HOST_Process(void)
{
  /* USB Host Background task */
  USBH_Process(&hUsbHostHS);
}
/*
 * user callback definition
 */
static void USBH_UserProcess  (USBH_HandleTypeDef *phost, uint8_t id)
{
  /* USER CODE BEGIN CALL_BACK_1 */
    switch(id)
    {
        case HOST_USER_DISCONNECTION:
            Appli_state = APPLICATION_DISCONNECT;
            RingBuffer_Flush();
            CDC_TxComplete = 1;
            CDC_RxComplete = 0;
            CDC_RxLength = 0;
            printf("[USB] Disconnected\r\n");
            break;

        case HOST_USER_CLASS_ACTIVE:
            Appli_state = APPLICATION_READY;
            CDC_TxComplete = 1;
            CDC_RxComplete = 0;
            CDC_RxLength = 0;
            RingBuffer_Flush();
            printf("[USB] CDC Ready!\r\n");
            /* NOTE: Do NOT start receive here - causes interrupt flooding! */
            break;

        case HOST_USER_CONNECTION:
            Appli_state = APPLICATION_START;
            printf("[USB] Connected\r\n");
            break;

        case HOST_USER_CLASS_SELECTED:
            /* Class selected */
            break;

        default:
            break;
    }
  /* USER CODE END CALL_BACK_1 */
}

/* USER CODE BEGIN 2 */

/**
 * @brief  CDC Receive callback - called from USB interrupt
 *
 */

void USBH_CDC_ReceiveCallback(USBH_HandleTypeDef *phost)
{

    CDC_RxLength = USBH_CDC_GetLastReceivedDataSize(phost);
//    printf("rec len %ld\n",CDC_RxLength);
    CDC_RxComplete = 1;
    if(ota_started) USBH_CDC_Receive(phost, CDC_RxBuffer, CDC_RX_BUFFER_SIZE);


}

/**
 * @brief  CDC Transmit callback - called from USB interrupt
 */
void USBH_CDC_TransmitCallback(USBH_HandleTypeDef *phost)
{
    CDC_TxComplete = 1;
}

/* USER CODE END 2 */

/**
  * @}
  */

/**
  * @}
  */

