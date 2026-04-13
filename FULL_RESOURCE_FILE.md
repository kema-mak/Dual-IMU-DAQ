# Full Resource File — STM32F429 Dual-IMU DAQ Firmware

This document compiles the existing `RESOURCE_MAP.md` into an execution-ready resource package for implementing the complete firmware requested (MPU9250 dual readout + timestamp + ring buffer + SD/FatFS logging).

---

## A) Project Baseline and Constraints

### Board/peripheral baseline already present in generated project
- MCU family/project structure: STM32CubeIDE + HAL + FatFS.
- SPI4 configured as master, Mode 3 (CPOL=1/CPHA=1), prescaler `/8` (high-speed runtime path for MPU reads).
- SPI2 configured as master, Mode 0, prescaler `/128` (SD init-safe default).
- TIM2 configured 32-bit free-running (`PSC=179`, `ARR=0xFFFFFFFF`) with CH3 on PB10.
- EXTI line IRQ is shared (`EXTI15_10_IRQHandler`) for both IMU interrupts.
- DMA mappings match desired topology:
  - SPI4 RX: DMA2 Stream0 Ch4 (high)
  - SPI4 TX: DMA2 Stream1 Ch4 (medium)
  - SPI2 RX: DMA1 Stream3 Ch0
  - SPI2 TX: DMA1 Stream4 Ch0

### Hard constraints to preserve
1. **Do not add duplicate raw IRQ handlers** (CubeMX owns handlers in `stm32f4xx_it.c`).
2. Use HAL callbacks for app logic:
   - `HAL_GPIO_EXTI_Callback`
   - `HAL_SPI_TxRxCpltCallback`
   - `HAL_TIM_OC_DelayElapsedCallback`
3. No FatFS writes from ISR/callback context.
4. Keep DMA buffers aligned (`__attribute__((aligned(4)))`).
5. Keep implementation in Cube user sections or separate user modules.

---

## B) Source-of-Truth Anchor Files (Current Tree)

## 1) `Core/Src/main.c`
Use this as top-level integration owner.

### Relevant facts from current file
- Global handles exist and should be imported by modules via `extern` where necessary:
  - `hspi2`, `hspi4`, DMA handles, `htim2`.
- Init order already includes `MX_GPIO_Init`, `MX_DMA_Init`, `MX_SPI4_Init`, `MX_SPI2_Init`, `MX_TIM2_Init`, `MX_FATFS_Init`.
- GPIO setup already drives CS pins high by default and configures IMU EXTI pins rising-edge.
- TIM2 OC channel 3 is configured in timing mode and post-init on PB10 already exists.

### User-code insertion points intended for integration
- `USER CODE BEGIN Includes`
- `USER CODE BEGIN PV`
- `USER CODE BEGIN PFP`
- `USER CODE BEGIN 0`
- `USER CODE BEGIN 2`
- `USER CODE BEGIN WHILE` / `USER CODE BEGIN 3`
- `USER CODE BEGIN 4`

## 2) `Core/Inc/main.h`
Pin aliases to reuse in all modules:
- `CS_IMU1_Pin/Port`, `CS_IMU2_Pin/Port`
- `INT_IMU1_Pin/Port`, `INT_IMU2_Pin/Port`
- `CS_SD_Pin/Port`
- `LED_GREEN_Pin/Port`, `LED_RED_Pin/Port`

## 3) `Core/Src/stm32f4xx_it.c`
IRQ ownership stays here (generated shell).

### Existing handlers present
- `DMA1_Stream3_IRQHandler`, `DMA1_Stream4_IRQHandler`
- `DMA2_Stream0_IRQHandler`, `DMA2_Stream1_IRQHandler`
- `TIM2_IRQHandler`
- `SPI2_IRQHandler`, `SPI4_IRQHandler`
- `EXTI15_10_IRQHandler` (calls HAL for both IMU pins)

### Integration implication
Add application behavior only through HAL callbacks to avoid linker conflicts.

## 4) `Core/Src/stm32f4xx_hal_msp.c`
Confirms DMA stream/channel/pin AF links, already matching requested hardware.
No structural changes needed unless priorities are tuned.

## 5) `FATFS/App/fatfs.c`
Contains FatFS linkage and global file objects:
- `USERPath`, `USERFatFS`, `USERFile`
- `MX_FATFS_Init()` performs `FATFS_LinkDriver(&USER_Driver, USERPath)`

## 6) `FATFS/Target/user_diskio.c`
This is currently a stub and is the correct place to implement SPI2+DMA SD block I/O.

Functions to complete:
- `USER_initialize`
- `USER_status`
- `USER_read`
- `USER_write`
- `USER_ioctl`

---

## C) New Module File Set to Implement

Create these files:
- `Core/Inc/mpu9250.h`
- `Core/Src/mpu9250.c`
- `Core/Inc/timestamp.h`
- `Core/Src/timestamp.c`
- `Core/Inc/ring_buffer.h`
- `Core/Src/ring_buffer.c`
- `Core/Inc/sd_logger.h`
- `Core/Src/sd_logger.c`

---

## D) Interface Contract (Recommended)

