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

/* TIM8 的六路互补输出和驱动使能接线。AF3 是这些引脚的 TIM8 复用功能。 */
#define FOC_PWM_TIMER             TIM8
#define FOC_PWM_AF                GPIO_AF3_TIM8
#define FOC_PWM_HIGH_PORT         GPIOC
#define FOC_PWM_UH_PIN            GPIO_PIN_6
#define FOC_PWM_VH_PIN            GPIO_PIN_7
#define FOC_PWM_WH_PIN            GPIO_PIN_8
#define FOC_PWM_UL_PORT           GPIOA
#define FOC_PWM_UL_PIN            GPIO_PIN_5
#define FOC_PWM_VW_LOW_PORT       GPIOB
#define FOC_PWM_VL_PIN            GPIO_PIN_14
#define FOC_PWM_WL_PIN            GPIO_PIN_15
#define FOC_DRIVE_ENABLE_PORT     GPIOA
#define FOC_DRIVE_ENABLE_PIN      GPIO_PIN_3

/* DIV1 下 100 个死区计数约为 0.5 us。 */
#define FOC_PWM_DEAD_TIME_TICKS   100U

/* TIM8 为 200 MHz，上数到 9900 约需 49.5 us。首轮用于关断功率级时验证触发。 */
#define FOC_PWM_ADC_TRIGGER_TICKS 9900U

typedef struct
{
    uint16_t uTicks;
    uint16_t vTicks;
    uint16_t wTicks;
} FocPwmDuty;

/* 系统时钟初始化完成后配置 TIM8。计数器和功率输出保持关闭。 */
void FocPwm_Init(void);

/* 在三台 ADC 都已进入等待触发状态后，启动 TIM8 计数。 */
void FocPwm_StartCounter(void);

/* 提交一组三相比较值。范围为 0 到 ARR，越界时整组拒绝。 */
bool FocPwm_SetDutyTicks(FocPwmDuty duty);

/* 开放 TIM8 的六路输出。PA3 的使能时序由后续启动流程管理。 */
void FocPwm_EnableOutputs(void);

/* 拉低 PA3 并关闭六路输出，保留 TIM8 计数供后续 ADC 同步使用。 */
void FocPwm_DisableOutputs(void);

#endif