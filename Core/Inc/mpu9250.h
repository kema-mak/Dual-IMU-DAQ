#ifndef MPU9250_H
#define MPU9250_H

#include "main.h"

typedef struct
{
  GPIO_TypeDef *cs_port;
  uint16_t cs_pin;
} mpu9250_dev_t;

HAL_StatusTypeDef MPU9250_Init(GPIO_TypeDef *cs_port, uint16_t cs_pin);
HAL_StatusTypeDef MPU9250_ReadSensor_DMA(mpu9250_dev_t *dev, uint8_t rx_frame[15]);
void MPU9250_ExtractBurst14(const uint8_t rx_frame[15], uint8_t out14[14]);
void MPU9250_SPI_SetSlow(void);
void MPU9250_SPI_SetFast(void);

#endif
