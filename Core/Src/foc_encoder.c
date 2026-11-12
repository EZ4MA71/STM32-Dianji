#include "main.h"
#include "foc_encoder.h"

#define FOC_ENCODER_TIMER         TIM2             /* TIM2/TIM5 为 32 位，TIM3/TIM4 为 16 位，也支持编码器。 */
#define FOC_ENCODER_COUNTER_TOP   UINT32_MAX       /* ARR 上限 0xFFFFFFFF；更小的 ARR 会更早回绕。 */
#define FOC_ENCODER_AF            GPIO_AF1_TIM2    /* PA15/PB3 的 TIM2 复用号，换脚时按芯片复用表查 AF。 */

#define FOC_ENCODER_A_PORT        GPIOA
#define FOC_ENCODER_A_PIN         GPIO_PIN_15      /* A 相进 TIM2_CH1。换脚要保留对应的定时器输入功能。 */
#define FOC_ENCODER_B_PORT        GPIOB
#define FOC_ENCODER_B_PIN         GPIO_PIN_3       /* B 相进 TIM2_CH2。换端口时同时改端口宏和 RCC 时钟。 */
#define FOC_ENCODER_Z_PORT        GPIOD
#define FOC_ENCODER_Z_PIN         GPIO_PIN_7       /* Z 相进 EXTI7。换脚时同步核对 EXTI 线号和中断入口。 */

/*
 * A/B 共用数字滤波档位 ICxF。N 是确认一次电平变化所需的连续稳定采样数。
 * 0         关闭数字滤波
 * 1 / 2 / 3 采样频率 fCK_INT，N 分别为 2 / 4 / 8
 * 4 / 5     采样频率 fDTS/2，N 分别为 6 / 8
 * 6 / 7     采样频率 fDTS/4，N 分别为 6 / 8
 * 8 / 9     采样频率 fDTS/8，N 分别为 6 / 8
 * 10/11/12  采样频率 fDTS/16，N 分别为 5 / 6 / 8
 * 13/14/15  采样频率 fDTS/32，N 分别为 5 / 6 / 8
 * fCK_INT 为定时器内核时钟，fDTS 由 ClockDivision 决定。
 * 滤波窗口越长，越容易滤掉短毛刺，也越可能漏掉高速时的有效窄脉冲。
 */
#define FOC_ENCODER_INPUT_FILTER  0U
#define FOC_ENCODER_Z_IRQ         EXTI9_5_IRQn     /* EXTI5～9 共用中断，当前处理 PD7 的 EXTI7。 */
#define FOC_ENCODER_Z_PRIORITY    6U              /* 抢占优先级。分组相同时，数值越小优先级越高。 */

/* Z 中断需要维护的三项状态。 */
typedef struct
{
    uint32_t count;
    uint32_t position;
    int32_t delta;
} FocEncoderIndex;

static TIM_HandleTypeDef encoderTimer;
static volatile FocEncoderIndex encoderIndex;

