#ifndef FOC_PWM_H
#define FOC_PWM_H

#include <stdbool.h>
#include <stdint.h>

/* 当前时钟树下 TIM8 为 200 MHz，中心对齐的完整周期包含上下两段计数。 */
#define FOC_PWM_INITIAL_DUTY_TICKS 0U
#define FOC_PWM_TIMER_CLOCK_HZ  200000000U
#define FOC_PWM_FREQ_HZ         10000U
#define FOC_PWM_PRESCALER       0U
#define FOC_PWM_PERIOD_TICKS    \
    (FOC_PWM_TIMER_CLOCK_HZ / (2U * (FOC_PWM_PRESCALER + 1U) * FOC_PWM_FREQ_HZ))

typedef struct
{
    uint16_t uTicks;
    uint16_t vTicks;
    uint16_t wTicks;
} FocPwmDuty;

/* 系统时钟初始化完成后调用一次。启动计数器，功率级保持关断。 */
void FocPwm_Init(void);

/* 提交一组三相比较值。范围为 0 到 ARR，越界时整组拒绝。 */
bool FocPwm_SetDutyTicks(FocPwmDuty duty);

/* 开放 TIM8 的六路输出。PA3 的使能时序由后续启动流程管理。 */
void FocPwm_EnableOutputs(void);

/* 拉低 PA3 并关闭六路输出，保留 TIM8 计数供后续 ADC 同步使用。 */
void FocPwm_DisableOutputs(void);

#endif