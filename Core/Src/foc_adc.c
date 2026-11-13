#include "main.h"
#include "foc_adc.h"

/*
 * 相电流链路：TIM8 选定 PWM 周期内的采样点 → TRGO2 上升沿 →
 * ADC1/2/3 各采一相 → 各自的 JDR1 出结果 → 软件收齐同一拍的 U/V/W。
 * 三台 ADC 用同一触发并行采样，避免一个 ADC 扫三路时的相间采样偏移。
 * 母线电压（PC2_C/ADC3_INP0）和板温（PA4/ADC1_INP18）预留给低频规则组。
 */

typedef enum
{
    FOC_ADC_PHASE_U,
    FOC_ADC_PHASE_V,
    FOC_ADC_PHASE_W,
    FOC_ADC_PHASE_COUNT
} FocAdcPhase;

typedef struct
{
    uint16_t pending[FOC_ADC_PHASE_COUNT]; /* 正在收集的三路结果。 */
    uint32_t readyMask;                  /* 每到一路，就置对应的位。 */
    volatile FocAdcCurrentFrame latest;   /* 三路收齐后发布的快照。 */
    volatile bool hasFrame;              /* 第一次收齐三相后置位。 */
    volatile bool fault;                 /* ADC 报错或收帧异常，复位前保持。 */
} FocAdcState;

static ADC_HandleTypeDef phaseAdc[FOC_ADC_PHASE_COUNT];
static FocAdcState adcState;

static void focAdcConfigCurrent(ADC_HandleTypeDef *adc,
                                ADC_TypeDef *instance,
                                uint32_t channel)
{
    ADC_InjectionConfTypeDef injected = {0};

    adc->Instance = instance; /* U、V、W 分别传入 ADC1、ADC2、ADC3。 */

    /*
     * 选异步时钟，让 ADC 取 CKPER（25 MHz HSE）。DIV2 得到 12.5 MHz；
     * H743 Rev V 的模拟 ADC 再硬件除以 2，实际为 6.25 MHz，见 ST AN5312。
     * 其他异步分频如 DIV1、DIV4 会改变每个采样周期的时长。
     * 同步模式改用总线相关时钟，须按参考手册的时钟约束选档。
     * ADC 时钟用于计算转换时间，TIM8 时钟用于计算触发时刻。
     */
    adc->Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV2;

    /* 12 位为 0～4095，VREF+ 为 3.3 V 时约 0.806 mV/码。14/16 位量化更细、转换更久，8/10 位反之。 */
    adc->Init.Resolution = ADC_RESOLUTION_12B;

    /* DISABLE 使规则组和注入组都只转换 Rank 1。ENABLE 可按 Rank 顺序扫描多路。 */
    adc->Init.ScanConvMode = ADC_SCAN_DISABLE;
    /* 规则组序列长度为 1。开启扫描后可配置多个 Rank，实际通道另行配置。 */
    adc->Init.NbrOfConversion = 1U;

    /* SEQ_CONV 一轮结束才通知，注入组使用 JEOS；SINGLE_CONV 每个 Rank 完成通知，注入组使用 JEOC。 */
    adc->Init.EOCSelection = ADC_EOC_SEQ_CONV;

    /* 规则组预留软件启动，供以后低频读取母线、板温；也可选受支持的定时器触发。 */
    adc->Init.ExternalTrigConv = ADC_SOFTWARE_START;
    /* 软件启动忽略边沿配置。改用硬件触发时，可选上升、下降或双边沿。 */
    adc->Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;

    /*
     * 规则组结果留在 DR，由软件读取。DMA_ONESHOT 搬运一批后停止，
     * DMA_CIRCULAR 可反复填充缓冲区。这一项只管规则组，注入结果在 JDR1。
     */
    adc->Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;

    /* 规则组溢出时保留旧值。OVERWRITTEN 则保留最新转换值。 */
    adc->Init.Overrun = ADC_OVR_DATA_PRESERVED;

    /* 保留原始结果位宽。配置左移会改变读数的数值比例。 */
    adc->Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;

    /*
     * 句柄零初始化关闭连续转换、间断转换、自动等待和规则组过采样。
     * 连续模式一次启动后重复转换；间断模式把多 Rank 序列拆成多次触发。
     * 自动等待让后续转换受读取时机影响；过采样对多次转换累加并移位，
     * 可减小部分随机噪声，但增加延迟。相电流的启动时机由 TIM8 决定。
     */
    if (HAL_ADC_Init(adc) != HAL_OK)
    {
        Error_Handler();
    }

    /* 按该相接线选物理通道，U/V/W 的通道号见 foc_adc.h。 */
    injected.InjectedChannel = channel;
    /* Rank 指该 ADC 内的转换次序。三台 ADC 各有一相，所以都用 Rank 1。 */
    injected.InjectedRank = ADC_INJECTED_RANK_1;
    /* 每次触发只做一次转换。单 ADC 要采多个 Rank 时需启用扫描并配置序列。 */
    injected.InjectedNbrOfConversion = 1U;

    /* 三相统一选 16.5 周期。其他采样档位和时间换算见 foc_adc.h。 */
    injected.InjectedSamplingTime = FOC_ADC_CURRENT_SAMPLE_TIME;

    /* 板上放大器输出是对地电压，选单端。差分模式需要匹配的正负输入对。 */
    injected.InjectedSingleDiff = ADC_SINGLE_ENDED;

    /* 保留原始值。硬件 Offset 可在 ADC 内扣偏置，这里留给后续软件校零。 */
    injected.InjectedOffsetNumber = ADC_OFFSET_NONE;

    /*
     * 三台 ADC 等同一个 TIM8_TRGO2。TIM8 用 CH4 的内部 OC4REF 作为 TRGO2，
     * CH4 只定采样时刻，无需 GPIO。软件触发可用 ADC_INJECTED_SOFTWARE_START，
     * 适合按需采样；固定在 PWM 某一拍内采样时用定时器触发。
     */
    injected.ExternalTrigInjecConv = ADC_EXTERNALTRIGINJEC_T8_TRGO2;

    /*
     * CH4 配 PWM2 且 TRGO2 选 OC4REF 时，上数到 CCR4 产生上升沿，下数产生
     * 下降沿。RISING 每个完整 PWM 周期触发一次，FALLING 改为下数时触发，
     * RISINGFALLING 两次都触发，NONE 关闭硬件边沿检测。
     * 200 MHz 下，CCR4=9900 对应上数约 49.5 us，可用于驱动关断时验证链路。
     * 带电后，触发前给死区、驱动和放大器留稳定时间，触发后还要把采样
     * 电容跟踪的整段时间放进三相低侧稳定导通的 000 窗口。95% 最大占空比
     * 时，理想窗口为 47.5～52.5 us。实际余量还取决于芯片修订与板上波形。
     */
    injected.ExternalTrigInjecConvEdge = ADC_EXTERNALTRIGINJECCONV_EDGE_RISING;

    /*
     * 零初始化关闭注入队列、自动注入、间断转换和过采样。
     * 队列用于预先安排多套注入配置；自动注入在规则组完成后启动注入组。
     * 间断模式每次触发只推进一个 Rank；过采样用多次转换生成一个结果。
     * 当前只需固定 Rank 1，在每次 TIM8 触发后采一次。
     *
     * 启动前用 ADC_CALIB_OFFSET 校正 ADC 自身的单端偏移，OFFSET_LINEARITY
     * 还会执行线性校准。驱动板放大器的零电流偏置另测：功率级关断且无相
     * 电流时取多帧均值，运行时各相用自己的原始码减去该均值。
     */
    if (HAL_ADCEx_InjectedConfigChannel(adc, &injected) != HAL_OK ||
        HAL_ADCEx_Calibration_Start(adc, ADC_CALIB_OFFSET,
                                    ADC_SINGLE_ENDED) != HAL_OK)
    {
        Error_Handler();
    }
}

