#ifndef FOC_ADC_H
#define FOC_ADC_H

#include <stdbool.h>
#include <stdint.h>

/* U 相放大器输出 AMP_IU：PA6 → ADC1_INP3。通道号由接线决定。 */
#define FOC_ADC_U_PORT               GPIOA
#define FOC_ADC_U_PIN                GPIO_PIN_6
#define FOC_ADC_U_CHANNEL            ADC_CHANNEL_3

/* V 相放大器输出 AMP_IV：PB1 → ADC2_INP5。 */
#define FOC_ADC_V_PORT               GPIOB
#define FOC_ADC_V_PIN                GPIO_PIN_1
#define FOC_ADC_V_CHANNEL            ADC_CHANNEL_5

/* W 相放大器输出 AMP_IW：板上标 PC3，芯片 PC3_C 直连 ADC3_INP1。 */
#define FOC_ADC_W_CHANNEL            ADC_CHANNEL_1

/*
 * 采样时间是 ADC 内部电容跟踪输入电压的时间。当前 16.5 周期给电容充电，
 * 三相使用同一档以对齐采样。可选 1.5、2.5、8.5、32.5、64.5、387.5、
 * 810.5 周期。短档节省 PWM 窗口，要求信号源能更快驱动采样电容；
 * 长档给高阻抗输入更多稳定时间，但占用更长的窗口。
 * 12 位转换在采样后另需 6.5 周期。实际 ADC 时钟为 12.5 MHz 时，
 * 采样约 1.32 us，采样加转换约 1.84 us。Rev V 为 6.25 MHz，
 * 对应约 2.64 us 和 3.68 us。以上未计触发同步和中断延迟。
 */
#define FOC_ADC_CURRENT_SAMPLE_TIME  ADC_SAMPLETIME_16CYCLES_5
/* NVIC 抢占优先级为 5，数值越小越优先。两条 ADC 中断使用相同优先级。 */
#define FOC_ADC_IRQ_PRIORITY         5U

typedef struct
{
    uint16_t uRaw;       /* ADC1 的 U 相原始值。 */
    uint16_t vRaw;       /* ADC2 的 V 相原始值。 */
    uint16_t wRaw;       /* ADC3 的 W 相原始值。 */
    uint32_t sequence;  /* 每收齐一组三相数据加 1。 */
} FocAdcCurrentFrame;

/* 在 TIM8 开始产生采样触发前调用。 */
void FocAdc_Init(void);

/* 读取最近一帧。调用方可比较 sequence 判断是否为新帧。
 * frame 为空、首帧未就绪或采样故障时返回 false，不修改调用方的数据。
 */
bool FocAdc_ReadCurrents(FocAdcCurrentFrame *frame);

#endif