void FocEncoder_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_Encoder_InitTypeDef config = {0};

    /* 1. 开启 A、B、Z 对应的 GPIO 时钟，以及 TIM2 时钟。 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    /* 2. PA15 和 PB3 都切到 AF1，分别接入 TIM2 的 CH1 和 CH2。 */
    gpio.Mode = GPIO_MODE_AF_PP;       /* 复用模式把引脚交给 TIM2。GPIO_MODE_INPUT 则配置为普通输入。 */
    gpio.Pull = GPIO_NOPULL;           /* 可选 GPIO_PULLUP / GPIO_PULLDOWN，给空闲电平提供上拉或下拉。 */
    gpio.Alternate = FOC_ENCODER_AF;   /* 当前 AF1；其他 AF 对应的外设功能由引脚复用表决定。 */

    gpio.Pin = FOC_ENCODER_A_PIN;
    HAL_GPIO_Init(FOC_ENCODER_A_PORT, &gpio);

    gpio.Pin = FOC_ENCODER_B_PIN;
    HAL_GPIO_Init(FOC_ENCODER_B_PORT, &gpio);

    /* 3. 配置 TIM2 计数器和 A/B 输入。 */
    encoderTimer.Instance = FOC_ENCODER_TIMER;  /* 选 TIM2，换实例时要一起核对 GPIO 复用和时钟。 */

    /* PSC=0 即除以 1。普通时基中 PSC=N 即除以 N+1，编码器这里用 0 保留每步计数。 */
    encoderTimer.Init.Prescaler = 0U;

    /*
     * 普通时基可选 UP（递增）、DOWN（递减）、CENTERALIGNED1/2/3（上下往返，
     * 分别在下数、上数、两个方向置比较标志）。编码器保持 UP 作为初始配置，
     * 运行中的 DIR 由 TIM2 根据 A/B 相序自动更新。
     */
    encoderTimer.Init.CounterMode = TIM_COUNTERMODE_UP;

    /* Period 写入 ARR。保留 32 位全范围，当前索引差值按 32 位回绕计算。 */
    encoderTimer.Init.Period = FOC_ENCODER_COUNTER_TOP;

    /* CKD 可选 DIV1 / DIV2 / DIV4，tDTS 分别为 1 / 2 / 4 个内核时钟周期。 */
    encoderTimer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;

    /* DISABLE 使 ARR 写入直接生效，ENABLE 使新 ARR 在更新事件时装载。 */
    encoderTimer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    /*
     * TI1：只用 A 的双边沿计数，B 提供方向（2 倍频）。
     * TI2：只用 B 的双边沿计数，A 提供方向（2 倍频）。
     * TI12：A、B 的上升沿和下降沿都计数（4 倍频）。
     */
    config.EncoderMode = TIM_ENCODERMODE_TI12;

    /* A 相极性：RISING 为原极性，FALLING 为反相。有效边沿由编码器模式决定。 */
    config.IC1Polarity = TIM_ICPOLARITY_RISING;

    /* IC1 输入来源：DIRECTTI 选 TI1（A），INDIRECTTI 选 TI2（B），TRC 选内部触发。 */
    config.IC1Selection = TIM_ICSELECTION_DIRECTTI;

    /* 捕获分频 DIV1/2/4/8 表示每 1/2/4/8 次捕获事件记录一次。编码器倍频由 TI 模式决定。 */
    config.IC1Prescaler = TIM_ICPSC_DIV1;

    /* A 相数字滤波当前为 0，其他档位见 FOC_ENCODER_INPUT_FILTER 上方的表。 */
    config.IC1Filter = FOC_ENCODER_INPUT_FILTER;

    /* B 相极性：RISING 为原极性，FALLING 为反相。有效边沿由编码器模式决定。 */
    config.IC2Polarity = TIM_ICPOLARITY_RISING;

    /* IC2 输入来源：DIRECTTI 选 TI2（B），INDIRECTTI 选 TI1（A），TRC 选内部触发。 */
    config.IC2Selection = TIM_ICSELECTION_DIRECTTI;

    /* 捕获分频可选 DIV1/2/4/8。这里保持 DIV1，A/B 的编码器计数由 TI12 处理。 */
    config.IC2Prescaler = TIM_ICPSC_DIV1;

    /* B 相数字滤波沿用 A 相的档位，可选值和采样频率相同。 */
    config.IC2Filter = FOC_ENCODER_INPUT_FILTER;

    if (HAL_TIM_Encoder_Init(&encoderTimer, &config) != HAL_OK)
    {
        Error_Handler();
    }

    /* 4. CNT 从 0 开始，作为上电后的相对位置基准。 */
    __HAL_TIM_SET_COUNTER(&encoderTimer, 0U);
    /* TIM_CHANNEL_ALL 同时使能 CH1/CH2，TI12 需要两路 A/B 输入。 */
    if (HAL_TIM_Encoder_Start(&encoderTimer, TIM_CHANNEL_ALL) != HAL_OK)
    {
        Error_Handler();
    }

    /* 5. PD7 用上升沿中断接收 Z 索引。 */
    gpio.Pin = FOC_ENCODER_Z_PIN;
    /* RISING 记上升沿；FALLING 记下降沿；RISING_FALLING 会记录脉冲的两个边沿。 */
    gpio.Mode = GPIO_MODE_IT_RISING;
    HAL_GPIO_Init(FOC_ENCODER_Z_PORT, &gpio);

    __HAL_GPIO_EXTI_CLEAR_IT(FOC_ENCODER_Z_PIN);  /* 开放中断前清掉已有的挂起标志。 */
    /* 参数依次是中断号、抢占优先级、子优先级。子优先级用于同级中断排队。 */
    HAL_NVIC_SetPriority(FOC_ENCODER_Z_IRQ, FOC_ENCODER_Z_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(FOC_ENCODER_Z_IRQ);
}

void EXTI9_5_IRQHandler(void)
{
    /* EXTI5～9 共用入口，这里只处理编码器 Z 所在的 EXTI7。 */
    if (__HAL_GPIO_EXTI_GET_IT(FOC_ENCODER_Z_PIN) != 0U)
    {
        const uint32_t position = __HAL_TIM_GET_COUNTER(&encoderTimer);
        __HAL_GPIO_EXTI_CLEAR_IT(FOC_ENCODER_Z_PIN);

        /*
         * 收到第二个 Z 后才有两次位置可相减。
         * 先做 32 位无符号差，再取有符号结果，可以跨过 CNT 的回绕点。
         */
        if (encoderIndex.count != 0U)
        {
            encoderIndex.delta = (int32_t)(position - encoderIndex.position);
        }

        encoderIndex.position = position;
        ++encoderIndex.count;
    }
}

void FocEncoder_Read(FocEncoderSnapshot *snapshot)
{
    /* 复制快照时短暂屏蔽中断，避免三项 Z 数据来自不同次索引。 */
    const uint32_t irqMask = __get_PRIMASK();
    __disable_irq();

    snapshot->count = __HAL_TIM_GET_COUNTER(&encoderTimer);
    snapshot->indexCount = encoderIndex.count;
    snapshot->indexPosition = encoderIndex.position;
    snapshot->indexDelta = encoderIndex.delta;

    __set_PRIMASK(irqMask);
}