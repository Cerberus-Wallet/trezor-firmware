#ifndef _STM32F429I_DISC1_H
#define _STM32F429I_DISC1_H

#define USE_TOUCH 1
#define USE_BLE 1
#define USE_SDRAM 1

#include "displays/ltdc.h"

#define I2C_INSTANCE I2C3
#define I2C_INSTANCE_CLK_EN __HAL_RCC_I2C3_CLK_ENABLE
#define I2C_INSTANCE_CLK_DIS __HAL_RCC_I2C3_CLK_DISABLE
#define I2C_INSTANCE_PIN_AF GPIO_AF4_I2C3
#define I2C_INSTANCE_SDA_PORT GPIOC
#define I2C_INSTANCE_SDA_PIN GPIO_PIN_9
#define I2C_INSTANCE_SDA_CLK_EN __HAL_RCC_GPIOC_CLK_ENABLE
#define I2C_INSTANCE_SCL_PORT GPIOA
#define I2C_INSTANCE_SCL_PIN GPIO_PIN_8
#define I2C_INSTANCE_SCL_CLK_EN __HAL_RCC_GPIOA_CLK_ENABLE
#define I2C_INSTANCE_FORCE_RESET __HAL_RCC_I2C3_FORCE_RESET
#define I2C_INSTANCE_RELEASE_RESET __HAL_RCC_I2C3_RELEASE_RESET

#endif  //_STM32F429I_DISC1_H
