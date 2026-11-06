# STM32H743VIT6 串口基础工程

手动搭建的 Keil MDK 工程，使用 VS Code 编辑，**不使用 CubeMX，没有 `.ioc` 文件**。
目前配置 USART1、系统时钟、SysTick 和板载 LED1；不初始化电机、霍尔或其他外设引脚。

## 工程配置

| 项目 | 配置 |
| --- | --- |
| MCU | STM32H743VIT6，LQFP100，2 MB Flash |
| 编译器 | Keil Arm Compiler 6，本机验证版本 6.16 |
| 设备包 | Keil.STM32H7xx_DFP 2.8.0 |
| 官方库 | STM32Cube_FW_H7_V1.13.0 中的 CMSIS 和 HAL，已复制到工程内 |
| 系统时钟 | 外部 25 MHz 晶振，HSE 晶体模式，PLL 后 400 MHz |
| PLL1 | M=5，N=160，P=2，Q=4，R=2，输入范围 4~8 MHz，宽 VCO |
| 电源及 Flash | 内部 LDO，VOS1，Flash 4 等待周期 |
| 总线 | HCLK=200 MHz，APB1/2/3/4=100 MHz |
| 串口 | USART1，PA9=TX，PA10=RX，AF7，115200，8N1，无流控 |
| 接收 | 中断接收，512 字节环形缓冲区，主循环回显 |
| 运行指示灯 | LED1，PE9，低电平点亮，每 500 ms 翻转一次，完整周期约 1 秒 |
| 内存 | 程序从 0x08000000 启动，数据及 4 KB 栈位于 DTCM |

板上原理图的 X2 是 25 MHz 晶振。USART1 时钟选 APB2 的 100 MHz。
工程使用 Keil MicroLIB，`printf()` 已通过 `fputc()` 输出到 USART1，不依赖半主机输出。
`printf()` 和 `DebugUART_Write()` 供主循环使用，不要在中断中调用。

## 在 Keil 中编译和下载

1. 打开 `MDK-ARM/dianji1.uvprojx`，选择目标 `STM32H743VIT6_UART`。
2. 点击 Build（F7），输出位于 `MDK-ARM/Objects/`。
3. 工程默认使用 ST-Link。按你的下载器，在 Options for Target → Debug 中选择 ST-Link、J-Link 或 CMSIS-DAP，并使用 SWD。
4. 下载器连接板上 SWDIO=PA13、SWCLK=PA14、GND，以及下载器要求的参考电压；板子正常供电。
5. 使用 Flash → Download 下载，退出调试或运行程序；必要时按板上复位键。

生成的文件：

- `MDK-ARM/Objects/dianji1.axf`：带调试信息的可执行文件。
- `MDK-ARM/Objects/dianji1.hex`：烧录固件。
- `MDK-ARM/Objects/build.log`：命令行编译日志。

## 在 VS Code 中开发

用 VS Code 打开本目录，推荐安装 Microsoft C/C++ 扩展。`.vscode/` 中已配置头文件、芯片宏和 Keil 编译任务。

- `Ctrl+Shift+B`：调用本机 Keil 编译。
- 运行任务 `Keil: Rebuild`：完整重新编译。
- 也可编辑后切回 Keil，按 F7 编译。

命令行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1 -Rebuild
```

脚本默认查找 `D:\work\keil_v5\UV4\UV4.exe`，也支持常见的 `C:\Keil_v5`、`D:\Keil_v5`。
其他安装位置可通过脚本参数 `-UV4Path` 或环境变量 `KEIL_UV4_PATH` 指定。
如果更换电脑或编译器位置，同时更新 `.vscode/c_cpp_properties.json` 中的 `compilerPath`。

## 串口接线和验证

使用 **3.3 V 电平 USB 转 TTL** 模块：

| STM32 引脚 | 板上接线位置 | USB 转 TTL 模块 |
| --- | --- | --- |
| PA9 / USART1_TX | U9 第 1 脚；或左侧 32P 排针第 24 脚 | RXD |
| PA10 / USART1_RX | U9 第 3 脚；或左侧 32P 排针第 23 脚 | TXD |
| GND | U9 第 5 或第 6 脚 | GND |

“左侧排针”指 STM32 原理图第 3 页的 IO 引出区域；实物接线以信号丝印和 1 脚标记为准。
本固件没有 USB 虚拟串口功能，板上 USB 接口不代替上述 PA9/PA10 接线。

1. 串口工具选择转换器的 COM 端口，配置 115200、8 数据位、无校验、1 停止位、无流控。
2. 关闭串口工具的本地回显，以便确认收到的是单片机返回的数据。
3. 打开串口后按板上复位键，应看到：

```text
STM32H743VIT6 UART ready
SYSCLK=400000000 Hz, HCLK=200000000 Hz, PCLK2=100000000 Hz
USART1: PA9=TX, PA10=RX, 115200 8N1
Type text to echo. Motor GPIOs are not initialized.
```

运行时 LED1 亮半秒、灭半秒，使用 `HAL_GetTick()` 计时，不用 `HAL_Delay()` 阻塞主循环。
串口每秒输出一行 `[alive]`。发送 `hello` 应收到 `hello` 回显。
`rx_errors` 统计串口硬件错误，`rx_dropped` 统计接收缓冲区溢出；日常手动调试应保持为 0。
这是基础调试串口，周期状态输出会增加发送量，不作为无限持续满速数据流的吞吐测试。

若没有输出，先核对供电、共地、TX/RX 交叉接线及波特率；再用 Keil 检查是否停在 `Error_Handler()`。
晶振启动或 PLL 配置失败也会停在这里。

## 代码位置

- `Core/Src/main.c`：400 MHz 时钟、LED1 闪烁、启动信息、每秒状态输出和回显主循环。
- `Core/Src/debug_uart.c`：USART1 初始化、接收环形缓冲区及错误恢复。
- `Core/Src/stm32h7xx_hal_msp.c`：PA9/PA10 复用、USART1 时钟和中断。
- `Core/Src/stm32h7xx_it.c`：SysTick 和 USART1 中断入口。
- `Core/Src/retarget.c`：`printf()` 重定向。
- `Core/Inc/stm32h7xx_hal_conf.h`：HAL 模块和晶振频率配置。
- `Startup/startup_stm32h743xx.s`：官方启动文件，栈大小调整为 4 KB。
- `MDK-ARM/STM32H743VIT6.sct`：Flash / DTCM 内存布局。

## 验证状态

已使用本机 Keil MDK 5.36 / Arm Compiler 6.16 完整编译，0 错误、0 警告，并生成 AXF 和 HEX。
固件未自动下载到开发板；LED 闪烁、串口接线及实际收发仍需上板验证。

第三方源码及许可见 `THIRD_PARTY.md`。
