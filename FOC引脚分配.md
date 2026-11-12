# PMSM FOC 引脚分配

适用硬件为 **STM32H743VIT6 IOT 开发板 + ATK-PD6010B 驱动板**。编码器按 **ABZ 增量式**分配。表中的「左」和「右」指 STM32 原理图第 3 页 IO 引出区域的两组 32P 排针，接线时按实物的 1 脚标记定位。

## 互补 PWM 和使能

| 信号 | STM32 引脚 | STM32 排针 | STM32 功能 | 驱动板端子 |
| --- | --- | --- | --- | --- |
| U 上桥 PWM_UH | PC6 | 左 17 | TIM8_CH1 · AF3 | CN3-17 |
| U 下桥 PWM_UL | PA5 | 右 26 | TIM8_CH1N · AF3 | CN3-18 |
| V 上桥 PWM_VH | PC7 | 左 20 | TIM8_CH2 · AF3 | CN3-19 |
| V 下桥 PWM_VL | PB14 | 左 15 | TIM8_CH2N · AF3 | CN3-20 |
| W 上桥 PWM_WH | PC8 | 左 19 | TIM8_CH3 · AF3 | CN3-21 |
| W 下桥 PWM_WL | PB15 | 左 18 | TIM8_CH3N · AF3 | CN3-22 |
| 驱动使能 CTRL_SD | PA3 | 右 28 | GPIO 输出，低电平关断，高电平使能 | CN3-24 |
| 信号地 GND | GND | 左 1 | STM32 与驱动板共地 | CN3-7 或 CN3-16 |

TIM8 计划使用中心对齐互补 PWM，由定时器插入死区。使能 PA3 前先核对每对 PWM 的极性和死区。

| 驱动板端子 | 电机及母线 |
| --- | --- |
| CN1-1 / CN1-2 / CN1-3 | 电机动力线 U / V / W |
| CN1-4 / CN1-5 | 功率电源 GND / VIN |

## ABZ 编码器

| 信号 | 编码器 → 驱动板 | 驱动板 → STM32 | STM32 排针 | STM32 功能 |
| --- | --- | --- | --- | --- |
| A | CN2-8 ENCA | CN3-1 → PA15 | 左 28 | TIM2_CH1 · AF1 |
| B | CN2-7 ENCB | CN3-3 → PB3 | 右 10 | TIM2_CH2 · AF1 |
| Z | CN2-6 ENCZ | CN3-5 → PD7 | 右 3 | GPIO 输入 · EXTI7 |
| 编码器地 | CN2-4 GND | 通过 CN3 共地 | — | — |
| 编码器供电 | CN2-5 VENC | JP2 选择 5 V 或 11 V | — | — |

- **JP1 短接 1–2**，将编码器 Z 接到 CN3-5。
- **JP2 短接 1–2 为 5 V，短接 2–3 为 11 V**。按编码器额定供电选择。供电电压和 A/B/Z 的输出电平分别核对。
- CN2 的 ABZ 是单端输入。差分编码器先通过匹配的差分接收器，再接入单端 A/B/Z。
- A/B 由 32 位 TIM2 硬件计数。Z 用于机械索引，FOC 的电角度零点偏移需要标定。
- PA15、PB3 用作编码器时，Keil 选 SWD 调试，使用 PA13（SWDIO）、PA14（SWCLK）。

## 电流、母线和温度

| 信号 | 驱动板端子 | STM32 引脚 | STM32 排针 | ADC 功能 |
| --- | --- | --- | --- | --- |
| U 相电流 AMP_IU | CN3-10 | PA6 | 右 23 | ADC1_INP3 |
| V 相电流 AMP_IV | CN3-12 | PB1 | 右 21 | ADC2_INP5 |
| W 相电流 AMP_IW | CN3-14 | PC3_C，板上标 PC3 | 右 29 | ADC3_INP1 |
| 母线电压 VBUS | CN3-8 | PC2_C，板上标 PC2 | 右 30 | ADC3_INP0 |
| 驱动板温度 VTEMP | CN3-15 | PA4 | 右 25 | ADC1_INP18 |

PC2_C、PC3_C 使用 LQFP100 封装上直连 ADC3 的模拟通道。三路相电流计划由同一个 TIM8_TRGO2 触发，在低侧分流电流有效的 PWM 窗口采样。母线和板温以较低频率采集。

## 调试串口和指示灯

| 信号 | STM32 引脚 | STM32 排针 | 接线或功能 |
| --- | --- | --- | --- |
| USART1_TX | PA9 | 左 24 | AF7，接 USB 转 TTL 的 RXD |
| USART1_RX | PA10 | 左 23 | AF7，接 USB 转 TTL 的 TXD |
| 串口地 | GND | 左 1 | 接 USB 转 TTL 的 GND |
| 板载 LED1 | PE9 | 右 22 | 低电平点亮 |

调试串口使用 **3.3 V TTL、115200、8N1**。

## 从 dianji1 接线切换

1. 如果 VBUS 已接在 PA5，先把 CN3-8 的 VBUS 移到右 30（PC2_C）。
2. 把 CN3-18 的 U 下桥从左 21（PA8）移到右 26（PA5）。
3. 如果 W 相电流已接在 PB0，把 CN3-14 的 AMP_IW 从右 24 移到右 29（PC3_C）。
4. 按上表接入 CN3-1、CN3-3、CN3-5 的 ABZ。原来的霍尔通路使用 CN3-2、CN3-4、CN3-6，切换时按针号区分。

## 核对资料

- 开发板排针见第 3 页 IO 引出区域。[STM32H743VIT6 IOT 原理图](<SCH_STM32H743VIT6 IOT.pdf>)
- 驱动板功率级见第 1 页，CN1、CN2、CN3、JP1、JP2 见第 2 页。[ATK-PD6010B 原理图](ATK-PD6010B_V1.0.pdf)
- ADC 与引脚复用见 LQFP100 引脚定义和复用表。[STM32H743 数据手册 DS12110](https://www.st.com/resource/en/datasheet/stm32h743bg.pdf)
