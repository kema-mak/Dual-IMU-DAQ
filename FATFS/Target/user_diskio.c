/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
 ******************************************************************************
  */
/* USER CODE END Header */

/* USER CODE BEGIN DECL */
#include <string.h>

#include "ff_gen_drv.h"
#include "main.h"

extern SPI_HandleTypeDef hspi2;

static volatile DSTATUS Stat = STA_NOINIT;

#define SD_SECTOR_SIZE            512U
#define SD_CMD_TIMEOUT_MS         300U
#define SD_DATA_TIMEOUT_MS        500U

#define CMD0   0U
#define CMD8   8U
#define CMD9   9U
#define CMD12  12U
#define CMD16  16U
#define CMD17  17U
#define CMD24  24U
#define CMD55  55U
#define CMD58  58U
#define ACMD41 41U

static uint8_t sd_dma_dummy_tx[SD_SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t sd_dma_dummy_rx[SD_SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t sd_is_sdhc;

/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize(BYTE pdrv);
DSTATUS USER_status(BYTE pdrv);
DRESULT USER_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
DRESULT USER_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif
#if _USE_IOCTL == 1
DRESULT USER_ioctl(BYTE pdrv, BYTE cmd, void *buff);
#endif

Diskio_drvTypeDef USER_Driver = {
  USER_initialize,
  USER_status,
  USER_read,
#if _USE_WRITE
  USER_write,
#endif
#if _USE_IOCTL == 1
  USER_ioctl,
#endif
};

static inline void sd_cs_low(void)
{
  HAL_GPIO_WritePin(CS_SD_GPIO_Port, CS_SD_Pin, GPIO_PIN_RESET);
}

static inline void sd_cs_high(void)
{
  HAL_GPIO_WritePin(CS_SD_GPIO_Port, CS_SD_Pin, GPIO_PIN_SET);
}

static HAL_StatusTypeDef spi2_set_prescaler(uint32_t prescaler)
{
  hspi2.Init.BaudRatePrescaler = prescaler;
  return HAL_SPI_Init(&hspi2);
}

static uint8_t sd_spi_txrx(uint8_t byte)
{
  uint8_t rx = 0xFFU;
  (void)HAL_SPI_TransmitReceive(&hspi2, &byte, &rx, 1U, HAL_MAX_DELAY);
  return rx;
}

static void sd_send_clocks(uint32_t nbytes)
{
  while (nbytes--)
  {
    (void)sd_spi_txrx(0xFFU);
  }
}

static uint8_t sd_wait_r1(uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();
  uint8_t r = 0xFFU;
  do
  {
    r = sd_spi_txrx(0xFFU);
    if ((r & 0x80U) == 0U)
    {
      return r;
    }
  } while ((HAL_GetTick() - start) < timeout_ms);

  return 0xFFU;
}

static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg, uint8_t crc)
{
  uint8_t frame[6];

  frame[0] = 0x40U | cmd;
  frame[1] = (uint8_t)(arg >> 24);
  frame[2] = (uint8_t)(arg >> 16);
  frame[3] = (uint8_t)(arg >> 8);
  frame[4] = (uint8_t)arg;
  frame[5] = crc;

  (void)HAL_SPI_Transmit(&hspi2, frame, 6U, HAL_MAX_DELAY);
  return sd_wait_r1(SD_CMD_TIMEOUT_MS);
}

static HAL_StatusTypeDef sd_dma_txrx(uint8_t *tx, uint8_t *rx, uint16_t len, uint32_t timeout)
{
  uint32_t start = HAL_GetTick();
  if (HAL_SPI_TransmitReceive_DMA(&hspi2, tx, rx, len) != HAL_OK)
  {
    return HAL_ERROR;
  }

  while (HAL_SPI_GetState(&hspi2) != HAL_SPI_STATE_READY)
  {
    if ((HAL_GetTick() - start) > timeout)
    {
      (void)HAL_SPI_Abort(&hspi2);
      return HAL_TIMEOUT;
    }
  }

  return HAL_OK;
}

static DRESULT sd_read_block(uint32_t lba, uint8_t *buf)
{
  uint8_t r1;
  uint32_t start;

  r1 = sd_send_cmd(CMD17, lba, 0x01U);
  if (r1 != 0x00U)
  {
    return RES_ERROR;
  }

  start = HAL_GetTick();
  while (sd_spi_txrx(0xFFU) != 0xFEU)
  {
    if ((HAL_GetTick() - start) > SD_DATA_TIMEOUT_MS)
    {
      return RES_ERROR;
    }
  }

  memset(sd_dma_dummy_tx, 0xFF, SD_SECTOR_SIZE);
  if (sd_dma_txrx(sd_dma_dummy_tx, buf, SD_SECTOR_SIZE, SD_DATA_TIMEOUT_MS) != HAL_OK)
  {
    return RES_ERROR;
  }

  (void)sd_spi_txrx(0xFFU);
  (void)sd_spi_txrx(0xFFU);
  return RES_OK;
}

static DRESULT sd_write_block(uint32_t lba, const uint8_t *buf)
{
  uint8_t r1;
  uint8_t token = 0xFEU;
  uint8_t resp;
  uint32_t start;

  r1 = sd_send_cmd(CMD24, lba, 0x01U);
  if (r1 != 0x00U)
  {
    return RES_ERROR;
  }

  (void)HAL_SPI_Transmit(&hspi2, &token, 1U, HAL_MAX_DELAY);
  if (sd_dma_txrx((uint8_t *)buf, sd_dma_dummy_rx, SD_SECTOR_SIZE, SD_DATA_TIMEOUT_MS) != HAL_OK)
  {
    return RES_ERROR;
  }

  token = 0xFFU;
  (void)HAL_SPI_Transmit(&hspi2, &token, 1U, HAL_MAX_DELAY);
  (void)HAL_SPI_Transmit(&hspi2, &token, 1U, HAL_MAX_DELAY);

  resp = sd_spi_txrx(0xFFU);
  if ((resp & 0x1FU) != 0x05U)
  {
    return RES_ERROR;
  }

  start = HAL_GetTick();
  while (sd_spi_txrx(0xFFU) == 0x00U)
  {
    if ((HAL_GetTick() - start) > SD_DATA_TIMEOUT_MS)
    {
      return RES_ERROR;
    }
  }

  return RES_OK;
}

DSTATUS USER_initialize(BYTE pdrv)
{
  uint8_t r1;
  uint8_t r7[4] = {0};
  uint8_t ocr[4] = {0};
  uint32_t start;

  if (pdrv != 0U)
  {
    return STA_NOINIT;
  }

  if (spi2_set_prescaler(SPI_BAUDRATEPRESCALER_128) != HAL_OK)
  {
    return STA_NOINIT;
  }

  sd_cs_high();
  sd_send_clocks(10U);

  sd_cs_low();
  r1 = sd_send_cmd(CMD0, 0U, 0x95U);
  sd_cs_high();
  (void)sd_spi_txrx(0xFFU);
  if (r1 != 0x01U)
  {
    return STA_NOINIT;
  }

  sd_cs_low();
  r1 = sd_send_cmd(CMD8, 0x000001AAU, 0x87U);
  for (uint8_t i = 0; i < 4U; i++)
  {
    r7[i] = sd_spi_txrx(0xFFU);
  }
  sd_cs_high();
  (void)sd_spi_txrx(0xFFU);

  if ((r1 & 0x04U) != 0U)
  {
    return STA_NOINIT;
  }

  start = HAL_GetTick();
  do
  {
    sd_cs_low();
    (void)sd_send_cmd(CMD55, 0U, 0x01U);
    r1 = sd_send_cmd(ACMD41, 0x40000000U, 0x01U);
    sd_cs_high();
    (void)sd_spi_txrx(0xFFU);

    if (r1 == 0x00U)
    {
      break;
    }
  } while ((HAL_GetTick() - start) < 1500U);

  if (r1 != 0x00U)
  {
    return STA_NOINIT;
  }

  sd_cs_low();
  r1 = sd_send_cmd(CMD58, 0U, 0x01U);
  for (uint8_t i = 0; i < 4U; i++)
  {
    ocr[i] = sd_spi_txrx(0xFFU);
  }
  sd_cs_high();
  (void)sd_spi_txrx(0xFFU);
  if (r1 != 0x00U)
  {
    return STA_NOINIT;
  }
  sd_is_sdhc = (ocr[0] & 0x40U) ? 1U : 0U;

  if (spi2_set_prescaler(SPI_BAUDRATEPRESCALER_4) != HAL_OK)
  {
    return STA_NOINIT;
  }

  Stat &= (DSTATUS)~STA_NOINIT;
  return Stat;
}

DSTATUS USER_status(BYTE pdrv)
{
  if (pdrv != 0U)
  {
    return STA_NOINIT;
  }
  return Stat;
}

DRESULT USER_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
  if ((pdrv != 0U) || (count == 0U) || (buff == NULL))
  {
    return RES_PARERR;
  }
  if ((Stat & STA_NOINIT) != 0U)
  {
    return RES_NOTRDY;
  }

  sd_cs_low();
  while (count--)
  {
    uint32_t addr = sd_is_sdhc ? (uint32_t)sector : ((uint32_t)sector * SD_SECTOR_SIZE);
    if (sd_read_block(addr, buff) != RES_OK)
    {
      sd_cs_high();
      (void)sd_spi_txrx(0xFFU);
      return RES_ERROR;
    }
    sector++;
    buff += SD_SECTOR_SIZE;
  }
  sd_cs_high();
  (void)sd_spi_txrx(0xFFU);

  return RES_OK;
}