## `mpu9250.h`
- `typedef struct { GPIO_TypeDef *cs_port; uint16_t cs_pin; } mpu9250_dev_t;`
- `HAL_StatusTypeDef MPU9250_Init(mpu9250_dev_t *dev);`
- `HAL_StatusTypeDef MPU9250_ReadSensor_DMA(mpu9250_dev_t *dev, uint8_t *rx14);`
- `void MPU9250_SPI_SetSlow(void);`  // /64 during config
- `void MPU9250_SPI_SetFast(void);`  // /8 during streaming

## `timestamp.h`
- `void Timestamp_Init(void);`
- `uint32_t Timestamp_Get(void);`

## `ring_buffer.h`
- `#define RECORD_SIZE_BYTES 32`
- `#define BLOCK_SIZE_BYTES 512`
- `#define RECORDS_PER_BLOCK 16`
- `void RingBuffer_Init(void);`
- `bool RingBuffer_PushRecord_ISR(const uint8_t rec[RECORD_SIZE_BYTES]);`
- `bool RingBuffer_GetFullBlock(uint8_t **block_ptr);`
- `void RingBuffer_ReleaseBlock(void);`

## `sd_logger.h`
- `bool SD_Logger_Init(void);`
- `bool SD_Logger_Write(uint8_t *data, uint16_t len);`
- `void SD_Logger_Flush(void);`
- `void SD_Logger_Close(void);`

---

## E) ISR/Callback Flow Contract

Implement in one C file (recommended `main.c` USER section):

1. `HAL_GPIO_EXTI_Callback()`
   - if `INT_IMU1_Pin`: capture timestamp (`Timestamp_Get`) and start SPI4 DMA read for IMU1.
   - optional ignore/debounce if SPI transaction already in progress.

2. `HAL_SPI_TxRxCpltCallback()` for `hspi4`
   - state `IMU1_RX_DONE`:
     - deassert CS1
     - start IMU2 DMA read
   - state `IMU2_RX_DONE`:
     - deassert CS2
     - build 32-byte record: `[ts(4)][imu1(14)][imu2(14)]`
     - `RingBuffer_PushRecord_ISR(record)`

3. `HAL_TIM_OC_DelayElapsedCallback()` for TIM2 CH3
   - `CCR3 += 1000` to maintain 1kHz continuous compare schedule.

---

## F) Data Layout Contract

### 32-byte record format
- Bytes `[0..3]`: timestamp `uint32_t` (little-endian host packing unless you intentionally choose BE for timestamp).
- Bytes `[4..17]`: IMU1 raw burst from MPU registers `0x3B..0x48` (14 bytes, preserve device byte order).
- Bytes `[18..31]`: IMU2 raw burst, same layout.

### Buffering model
- Two 512-byte ping-pong blocks.
- ISR path writes records only.
- Main loop consumes full block and calls SD logger write.

---

## G) `user_diskio.c` Implementation Notes (SD over SPI2)

Minimum capabilities required by FatFS disk layer:
- SPI transaction wrappers with CS control on `CS_SD`.
- SD command framing (CMD0, CMD8, ACMD41, CMD58 for init; CMD17/18 read; CMD24/25 write).
- Sector-level transfers (512-byte block).
- Return accurate `DSTATUS`/`DRESULT`.
- `GET_SECTOR_COUNT`, `GET_SECTOR_SIZE`, `CTRL_SYNC` in `USER_ioctl`.

Clock policy:
- Keep `/128` during card init.
- Switch to `/4` after card is ready.

DMA policy:
- Use SPI2 DMA for multi-byte payload transfers.
- Poll/wait with timeout in diskio layer (thread context), not ISR.

---

## H) Main Loop Responsibilities

In `while(1)`:
1. Check ring buffer full-block availability.
2. Write one 512-byte block via `SD_Logger_Write`.
3. Periodic flush via `SD_Logger_Flush` (e.g., every N blocks).
4. Optional heartbeat LED toggle.

Startup sequence in `main()` after Cube inits:
1. Initialize module globals/ring buffer.
2. Simultaneous IMU reset sequence (both CS controlled; same timing window).
3. Configure both IMUs (`MPU9250_Init`).
4. Initialize SD logger (`SD_Logger_Init`).
5. Start timestamp/FSYNC generator (`Timestamp_Init`, TIM2 OC IT start).

---

## I) Build/Integration Checklist

- Add new headers to include path automatically via `Core/Inc`.
- Ensure each new C file is under `Core/Src` so CubeIDE builds it.
- Confirm no duplicate definitions for:
  - IRQ handlers in `stm32f4xx_it.c`
  - global `SPI_HandleTypeDef` instances
- Verify DMA complete callback is triggered for SPI4 transfers.
- Verify EXTI priorities remain higher than SD write path execution.

---

## J) Quick Retrieval Commands (for incremental coding passes)

```bash
nl -ba Core/Src/main.c | sed -n '1,260p'
nl -ba Core/Src/main.c | sed -n '260,520p'
nl -ba Core/Inc/main.h | sed -n '1,220p'
nl -ba Core/Src/stm32f4xx_it.c | sed -n '200,360p'
nl -ba Core/Src/stm32f4xx_hal_msp.c | sed -n '90,260p'
nl -ba FATFS/App/fatfs.c | sed -n '1,160p'
nl -ba FATFS/Target/user_diskio.c | sed -n '1,280p'
```

