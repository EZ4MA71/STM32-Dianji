#include "main.h"
#include "motor_io.h"
#include "motor_commutation.h"

TIM_HandleTypeDef htim8;

/* EXTI 写入数据，主循环在临界区中读取快照。 */
typedef struct
{
    uint8_t code;       /* 最近一次读取的霍尔码 */
    uint8_t changed;    /* 有待读取的更新 */
    uint32_t irqCount;  /* 霍尔回调累计次数 */
} MotorHallMonitor;

static volatile MotorHallMonitor hallMonitor;

static void MotorOutputGPIO_Init(void);
static void MotorPWM_Init(void);
static void MotorHall_Init(void);


void MotorIO_Init(void)
{
    MotorOutputGPIO_Init();
    MotorPWM_Init();
    MotorHall_Init();

    /*
     * 初始化结束后：
     * CTRL_SD 保持低电平；
     * 下桥臂保持低电平；
     * PWM 定时器尚未启动。
     */
}


static void MotorOutputGPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /*
     * 先写输出寄存器，再切换为输出模式。
     * PA3：CTRL_SD
     * PA5：U 相下桥臂
     */
    HAL_GPIO_WritePin(MOTOR_ENABLE_PORT,
                      MOTOR_ENABLE_PIN | MOTOR_UL_PIN,
                      GPIO_PIN_RESET);

    /* PB14、PB15：V/W 相下桥臂 */
    HAL_GPIO_WritePin(MOTOR_VL_PORT,
                      MOTOR_VL_PIN | MOTOR_WL_PIN,
                      GPIO_PIN_RESET);

    /* 上桥臂暂时也配置为低电平 GPIO */
    HAL_GPIO_WritePin(MOTOR_PWM_PORT,
                      MOTOR_UH_PIN | MOTOR_VH_PIN | MOTOR_WH_PIN,
                      GPIO_PIN_RESET);

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio.Pin = MOTOR_ENABLE_PIN | MOTOR_UL_PIN;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = MOTOR_VL_PIN | MOTOR_WL_PIN;
    HAL_GPIO_Init(GPIOB, &gpio);

    /*
     * 定时器配置完成后，
     * 再将 PC6、PC7、PC8 切换为 TIM8 复用功能。
     */
    gpio.Pin = MOTOR_UH_PIN | MOTOR_VH_PIN | MOTOR_WH_PIN;
    HAL_GPIO_Init(MOTOR_PWM_PORT, &gpio);
}


static void MotorPWM_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_OC_InitTypeDef oc = {0};
    TIM_BreakDeadTimeConfigTypeDef bd = {0};

    __HAL_RCC_TIM8_CLK_ENABLE();

    /*
     * 当前时钟树：
     * SYSCLK = 400 MHz
     * HCLK   = 200 MHz
     * PCLK2  = 100 MHz
     * TIM8 = 200 MHz
     * PSC  = 19
     * ARR  = 999
     *
     * PWM = 200000000 / 20 / 1000 = 10000 Hz
     */
    htim8.Instance = TIM8;
    htim8.Init.Prescaler = 19U;
    htim8.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim8.Init.Period = MOTOR_PWM_PERIOD;
    htim8.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim8.Init.RepetitionCounter = 0U;
    htim8.Init.AutoReloadPreload =
        TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_PWM_Init(&htim8) != HAL_OK)
    {
        Error_Handler();
    }

    /*
     * PWM 模式 1，高电平有效。
     * Pulse=0：初始占空比为 0。
     *
     * OCN 配置字段设为正常默认值，
     * 但本方案不启动 CHxN 互补输出。
     */
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0U;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCNPolarity = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    oc.OCIdleState = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;

    if (HAL_TIM_PWM_ConfigChannel(
            &htim8, &oc, TIM_CHANNEL_1) != HAL_OK ||
        HAL_TIM_PWM_ConfigChannel(
            &htim8, &oc, TIM_CHANNEL_2) != HAL_OK ||
        HAL_TIM_PWM_ConfigChannel(
            &htim8, &oc, TIM_CHANNEL_3) != HAL_OK)
    {
        Error_Handler();
    }

    /*
     * 配置高级定时器的空闲输出状态。
     * 本次没有分配 BKIN/BKIN2 输入，因此关闭这两路输入。
     */
    bd.OffStateRunMode = TIM_OSSR_ENABLE;
    bd.OffStateIDLEMode = TIM_OSSI_ENABLE;
    bd.LockLevel = TIM_LOCKLEVEL_OFF;
    bd.DeadTime = 0U;

    bd.BreakState = TIM_BREAK_DISABLE;
    bd.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
    bd.BreakFilter = 0U;

    bd.Break2State = TIM_BREAK2_DISABLE;
    bd.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
    bd.Break2Filter = 0U;

    bd.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;

    if (HAL_TIMEx_ConfigBreakDeadTime(&htim8, &bd) != HAL_OK)
    {
        Error_Handler();
    }

    /* PC6、PC7、PC8 切换为 TIM8 的 AF3 复用功能 */
    gpio.Pin = MOTOR_UH_PIN | MOTOR_VH_PIN | MOTOR_WH_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF3_TIM8;

    HAL_GPIO_Init(MOTOR_PWM_PORT, &gpio);
}


