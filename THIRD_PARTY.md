# Third-party sources

The CMSIS and HAL files were copied from the locally installed official
STM32Cube_FW_H7_V1.13.0 package. This project does not depend on that package's
installation path after creation.

| Component | Local files | License |
| --- | --- | --- |
| Arm CMSIS Core | `Drivers/CMSIS/Include/` | `Drivers/CMSIS/LICENSE.txt` (Apache-2.0) |
| ST STM32H7 device support | `Drivers/CMSIS/Device/ST/STM32H7xx/`, `Core/Src/system_stm32h7xx.c`, `Startup/startup_stm32h743xx.s` | `Drivers/CMSIS/Device/ST/STM32H7xx/LICENSE.txt` |
| ST STM32H7 HAL | `Drivers/STM32H7xx_HAL_Driver/`, `Core/Inc/stm32h7xx_hal_conf.h` | `Drivers/STM32H7xx_HAL_Driver/LICENSE.txt` |

Original copyright and license notices have been retained. The startup file's
stack allocation was changed from 1 KB to 4 KB. The HAL configuration template
was adapted to enable only the modules required by the UART foundation.

The Keil project metadata was adapted from the official NUCLEO-H743ZI template,
with the device, compiler version, source paths, clock, memory layout, and debug
settings replaced for this STM32H743VIT6 board. No Nucleo BSP code is used.
