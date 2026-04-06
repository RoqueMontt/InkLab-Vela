/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "stm32g0xx_hal.h"

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

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_RGB2_Pin GPIO_PIN_13
#define LED_RGB2_GPIO_Port GPIOC
#define LED_RGB1_Pin GPIO_PIN_14
#define LED_RGB1_GPIO_Port GPIOC
#define LED_RGB0_Pin GPIO_PIN_15
#define LED_RGB0_GPIO_Port GPIOC
#define SD_SCK_Pin GPIO_PIN_0
#define SD_SCK_GPIO_Port GPIOA
#define SD_CS_Pin GPIO_PIN_1
#define SD_CS_GPIO_Port GPIOA
#define SD_DET_Pin GPIO_PIN_2
#define SD_DET_GPIO_Port GPIOA
#define SD_MISO_Pin GPIO_PIN_3
#define SD_MISO_GPIO_Port GPIOA
#define SD_MOSI_Pin GPIO_PIN_4
#define SD_MOSI_GPIO_Port GPIOA
#define PG_TPS65_Pin GPIO_PIN_12
#define PG_TPS65_GPIO_Port GPIOB
#define PWR_EN_TPS65_Pin GPIO_PIN_13
#define PWR_EN_TPS65_GPIO_Port GPIOB
#define WKP_TPS65_Pin GPIO_PIN_14
#define WKP_TPS65_GPIO_Port GPIOB
#define MCU_CLK_Pin GPIO_PIN_8
#define MCU_CLK_GPIO_Port GPIOA
#define SW_A_Pin GPIO_PIN_7
#define SW_A_GPIO_Port GPIOC
#define FPGA_nDONE_Pin GPIO_PIN_15
#define FPGA_nDONE_GPIO_Port GPIOA
#define FPGA_RST_Pin GPIO_PIN_0
#define FPGA_RST_GPIO_Port GPIOD
#define SW_B_Pin GPIO_PIN_1
#define SW_B_GPIO_Port GPIOD
#define SW_CEN_Pin GPIO_PIN_2
#define SW_CEN_GPIO_Port GPIOD
#define FPGA_CS_Pin GPIO_PIN_3
#define FPGA_CS_GPIO_Port GPIOD
#define FPGA_SCK_Pin GPIO_PIN_3
#define FPGA_SCK_GPIO_Port GPIOB
#define FPGA_MISO_Pin GPIO_PIN_4
#define FPGA_MISO_GPIO_Port GPIOB
#define FPGA_MOSI_Pin GPIO_PIN_5
#define FPGA_MOSI_GPIO_Port GPIOB
#define I2C1_SCL_Pin GPIO_PIN_6
#define I2C1_SCL_GPIO_Port GPIOB
#define I2C1_SDA_Pin GPIO_PIN_7
#define I2C1_SDA_GPIO_Port GPIOB
#define SW_C_Pin GPIO_PIN_8
#define SW_C_GPIO_Port GPIOB
#define SW_D_Pin GPIO_PIN_9
#define SW_D_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
