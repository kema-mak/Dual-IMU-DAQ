# STM32F429 Dual-IMU + SD Logger: Relevant Resource Map

This file narrows the code-generation scope for the requested firmware so only the minimum, high-signal project files are loaded.

## 1) Existing CubeMX/CubeIDE files to use as integration anchors

### Core startup + peripheral ownership
- `Core/Src/main.c`
  - **Peripheral handles and init order**: SPI2/SPI4 DMA handles, TIM2 handle, `MX_*` calls in `main()`.
  - **Clock + peripheral configuration already matching your spec**: SPI4 mode 3 @ /8, SPI2 mode 0 @ /128, TIM2 PSC=179/ARR=0xFFFFFFFF, GPIO EXTI config.
  - **User extension points**:
    - `/* USER CODE BEGIN Includes */`
    - `/* USER CODE BEGIN PV */`
    - `/* USER CODE BEGIN PFP */`
    - `/* USER CODE BEGIN 0 */`
    - `/* USER CODE BEGIN 2 */`
    - `/* USER CODE BEGIN WHILE */` / `/* USER CODE BEGIN 3 */`
    - `/* USER CODE BEGIN 4 */` (HAL callbacks if desired)

- `Core/Inc/main.h`
  - Pin definitions for chip-select, IMU interrupts, LEDs.
  - Use these symbolic names in modules (avoid hardcoded pins/ports).

### IRQ shell ownership (do not duplicate handlers)
- `Core/Src/stm32f4xx_it.c`
  - CubeMX-owned IRQ handlers already present:
    - `EXTI15_10_IRQHandler`
    - `DMA2_Stream0_IRQHandler`, `DMA2_Stream1_IRQHandler` (SPI4 DMA)
    - `DMA1_Stream3_IRQHandler`, `DMA1_Stream4_IRQHandler` (SPI2 DMA)
    - `TIM2_IRQHandler`
  - User logic should be implemented in HAL callbacks, not duplicate IRQ functions.

### MSP and DMA linkage
- `Core/Src/stm32f4xx_hal_msp.c`
  - Confirms DMA stream/channel mapping and priorities for SPI2/SPI4.
  - Confirms TIM2 CH3 pin alternate function on PB10.

### FatFS + disk layer hook points
- `FATFS/App/fatfs.c`
  - FatFS link driver (`FATFS_LinkDriver`) and file system globals (`USERFatFS`, `USERFile`, `USERPath`).

- `FATFS/Target/user_diskio.c`
  - Stubbed user disk I/O entry points to implement:
    - `USER_initialize`
    - `USER_status`
    - `USER_read`
    - `USER_write`
    - `USER_ioctl`
  - This is where SPI2 + DMA-backed SD block transactions should live.

## 2) New files to add (module split requested)

Place application modules under Cube user code area (recommended paths):

- `Core/Inc/mpu9250.h`
- `Core/Src/mpu9250.c`
- `Core/Inc/sd_logger.h`
- `Core/Src/sd_logger.c`
- `Core/Inc/ring_buffer.h`
- `Core/Src/ring_buffer.c`
- `Core/Inc/timestamp.h`
- `Core/Src/timestamp.c`

## 3) Callback ownership plan (single source of truth)

Implement callbacks in **one** C file only (recommended: `Core/Src/main.c` USER CODE section):
- `HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)`
- `HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)`
- `HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)`

Reason: avoid duplicate symbol/linker conflicts while still keeping ISR code out of CubeMX IRQ shells.

## 4) Data-path resources to keep in working set

For coding the 32-byte record path, keep only these facts loaded:
- IMU burst payload is 14 bytes from `0x3B..0x48`.
- Record format: `4-byte timestamp + 14-byte IMU1 + 14-byte IMU2 = 32 bytes`.
- Ring buffer block target: 512 bytes (=16 records per block).
- No `f_write` in ISR/callback context.

## 5) Minimal implementation sequence (to reduce generation cost)

1. Add ring buffer + timestamp modules.
2. Add MPU9250 init + DMA read module.
3. Add `user_diskio.c` low-level SD SPI2/DMA implementation.
4. Add SD logger wrapper on FatFS.
5. Wire callbacks + main loop state machine in `main.c` USER CODE sections only.

## 6) Out-of-scope files (skip for now)

Do not load/modify unless required:
- HAL driver internals in `Drivers/STM32F4xx_HAL_Driver/*`
- FatFS core in `Middlewares/Third_Party/FatFs/src/*`
- startup assembly except for debugging IRQ vector issues.

## 7) Quick extraction commands for future passes

Use these targeted commands instead of opening the whole tree:

```bash
nl -ba Core/Src/main.c | sed -n '1,260p'
nl -ba Core/Src/main.c | sed -n '260,520p'
nl -ba Core/Inc/main.h | sed -n '1,220p'
nl -ba Core/Src/stm32f4xx_it.c | sed -n '200,360p'
nl -ba Core/Src/stm32f4xx_hal_msp.c | sed -n '90,260p'
nl -ba FATFS/App/fatfs.c | sed -n '1,140p'
nl -ba FATFS/Target/user_diskio.c | sed -n '1,260p'
```
