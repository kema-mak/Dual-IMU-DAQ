#include "ring_buffer.h"

static uint8_t block_a[BLOCK_SIZE_BYTES] __attribute__((aligned(4)));
static uint8_t block_b[BLOCK_SIZE_BYTES] __attribute__((aligned(4)));

static volatile uint8_t *volatile write_block;
static volatile uint8_t *volatile ready_block;
static volatile uint16_t write_offset;
static volatile uint8_t ready_flag;
static volatile uint32_t dropped_records;

void RingBuffer_Init(void)
{
  write_block = block_a;
  ready_block = block_b;
  write_offset = 0U;
  ready_flag = 0U;
  dropped_records = 0U;
}

bool RingBuffer_PushRecord_ISR(const uint8_t rec[RECORD_SIZE_BYTES])
{
  uint16_t i;
  uint8_t *dst = (uint8_t *)write_block + write_offset;

  if (ready_flag != 0U)
  {
    dropped_records++;
    return false;
  }

  for (i = 0; i < RECORD_SIZE_BYTES; i++)
  {
    dst[i] = rec[i];
  }

  write_offset += RECORD_SIZE_BYTES;
  if (write_offset >= BLOCK_SIZE_BYTES)
  {
    volatile uint8_t *tmp = ready_block;
    ready_block = write_block;
    write_block = tmp;
    write_offset = 0U;
    ready_flag = 1U;
  }

  return true;
}

bool RingBuffer_GetFullBlock(uint8_t **block_ptr)
{
  if ((ready_flag == 0U) || (block_ptr == 0))
  {
    return false;
  }

  *block_ptr = (uint8_t *)ready_block;
  return true;
}

void RingBuffer_ReleaseBlock(void)
{
  ready_flag = 0U;
}

uint32_t RingBuffer_DroppedRecords(void)
{
  return dropped_records;
}