static void MotorHall_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* 先关闭对应 NVIC 中断，再完成配置 */
    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
    HAL_NVIC_DisableIRQ(EXTI9_5_IRQn);

    gpio.Pin = MOTOR_HALL_U_PIN |
               MOTOR_HALL_V_PIN |
               MOTOR_HALL_W_PIN;

    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(MOTOR_HALL_PORT, &gpio);

    /* 清除初始化过程中可能产生的中断挂起标志 */
    __HAL_GPIO_EXTI_CLEAR_IT(gpio.Pin);

    HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    HAL_NVIC_ClearPendingIRQ(EXTI9_5_IRQn);

    /* 保存初始状态，并让主循环打印一次 */
    hallMonitor.code = MotorHall_Read();
    hallMonitor.irqCount = 0U;
    hallMonitor.changed = 1U;

    /*
     * PD14、PD15 共用 EXTI15_10 中断入口；
     * PD7 使用 EXTI9_5 中断入口。
     */
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 3U, 0U);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3U, 0U);

    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}


uint8_t MotorHall_Read(void)
{
    /*
     * 三根霍尔线都在 GPIOD。
     * 一次读取 IDR，取得同一时刻的三路状态。
     */
    const uint32_t pins = MOTOR_HALL_PORT->IDR;

    /* 对应换相表：bit2=W，bit1=V，bit0=U。 */
    return (uint8_t)(
        ((pins & MOTOR_HALL_W_PIN) ? 4U : 0U) |
        ((pins & MOTOR_HALL_V_PIN) ? 2U : 0U) |
        ((pins & MOTOR_HALL_U_PIN) ? 1U : 0U));
}


void HAL_GPIO_EXTI_Callback(uint16_t gpioPin)
{
    /* 步骤 1：只响应本模块的三条霍尔线路。 */
    if (gpioPin == MOTOR_HALL_U_PIN ||
        gpioPin == MOTOR_HALL_V_PIN ||
        gpioPin == MOTOR_HALL_W_PIN)
    {
        /* 读取同一时刻的三路霍尔状态 */
        /* 步骤 2：无论哪一根触发，都重新读取三根线的完整状态。 */
        hallMonitor.code = MotorHall_Read();

        /* 立即根据霍尔码更新六路控制信号 */
        /* 步骤 3：换相模块仅在 RUNNING 改输出；停止时仍继续监测霍尔。 */
        MotorCommutation_ApplyHall(hallMonitor.code);

        /* 保留原来的计数和串口打印通知 */
        /* 步骤 4：累计回调次数，通知主循环之后取快照/打印。 */
        ++hallMonitor.irqCount;
        hallMonitor.changed = 1U;
    }
}


uint8_t MotorHall_TakeSnapshot(uint8_t *state,
                               uint32_t *irqCount)
{
    uint8_t changed;
    const uint32_t primask = __get_PRIMASK();

    /*
     * 短暂关闭中断，保证：
     * 读取状态、读取计数、清除更新标志
     * 不会被一次新的霍尔中断打断。
     */
    __disable_irq();

    changed = hallMonitor.changed;
    *state = hallMonitor.code;
    *irqCount = hallMonitor.irqCount;
    hallMonitor.changed = 0U;

    /* 恢复进入函数之前的中断屏蔽状态 */
    __set_PRIMASK(primask);

    return changed;
}
