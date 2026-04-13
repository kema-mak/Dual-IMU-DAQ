#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

bool SD_Logger_Init(void);
bool SD_Logger_Write(uint8_t *data, uint16_t len);
void SD_Logger_Flush(void);
void SD_Logger_Close(void);

#endif