#if _USE_WRITE == 1
DRESULT USER_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
  if ((pdrv != 0U) || (count == 0U) || (buff == NULL))
  {
    return RES_PARERR;
  }
  if ((Stat & STA_NOINIT) != 0U)
  {
    return RES_NOTRDY;
  }

  sd_cs_low();
  while (count--)
  {
    uint32_t addr = sd_is_sdhc ? (uint32_t)sector : ((uint32_t)sector * SD_SECTOR_SIZE);
    if (sd_write_block(addr, buff) != RES_OK)
    {
      sd_cs_high();
      (void)sd_spi_txrx(0xFFU);
      return RES_ERROR;
    }
    sector++;
    buff += SD_SECTOR_SIZE;
  }
  sd_cs_high();
  (void)sd_spi_txrx(0xFFU);

  return RES_OK;
}
#endif

#if _USE_IOCTL == 1
DRESULT USER_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  if (pdrv != 0U)
  {
    return RES_PARERR;
  }

  switch (cmd)
  {
    case CTRL_SYNC:
      return RES_OK;
    case GET_SECTOR_SIZE:
      *(WORD *)buff = SD_SECTOR_SIZE;
      return RES_OK;
    case GET_BLOCK_SIZE:
      *(DWORD *)buff = 1U;
      return RES_OK;
    case GET_SECTOR_COUNT:
      *(DWORD *)buff = 0U;
      return RES_OK;
    default:
      return RES_PARERR;
  }
}
#endif