void FocAdc_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 打开 PA6/PB1 所在 GPIO 和内部模拟开关所属 SYSCFG 的时钟门控。 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    /* CKPER 可选 HSI、CSI 或 HSE。这里沿用板上的 25 MHz HSE。 */
    __HAL_RCC_CLKP_CONFIG(RCC_CLKPSOURCE_HSE);

    /* ADC 内核时钟选 CKPER，直接使用现有 HSE。也可用 PLL2 或 PLL3 提供所需的 ADC 时钟。 */
    __HAL_RCC_ADC_CONFIG(RCC_ADCCLKSOURCE_CLKP);
    __HAL_RCC_ADC12_CLK_ENABLE(); /* ADC1 和 ADC2 共用这一时钟门控。 */
    __HAL_RCC_ADC3_CLK_ENABLE();  /* ADC3 单独开时钟。 */

    /* ANALOG 关闭数字输入缓冲，数字输入、输出和复用模式用于数字信号。 */
    gpio.Mode = GPIO_MODE_ANALOG;
    /* NOPULL 不施加内部偏置；PULLUP/PULLDOWN 用于需要数字默认电平的场合。 */
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin = FOC_ADC_U_PIN; /* PA6 用于 U 相。 */
    HAL_GPIO_Init(FOC_ADC_U_PORT, &gpio);

    gpio.Pin = FOC_ADC_V_PIN; /* PB1 用于 V 相，沿用模拟模式和无上下拉。 */
    HAL_GPIO_Init(FOC_ADC_V_PORT, &gpio);

    /*
     * OPEN 将 PC3 与 PC3_C 的内部模拟开关断开。
     * 当前使用 PC3_C 直连 ADC3 的路径。CLOSE 用于需要连通两侧的配置。
     */
    HAL_SYSCFG_AnalogSwitchConfig(SYSCFG_SWITCH_PC3, SYSCFG_SWITCH_PC3_OPEN);

    focAdcConfigCurrent(&phaseAdc[FOC_ADC_PHASE_U], ADC1, FOC_ADC_U_CHANNEL);
    focAdcConfigCurrent(&phaseAdc[FOC_ADC_PHASE_V], ADC2, FOC_ADC_V_CHANNEL);
    focAdcConfigCurrent(&phaseAdc[FOC_ADC_PHASE_W], ADC3, FOC_ADC_W_CHANNEL);

    /*
     * ADC1/2 共用一个中断入口，ADC3 有单独入口。
     * 两个入口同为抢占优先级 5，收集结果时彼此不会抢占。
     * 当前 NVIC 分组的优先级位都用于抢占，子优先级填 0。
     */
    HAL_NVIC_SetPriority(ADC_IRQn, FOC_ADC_IRQ_PRIORITY, 0U);
    HAL_NVIC_SetPriority(ADC3_IRQn, FOC_ADC_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(ADC_IRQn);
    HAL_NVIC_EnableIRQ(ADC3_IRQn);

    /*
     * Start_IT 让三个 ADC 等待外部触发并打开完成中断。应先让三台都就绪，
     * 再让 TIM8 发第一拍。转换完成后，中断从每台 ADC 的 JDR1 取结果，
     * U/V/W 都到齐才发布一帧并增加 sequence，避免读到只更新了一部分的帧。
     */
    for (uint32_t phase = 0U; phase < FOC_ADC_PHASE_COUNT; ++phase)
    {
        if (HAL_ADCEx_InjectedStart_IT(&phaseAdc[phase]) != HAL_OK)
        {
            Error_Handler();
        }
    }
}

