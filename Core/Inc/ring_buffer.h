#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdbool.h>
#include <stdint.h>

#define RECORD_SIZE_BYTES 32U
#define BLOCK_SIZE_BYTES 512U
#define RECORDS_PER_BLOCK (BLOCK_SIZE_BYTES / RECORD_SIZE_BYTES)

void RingBuffer_Init(void);
bool RingBuffer_PushRecord_ISR(const uint8_t rec[RECORD_SIZE_BYTES]);
bool RingBuffer_GetFullBlock(uint8_t **block_ptr);
void RingBuffer_ReleaseBlock(void);
uint32_t RingBuffer_DroppedRecords(void);

#endif
