#include "mpu9250.h"

extern SPI_HandleTypeDef hspi4;

#define MPU_REG_SMPLRT_DIV       0x19U
#define MPU_REG_CONFIG           0x1AU
#define MPU_REG_GYRO_CONFIG      0x1BU
#define MPU_REG_ACCEL_CONFIG     0x1CU
#define MPU_REG_ACCEL_CONFIG2    0x1DU
#define MPU_REG_INT_PIN_CFG      0x37U
#define MPU_REG_INT_ENABLE       0x38U
#define MPU_REG_ACCEL_XOUT_H     0x3BU
#define MPU_REG_USER_CTRL        0x6AU
#define MPU_REG_PWR_MGMT_1       0x6BU
#define MPU_REG_PWR_MGMT_2       0x6CU
#define MPU_REG_WHO_AM_I         0x75U

#define MPU_WHO_AM_I_VALUE       0x71U

static uint8_t imu_dma_tx[15] __attribute__((aligned(4)));

static inline void mpu_cs_low(const mpu9250_dev_t *dev)
{
  HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);
}

static inline void mpu_cs_high(const mpu9250_dev_t *dev)
{
  HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);
}

static HAL_StatusTypeDef mpu_write_reg(const mpu9250_dev_t *dev, uint8_t reg, uint8_t val)
{
  uint8_t frame[2] = { reg & 0x7FU, val };
  mpu_cs_low(dev);
  if (HAL_SPI_Transmit(&hspi4, frame, 2U, HAL_MAX_DELAY) != HAL_OK)
  {
    mpu_cs_high(dev);
    return HAL_ERROR;
  }
  mpu_cs_high(dev);
  return HAL_OK;
}

static HAL_StatusTypeDef mpu_read_reg(const mpu9250_dev_t *dev, uint8_t reg, uint8_t *val)
{
  uint8_t tx[2] = { (uint8_t)(reg | 0x80U), 0xFFU };
  uint8_t rx[2] = {0};
  mpu_cs_low(dev);
  if (HAL_SPI_TransmitReceive(&hspi4, tx, rx, 2U, HAL_MAX_DELAY) != HAL_OK)
  {
    mpu_cs_high(dev);
    return HAL_ERROR;
  }
  mpu_cs_high(dev);
  *val = rx[1];
  return HAL_OK;
}

void MPU9250_SPI_SetSlow(void)
{
  hspi4.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;
  (void)HAL_SPI_Init(&hspi4);
}

void MPU9250_SPI_SetFast(void)
{
  hspi4.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  (void)HAL_SPI_Init(&hspi4);
}

HAL_StatusTypeDef MPU9250_Init(GPIO_TypeDef *cs_port, uint16_t cs_pin)
{
  uint8_t who = 0;
  mpu9250_dev_t dev = { .cs_port = cs_port, .cs_pin = cs_pin };

  MPU9250_SPI_SetSlow();

  if (mpu_write_reg(&dev, MPU_REG_PWR_MGMT_1, 0x80U) != HAL_OK) return HAL_ERROR;
  HAL_Delay(50);
  if (mpu_write_reg(&dev, MPU_REG_PWR_MGMT_1, 0x01U) != HAL_OK) return HAL_ERROR;
  if (mpu_write_reg(&dev, MPU_REG_PWR_MGMT_2, 0x00U) != HAL_OK) return HAL_ERROR;
  if (mpu_write_reg(&dev, MPU_REG_SMPLRT_DIV, 0x00U) != HAL_OK) return HAL_ERROR;
  if (mpu_write_reg(&dev, MPU_REG_CONFIG, 0x03U) != HAL_OK) return HAL_ERROR;
  if (mpu_write_reg(&dev, MPU_REG_GYRO_CONFIG, 0x00U) != HAL_OK) return HAL_ERROR;
  if (mpu_write_reg(&dev, MPU_REG_ACCEL_CONFIG, 0x00U) != HAL_OK) return HAL_ERROR;
  if (mpu_write_reg(&dev, MPU_REG_ACCEL_CONFIG2, 0x03U) != HAL_OK) return HAL_ERROR;

  if (mpu_write_reg(&dev, MPU_REG_INT_PIN_CFG, 0x10U) != HAL_OK) return HAL_ERROR;
  if (mpu_write_reg(&dev, MPU_REG_INT_ENABLE, 0x01U) != HAL_OK) return HAL_ERROR;

  if (mpu_read_reg(&dev, MPU_REG_USER_CTRL, &who) != HAL_OK) return HAL_ERROR;
  who |= 0x10U;
  if (mpu_write_reg(&dev, MPU_REG_USER_CTRL, who) != HAL_OK) return HAL_ERROR;

  if (mpu_read_reg(&dev, MPU_REG_WHO_AM_I, &who) != HAL_OK) return HAL_ERROR;
  if (who != MPU_WHO_AM_I_VALUE) return HAL_ERROR;

  MPU9250_SPI_SetFast();
  return HAL_OK;
}

HAL_StatusTypeDef MPU9250_ReadSensor_DMA(mpu9250_dev_t *dev, uint8_t rx_frame[15])
{
  imu_dma_tx[0] = MPU_REG_ACCEL_XOUT_H | 0x80U;
  for (uint32_t i = 1U; i < 15U; i++)
  {
    imu_dma_tx[i] = 0x00U;
  }

  mpu_cs_low(dev);
  return HAL_SPI_TransmitReceive_DMA(&hspi4, imu_dma_tx, rx_frame, 15U);
}

void MPU9250_ExtractBurst14(const uint8_t rx_frame[15], uint8_t out14[14])
{
  for (uint32_t i = 0U; i < 14U; i++)
  {
    out14[i] = rx_frame[i + 1U];
  }
}
