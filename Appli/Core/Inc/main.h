/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7rsxx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define MODEM_W_DIS1_Pin GPIO_PIN_3
#define MODEM_W_DIS1_GPIO_Port GPIOF
#define MODEM_CFG_1_Pin GPIO_PIN_2
#define MODEM_CFG_1_GPIO_Port GPIOF
#define MODEM_DPR_Pin GPIO_PIN_3
#define MODEM_DPR_GPIO_Port GPIOG
#define LED1_Pin GPIO_PIN_14
#define LED1_GPIO_Port GPIOE
#define MODEM_PWR_OFF_Pin GPIO_PIN_2
#define MODEM_PWR_OFF_GPIO_Port GPIOE
#define MODEM_CFG_3_Pin GPIO_PIN_4
#define MODEM_CFG_3_GPIO_Port GPIOF
#define MODEM_CFG_0_Pin GPIO_PIN_7
#define MODEM_CFG_0_GPIO_Port GPIOD
#define MODEM_RESET_Pin GPIO_PIN_0
#define MODEM_RESET_GPIO_Port GPIOM
#define MODEM_WAKE_ON_WAN_Pin GPIO_PIN_4
#define MODEM_WAKE_ON_WAN_GPIO_Port GPIOE
#define MODEM_CFG_2_Pin GPIO_PIN_14
#define MODEM_CFG_2_GPIO_Port GPIOG
#define Buttton_LED_Pin GPIO_PIN_14
#define Buttton_LED_GPIO_Port GPIOB
#define EN_5V0_PWR_Pin GPIO_PIN_12
#define EN_5V0_PWR_GPIO_Port GPIOF
#define MODEM_PWR_EN_Pin GPIO_PIN_8
#define MODEM_PWR_EN_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