void ADC_IRQHandler(void)
{
    /* ADC1 和 ADC2 共用入口。HAL 分别检查每台 ADC 的中断标志。 */
    HAL_ADC_IRQHandler(&phaseAdc[FOC_ADC_PHASE_U]);
    HAL_ADC_IRQHandler(&phaseAdc[FOC_ADC_PHASE_V]);
}

void ADC3_IRQHandler(void)
{
    /* ADC3 的注入完成事件通过独立入口处理。 */
    HAL_ADC_IRQHandler(&phaseAdc[FOC_ADC_PHASE_W]);
}

void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    FocAdcPhase phase;

    if (hadc == &phaseAdc[FOC_ADC_PHASE_U])
    {
        phase = FOC_ADC_PHASE_U;
    }
    else if (hadc == &phaseAdc[FOC_ADC_PHASE_V])
    {
        phase = FOC_ADC_PHASE_V;
    }
    else if (hadc == &phaseAdc[FOC_ADC_PHASE_W])
    {
        phase = FOC_ADC_PHASE_W;
    }
    else
    {
        return;
    }

    if (adcState.fault)
    {
        return;
    }

    const uint32_t phaseBit = 1UL << phase;
    if ((adcState.readyMask & phaseBit) != 0U)
    {
        /* 未收齐就再次收到同一相，锁存故障直到复位。 */
        adcState.fault = true;
        return;
    }

    /* 每台 ADC 只配置了一个注入 Rank，因此读该 ADC 的 JDR1。 */
    adcState.pending[phase] =
        (uint16_t)HAL_ADCEx_InjectedGetValue(hadc, ADC_INJECTED_RANK_1);
    adcState.readyMask |= phaseBit;

    /* 两个 ADC 中断入口同抢占优先级，收帧期间不会互相抢占。 */
    const uint32_t allPhasesMask = (1UL << FOC_ADC_PHASE_COUNT) - 1UL;
    if (adcState.readyMask == allPhasesMask)
    {
        adcState.latest.uRaw = adcState.pending[FOC_ADC_PHASE_U];
        adcState.latest.vRaw = adcState.pending[FOC_ADC_PHASE_V];
        adcState.latest.wRaw = adcState.pending[FOC_ADC_PHASE_W];
        ++adcState.latest.sequence;
        adcState.hasFrame = true;
        adcState.readyMask = 0U;
    }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &phaseAdc[FOC_ADC_PHASE_U] ||
        hadc == &phaseAdc[FOC_ADC_PHASE_V] ||
        hadc == &phaseAdc[FOC_ADC_PHASE_W])
    {
        /* 发生 ADC 错误后，读帧接口停止返回数据，保留快照供调试。 */
        adcState.fault = true;
    }
}

bool FocAdc_ReadCurrents(FocAdcCurrentFrame *frame)
{
    if (frame == NULL)
    {
        return false;
    }

    /* 复制快照期间屏蔽中断，防止三相数据和序号来自不同帧。 */
    const uint32_t irqMask = __get_PRIMASK();
    __disable_irq();

    const bool valid = adcState.hasFrame && !adcState.fault;
    if (valid)
    {
        *frame = adcState.latest;
    }

    /* 恢复调用前的中断屏蔽状态。 */
    __set_PRIMASK(irqMask);
    return valid;
}
