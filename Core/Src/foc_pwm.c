#include "main.h"
#include "foc_pwm.h"

#define FOC_PWM_TIMER            TIM8
#define FOC_PWM_AF               GPIO_AF3_TIM8
#define FOC_PWM_DEAD_TIME_TICKS  100U

#define FOC_PWM_HIGH_PORT        GPIOC
#define FOC_PWM_UH_PIN           GPIO_PIN_6
#define FOC_PWM_VH_PIN           GPIO_PIN_7
#define FOC_PWM_WH_PIN           GPIO_PIN_8

#define FOC_PWM_UL_PORT          GPIOA
#define FOC_PWM_UL_PIN           GPIO_PIN_5

#define FOC_PWM_VW_LOW_PORT      GPIOB
#define FOC_PWM_VL_PIN           GPIO_PIN_14
#define FOC_PWM_WL_PIN           GPIO_PIN_15

#define FOC_DRIVE_ENABLE_PORT    GPIOA
#define FOC_DRIVE_ENABLE_PIN     GPIO_PIN_3

static TIM_HandleTypeDef pwmTimer;

void FocPwm_DisableOutputs(void)
{
    /* 先让驱动板关断，再关闭 TIM8 的主输出使能。 */
    HAL_GPIO_WritePin(FOC_DRIVE_ENABLE_PORT,
                      FOC_DRIVE_ENABLE_PIN, GPIO_PIN_RESET);
    CLEAR_BIT(FOC_PWM_TIMER->BDTR, TIM_BDTR_MOE);
}

void FocPwm_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_OC_InitTypeDef oc = {0};
    TIM_BreakDeadTimeConfigTypeDef bd = {0};
    static const uint32_t channels[] = {
        TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3
    };

    /* 1. 开启时钟，先把 PA3 的输出锁存值写低，再切成输出模式。 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();

    HAL_GPIO_WritePin(FOC_DRIVE_ENABLE_PORT,
                      FOC_DRIVE_ENABLE_PIN, GPIO_PIN_RESET);
    gpio.Pin = FOC_DRIVE_ENABLE_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(FOC_DRIVE_ENABLE_PORT, &gpio);

    /* 2. 配置中心对齐计数。PSC=0、ARR=10000，PWM 为 10 kHz。 */
    pwmTimer.Instance = FOC_PWM_TIMER;
    pwmTimer.Init.Prescaler = FOC_PWM_PRESCALER;
    pwmTimer.Init.CounterMode = TIM_COUNTERMODE_CENTERALIGNED1;
    pwmTimer.Init.Period = FOC_PWM_PERIOD_TICKS;
    pwmTimer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    pwmTimer.Init.RepetitionCounter = 0U;
    pwmTimer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_PWM_Init(&pwmTimer) != HAL_OK)
    {
        Error_Handler();
    }

    /*
     * 3. 配置 CH1/2/3，分别对应 U/V/W。
     * 初始三相比较值都设为0，输出由 MOE 保持关闭。
     * 上下桥输入均高电平有效，互补关系由 TIM8 生成。
     */
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = FOC_PWM_INITIAL_DUTY_TICKS;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;

    for (uint32_t i = 0U; i < 3U; ++i)
    {
        if (HAL_TIM_PWM_ConfigChannel(&pwmTimer, &oc, channels[i]) != HAL_OK)
        {
            Error_Handler();
        }
    }

    /* 4. 设置输出空闲状态和死区。当前 100 个死区计数约为 0.5 us。 */
    bd.OffStateRunMode = TIM_OSSR_ENABLE;
    bd.OffStateIDLEMode = TIM_OSSI_ENABLE;
    bd.DeadTime = FOC_PWM_DEAD_TIME_TICKS;

    if (HAL_TIMEx_ConfigBreakDeadTime(&pwmTimer, &bd) != HAL_OK)
    {
        Error_Handler();
    }

    /* 5. 六根 PWM 引脚都切换到 TIM8 的 AF3。 */
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = FOC_PWM_AF;

    gpio.Pin = FOC_PWM_UH_PIN | FOC_PWM_VH_PIN | FOC_PWM_WH_PIN;
    HAL_GPIO_Init(FOC_PWM_HIGH_PORT, &gpio);

    gpio.Pin = FOC_PWM_UL_PIN;
    HAL_GPIO_Init(FOC_PWM_UL_PORT, &gpio);

    gpio.Pin = FOC_PWM_VL_PIN | FOC_PWM_WL_PIN;
    HAL_GPIO_Init(FOC_PWM_VW_LOW_PORT, &gpio);

    /*
     * 6. 准备三对通道，保持 MOE 为 0。
     * 在启动计数前用 UG 装载初始比较值，然后只启动计数器。
     */
    FocPwm_DisableOutputs();
    SET_BIT(FOC_PWM_TIMER->CCER, TIM_CCER_CC1E | TIM_CCER_CC1NE |
                                TIM_CCER_CC2E | TIM_CCER_CC2NE |
                                TIM_CCER_CC3E | TIM_CCER_CC3NE);
    FOC_PWM_TIMER->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&pwmTimer, TIM_FLAG_UPDATE);
    __HAL_TIM_ENABLE(&pwmTimer);
}

bool FocPwm_SetDutyTicks(FocPwmDuty duty)
{
    if (duty.uTicks > FOC_PWM_PERIOD_TICKS ||
        duty.vTicks > FOC_PWM_PERIOD_TICKS ||
        duty.wTicks > FOC_PWM_PERIOD_TICKS)
    {
        return false;
    }

    /*
     * 暂停更新事件，依次写入三个预装载寄存器。
     * 恢复更新后，下一次更新事件会一起装载三相。
     * 集成后由同一个控制中断提交三相比较值。
     */
    SET_BIT(FOC_PWM_TIMER->CR1, TIM_CR1_UDIS);
    FOC_PWM_TIMER->CCR1 = duty.uTicks;
    FOC_PWM_TIMER->CCR2 = duty.vTicks;
    FOC_PWM_TIMER->CCR3 = duty.wTicks;
    CLEAR_BIT(FOC_PWM_TIMER->CR1, TIM_CR1_UDIS);

    return true;
}

void FocPwm_EnableOutputs(void)
{
    /* MOE 只控制 TIM8 输出。后续启动流程再按条件使能驱动板。 */
    SET_BIT(FOC_PWM_TIMER->BDTR, TIM_BDTR_MOE);
}