#include "sd_logger.h"

#include <stdio.h>
#include <string.h>

#include "fatfs.h"
#include "main.h"

static FIL s_log_file;
static bool s_mounted;
static bool s_opened;
static uint32_t s_blocks_since_sync;

#define SYNC_EVERY_BLOCKS 32U

static void build_filename(char out[32])
{
  uint32_t t = HAL_GetTick();
  (void)snprintf(out, 32, "LOG_%08lu.BIN", (unsigned long)t);
}

bool SD_Logger_Init(void)
{
  FRESULT fr;
  char fname[32] = {0};

  fr = f_mount(&USERFatFS, (TCHAR const *)USERPath, 1U);
  if (fr != FR_OK)
  {
    return false;
  }

  s_mounted = true;
  build_filename(fname);

  fr = f_open(&s_log_file, fname, FA_CREATE_ALWAYS | FA_WRITE);
  if (fr != FR_OK)
  {
    return false;
  }

  s_opened = true;
  s_blocks_since_sync = 0U;
  return true;
}

bool SD_Logger_Write(uint8_t *data, uint16_t len)
{
  FRESULT fr;
  UINT bw = 0U;

  if ((!s_opened) || (data == NULL) || ((len % 512U) != 0U))
  {
    return false;
  }

  fr = f_write(&s_log_file, data, len, &bw);
  if ((fr != FR_OK) || (bw != len))
  {
    return false;
  }

  s_blocks_since_sync += (uint32_t)(len / 512U);
  if (s_blocks_since_sync >= SYNC_EVERY_BLOCKS)
  {
    SD_Logger_Flush();
    s_blocks_since_sync = 0U;
  }

  return true;
}

void SD_Logger_Flush(void)
{
  if (s_opened)
  {
    (void)f_sync(&s_log_file);
  }
}

void SD_Logger_Close(void)
{
  if (s_opened)
  {
    (void)f_sync(&s_log_file);
    (void)f_close(&s_log_file);
    s_opened = false;
  }
  if (s_mounted)
  {
    (void)f_mount(NULL, (TCHAR const *)USERPath, 1U);
    s_mounted = false;
  }
}
