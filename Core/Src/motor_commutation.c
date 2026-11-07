/*
 * 阅读路线：
 * 1. Init 初始化，输出保持关闭。
 * 2. main 收到 g 后调用 Start，检查霍尔并给自举电容预充电。
 * 3. 霍尔边沿进入 ApplyHall，按表选择“一个上桥 PWM + 另一个下桥常开”。
 * 4. SysTick 每毫秒调用 Tick1ms，检查启动和霍尔反馈是否超时。
 * 5. main 收到 x 调用 Stop；异常则由 FaultOff/EmergencyStop 关闭。
 *
 * 这里使用 H_PWM-L_ON：上桥由 TIM8 控制，下桥由普通 GPIO 控制。
 * PWM 定时器计数、通道使能、MOE 总输出、PA3 驱动使能，是不同层次的开关。
 * 正常换相只暂时关六路信号；真正停机时还需要把 PA3 拉低。
 */
#include "motor_commutation.h"
#include "motor_io.h"
#include "main.h"

/*
 * 上桥 PWM 频率在 motor_io.c 中配置为 10 kHz。
 * 5 us 全关间隔和 2 ms 预充电是调试初始值，尚未实测本板的功率管波形。
 * GPIO 下桥不会受 TIM8 的互补死区硬件控制，因此这里显式全关并等待。
 */
/* 换相时六路全关后的等待时间，单位 us。 */
#define COMMUTATION_BLANK_US       5U
/* 启动前两次霍尔读取的间隔；要求两次有效且相同。 */
#define HALL_STABLE_MS             5U
/* 上桥全关、下桥全开的预充电等待时间，单位 ms。 */
#define BOOTSTRAP_PRECHARGE_MS     2U
/* 只限制 STARTING 阶段耗时，不限制正常运行总时长。 */
#define STARTUP_TIMEOUT_MS         100U
/* RUNNING 状态下，多久没有有效霍尔变化就按故障关断。 */
#define STALL_TIMEOUT_MS           500U
/* 本示例允许的最低占空比为 30/1000，也就是 3%。 */
#define DUTY_MIN_PERMILLE          30U
/* 占空比上限为 500/1000，即 50%。 */
#define DUTY_MAX_PERMILLE          500U
/* 串口每收到一个 +，增加 50/1000，也就是 5 个百分点。 */
#define DUTY_STEP_PERMILLE          50U

/* 这里的 1/2/3 是相编号，不是霍尔码，也不是 GPIO 引脚号。 */
enum { PHASE_U = 1, PHASE_V = 2, PHASE_W = 3 };

/*
 * 数组下标直接等于霍尔码：bit2=W、bit1=V、bit0=U。
 * 每一行的两列分别是 {上桥 PWM 的相，下桥保持导通的相}。
 * 上下桥选择不同相，第三相的上下桥均关闭。
 * 这是按传感器状态查表，不是让软件按数字 1、2、3 依次换相。
 */
static const uint8_t forwardTable[8][2] =
{
    {0, 0},                   /* hall=0：本换相表中无效 */
    {PHASE_U, PHASE_W},        /* hall=1：UH PWM，WL ON */
    {PHASE_V, PHASE_U},        /* hall=2：VH PWM，UL ON */
    {PHASE_V, PHASE_W},        /* hall=3：VH PWM，WL ON */
    {PHASE_W, PHASE_V},        /* hall=4：WH PWM，VL ON */
    {PHASE_U, PHASE_V},        /* hall=5：UH PWM，VL ON */
    {PHASE_W, PHASE_U},        /* hall=6：WH PWM，UL ON */
    {0, 0}                    /* hall=7：本换相表中无效 */
};

/* 换相模块的运行状态与计时数据集中在一个对象中。 */
typedef struct
{
    volatile uint8_t initialized;    /* 初始化是否完成 */
    volatile MotorState state;      /* 停止、启动、运行或故障 */
    volatile MotorFault fault;       /* 当前故障原因 */
    volatile uint8_t lastHall;       /* 上一次应用的霍尔码 */
    volatile uint16_t dutyPermille;  /* 当前占空比，单位为千分比 */
    MotorDirection direction;        /* 本次运行方向 */
    volatile uint32_t lastEdgeTick;  /* 上一次有效霍尔变化的时间 */
    uint32_t startupTick;            /* 进入启动阶段的时间 */
    uint32_t cyclesPerUs;            /* DWT 每微秒的计数周期数 */
} MotorControl;

static MotorControl motor =
{
    .state = MOTOR_STOPPED,
    .fault = MOTOR_FAULT_NONE,
    .lastHall = 0xFFU
};

/*
 * 立即关闭功率级，供停机/故障使用。
 * 直接使用寄存器，不依赖已经配置好的 htim8 句柄。
 * 先检查外设时钟，允许 Error_Handler 在初始化尚未完成时调用。
 */
/* 本函数不等待转子停下，只撤销驱动控制。 */
static void HardwareOff(void)
{
    /* 步骤 1：PA3 驱动使能与 PA8 下桥一起拉低。BSRR 高 16 位表示复位引脚。 */
    if (__HAL_RCC_GPIOA_IS_CLK_ENABLED())
    {
        GPIOA->BSRR =
            ((uint32_t)(MOTOR_ENABLE_PIN | MOTOR_UL_PIN) << 16U);
    }
    /* 步骤 2：清除 TIM8 的 MOE，关闭三个上桥输出；定时器计数可以继续。 */
    if (__HAL_RCC_TIM8_IS_CLK_ENABLED())
    {
        CLEAR_BIT(TIM8->BDTR, TIM_BDTR_MOE);
    }
    /* 步骤 3：PB14/PB15 拉低，关闭另外两个下桥。 */
    if (__HAL_RCC_GPIOB_IS_CLK_ENABLED())
    {
        GPIOB->BSRR =
            ((uint32_t)(MOTOR_VL_PIN | MOTOR_WL_PIN) << 16U);
    }
}

/*
 * 暂时关闭六路信号，供正常换相使用。
 * PA3 保持原电平：运行期间保持使能，不在每次换相时复位板卡保护锁存。
 * 本函数中的 CCR=0 先写入预装载值；MOE=0 才是立即抑制上桥输出的动作。
 */
/* 调用前已由 MotorIO_Init 配置 TIM8 和输出 GPIO。 */
static void SignalsOff(void)
{
    /* 步骤 1：无条件关闭上桥总输出，避免旧 CCR 在等待更新期间继续产生脉冲。 */
    __HAL_TIM_MOE_DISABLE_UNCONDITIONALLY(&htim8);

    /* 步骤 2：关闭三个 GPIO 下桥；当前 VL/WL 同属于 GPIOB。 */
    HAL_GPIO_WritePin(MOTOR_UL_PORT, MOTOR_UL_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(MOTOR_VL_PORT,
                      MOTOR_VL_PIN | MOTOR_WL_PIN, GPIO_PIN_RESET);

    /* 步骤 3：将三个通道下一次装载的占空比清零。 */
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, 0U);
    __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_3, 0U);
}

/*
 * 进入故障锁定：内部辅助函数，调用者应已经屏蔽中断。
 * 故障不会因后续霍尔恢复正常而自动重启；需处理原因后 x，再 g。
 */
/* reason 是这次关断的故障编号。 */
static void FaultOff(MotorFault reason)
{
    /* 步骤 1：先把状态改为故障，阻止后续换相逻辑再次打开输出。 */
    motor.state = MOTOR_FAULT;
    motor.fault = reason;
    motor.dutyPermille = 0U;
    motor.lastHall = 0xFFU;

    /* 步骤 2：立即关 PA3 和六路输出。初始化完成后，再清零各 CCR 预装载值。 */
    HardwareOff();
    if (motor.initialized != 0U)
    {
        SignalsOff();
    }
}

/*
 * 基于 CPU 周期计数器的短暂忙等待。
 * 400 MHz 时等待 5 us，相当于等待至少 2000 个 CPU 周期。
 * 这里不用 HAL_Delay，因为它以毫秒为单位且依赖 SysTick 中断推进。
 */
static void DelayUs(uint32_t us)
{
    /* 记录起点，再把“微秒”换算为“CPU 周期数”。 */
    const uint32_t begin = DWT->CYCCNT;
    const uint32_t cycles = motor.cyclesPerUs * us;

    /* 无符号差值可以处理一次 32 位计数器回绕；等待期间 CPU 不做其他主线工作。 */
    while ((uint32_t)(DWT->CYCCNT - begin) < cycles)
    {
        __NOP();
    }
}

/*
 * 只判断这个霍尔码是否属于所给表中的 1~6。
 * 有效编码不等于已经证明电机相线/转向正确；对应关系此前由你核对。
 */
static uint8_t ValidHall(uint8_t hall)
{
    return (uint8_t)(hall >= 1U && hall <= 6U);
}

/*
 * 把霍尔码翻译为两个相编号。调用前必须确认 hall 在 1~6。
 * 指针 upper/lower 用来把结果写回调用者的变量。
 */
static void GetPhases(uint8_t hall, uint8_t *upper, uint8_t *lower)
{
    /* 步骤 1：先取正转表中的上桥相和下桥相。 */
    *upper = forwardTable[hall][0];
    *lower = forwardTable[hall][1];

    /* 步骤 2：反转表在同一霍尔码下交换这两个相；保持你提供的表的对应关系。 */
    if (motor.direction == MOTOR_DIR_REVERSE)
    {
        const uint8_t temporary = *upper;
        *upper = *lower;
        *lower = temporary;
    }
}

/*
 * 给指定上桥写入 CCR：U/V/W 对应 TIM8 CH1/CH2/CH3。
 * 不负责使能 MOE，也不负责清除其他通道；换相前 SignalsOff 已清零其他 CCR。
 */
static void SetUpperDuty(uint8_t phase, uint16_t permille)
{
    /* CCR=(ARR+1)*占空比。ARR=999、permille=80 时，CCR=80，即 8%。 */
    const uint32_t compare =
        ((__HAL_TIM_GET_AUTORELOAD(&htim8) + 1U) * permille) / 1000U;

    /* 根据相编号选择通道，写比较值。CCR 的预装载何时生效由更新事件决定。 */
    switch (phase)
    {
        case PHASE_U:
            __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, compare);
            break;
        case PHASE_V:
            __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, compare);
            break;
        case PHASE_W:
            __HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_3, compare);
            break;
        default:
            break;
    }
}

/*
 * 将选中的一个下桥 GPIO 拉高，形成 H_PWM-L_ON 中的 L_ON。
 * 其他下桥应已被 SignalsOff 关闭；这里不输出下桥 PWM。
 */
static void LowerOn(uint8_t phase)
{
    /* U 下桥 PA8，V 下桥 PB14，W 下桥 PB15。 */
    switch (phase)
    {
        case PHASE_U:
            HAL_GPIO_WritePin(MOTOR_UL_PORT, MOTOR_UL_PIN, GPIO_PIN_SET);
            break;
        case PHASE_V:
            HAL_GPIO_WritePin(MOTOR_VL_PORT, MOTOR_VL_PIN, GPIO_PIN_SET);
            break;
        case PHASE_W:
            HAL_GPIO_WritePin(MOTOR_WL_PORT, MOTOR_WL_PIN, GPIO_PIN_SET);
            break;
        default:
            break;
    }
}

/*
 * 紧急关断：Error_Handler 或 CPU 异常入口调用。
 * 可以在主循环或异常中调用，不使用 printf、HAL_Delay 等等待操作。
 * 它是软件尽力关断处理，并不能在 CPU 已无法执行指令时提供硬件关断保证。
 */
void MotorCommutation_EmergencyStop(void)
{
    /*
     * 步骤 1：记住进入前的中断屏蔽状态，再屏蔽普通可屏蔽中断。
     * PRIMASK 不能屏蔽 NMI/HardFault，也不会停止定时器计数。
     */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* 步骤 2：已有明确故障时保留原因，否则记录为通用软件异常。 */
    const MotorFault reason = (motor.state == MOTOR_FAULT) ?
        motor.fault : MOTOR_FAULT_SOFTWARE;
    FaultOff(reason);

    /* 步骤 3：恢复原屏蔽状态；不能一律开中断，否则会破坏调用者的临界区。 */
    __set_PRIMASK(primask);
}

/*
 * 主动停止，同时确认/清除软件故障，给下一次 g 做准备。
 * 函数返回时驱动已经关闭，但转子可能仍因惯性旋转。
 */
void MotorCommutation_Stop(void)
{
    /* 步骤 1：屏蔽中断，避免霍尔换相与停机过程交错。 */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* 步骤 2：撤销运行资格，清除故障和当前占空比，丢弃旧换相状态。 */
    motor.state = MOTOR_STOPPED;
    motor.fault = MOTOR_FAULT_NONE;
    motor.dutyPermille = 0U;
    motor.lastHall = 0xFFU;

    /* 步骤 3：关闭 PA3 和六路输出；已初始化时继续清零 CCR 预装载值。 */
    HardwareOff();
    if (motor.initialized != 0U)
    {
        SignalsOff();
    }

    /* 步骤 4：恢复进入前的中断状态，后续霍尔中断仍能更新监测数据。 */
    __set_PRIMASK(primask);
}

/*
 * 一次性初始化：main 中在 MotorIO_Init 后调用。
 * 结束时定时器可以继续计数，但 MOE=0、PA3=0、下桥为低，电机保持禁能。
 */
void MotorCommutation_Init(void)
{
    uint32_t before;

    /* 步骤 1：先标记为未初始化并关闭功率级，SysTick 此时会直接返回。 */
    motor.initialized = 0U;
    MotorCommutation_Stop();

    /*
     * 步骤 2：启用 Cortex-M7 的 DWT 周期计数器。
     * DEMCR.TRCENA 开启相关跟踪功能；LAR 写入解锁值；
     * CYCCNT 清零，CYCCNTENA 允许计数。这是后面微秒等待的时间基准。
     */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->LAR = 0xC5ACCE55UL;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /*
     * 步骤 3：计算每微秒周期数，并确认计数器实际在前进。
     * 这里只检查计时基础可用，不是对整板门极延迟进行测量。
     */
    motor.cyclesPerUs = SystemCoreClock / 1000000U;
    before = DWT->CYCCNT;
    for (volatile uint32_t i = 0U; i < 32U; ++i)
    {
        __NOP();
    }

    if (motor.cyclesPerUs == 0U ||
        (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0U ||
        DWT->CYCCNT == before)
    {
        FaultOff(MOTOR_FAULT_TIMING);
        Error_Handler();
    }

    /*
     * 步骤 4：清零三个 CCR，并触发 UG 更新事件，装载零占空比。
     * 清除更新标志，避免这次软件更新留下旧标志。
     */
    SignalsOff();
    TIM8->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim8, TIM_FLAG_UPDATE);

    /*
     * 步骤 5：三个 PWM 通道只在初始化时启动一次，此时 CCR 都为 0。
     * HAL_TIM_PWM_Start 会使能对应通道、MOE 和计数，随后还会统一关闭输出。
     * 正常 g/x 不重复调用这个 HAL 函数，避免通道状态 BUSY 导致启动失败。
     */
    if (HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_1) != HAL_OK ||
        HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_2) != HAL_OK ||
        HAL_TIM_PWM_Start(&htim8, TIM_CHANNEL_3) != HAL_OK)
    {
        HardwareOff();
        Error_Handler();
    }

    /* 步骤 6：允许后续接口工作，再执行一次统一停机，确保复位后不自动启动。 */
    motor.initialized = 1U;
    MotorCommutation_Stop();
}

/*
 * g 正转与 r 反转共用启动流程，依次关断、检查霍尔、预充电并建立初始换相。
 * 返回 1 只表示建立了运行状态；真正是否转动仍由霍尔反馈反映。
 * 该函数含毫秒等待，仅从主循环调用。
 */
uint8_t MotorCommutation_Start(uint16_t targetPermille,
                               MotorDirection direction)
{
    uint32_t primask;
    uint8_t hall;

    /* 步骤 1：检查是否初始化成功且处于停止状态；故障锁定时必须先 x。 */
    primask = __get_PRIMASK();
    __disable_irq();

    if (motor.initialized == 0U || motor.state != MOTOR_STOPPED)
    {
        __set_PRIMASK(primask);
        return 0U;
    }

    /* 步骤 2：限制这份调试示例的占空比范围与转向参数，异常参数不使能电机。 */
    if (targetPermille < DUTY_MIN_PERMILLE ||
        targetPermille > DUTY_MAX_PERMILLE ||
        (direction != MOTOR_DIR_FORWARD && direction != MOTOR_DIR_REVERSE))
    {
        FaultOff(MOTOR_FAULT_PARAMETER);
        __set_PRIMASK(primask);
        return 0U;
    }

    /* 步骤 3：统一关闭输出，进入 STARTING，并开始记录启动阶段超时。 */
    HardwareOff();
    SignalsOff();
    motor.state = MOTOR_STARTING;
    motor.startupTick = HAL_GetTick();

    __set_PRIMASK(primask);

    /*
     * 步骤 4：功率级关闭时读取霍尔，开中断等待 5 ms，再检查一次。
     * 这是“两次读取一致”检查，不是连续采样或完整的去抖算法。
     * HAL_Delay 依赖 SysTick，所以整个 Start 必须在主循环且中断开启时调用。
     */
    hall = MotorHall_Read();
    HAL_Delay(HALL_STABLE_MS);

    primask = __get_PRIMASK();
    __disable_irq();

    if (motor.state != MOTOR_STARTING)
    {
        __set_PRIMASK(primask);
        return 0U;
    }

    /* 步骤 5：两次霍尔必须同为有效码且相同，否则关闭并记录霍尔故障。 */
    if (!ValidHall(hall) || MotorHall_Read() != hall)
    {
        FaultOff(MOTOR_FAULT_HALL);
        __set_PRIMASK(primask);
        return 0U;
    }

    /*
     * 步骤 6：自举预充电，应在电机静止时进行。
     * MOE=0 让三路上桥保持关闭；PA3 拉高允许驱动器工作；
     * 三路下桥同时拉高，使各相下桥提供自举充电所需的低侧路径。
     * 这里全部导通的是三个下桥，上桥仍关闭。
     */
    HAL_GPIO_WritePin(MOTOR_ENABLE_PORT, MOTOR_ENABLE_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_UL_PORT, MOTOR_UL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(MOTOR_VL_PORT,
                      MOTOR_VL_PIN | MOTOR_WL_PIN, GPIO_PIN_SET);

    /*
     * 步骤 7：恢复中断，等待约 2 ms 完成预充电。
     * 此时状态仍为 STARTING，霍尔回调只监测，不会进入运行换相。
     */
    __set_PRIMASK(primask);
    HAL_Delay(BOOTSTRAP_PRECHARGE_MS);

    /* 步骤 8：重新进入临界区，检查等待期间是否发生了启动故障关断。 */
    primask = __get_PRIMASK();
    __disable_irq();

    if (motor.state != MOTOR_STARTING)
    {
        __set_PRIMASK(primask);
        return 0U;
    }

    /* 步骤 9：撤销预充电的下桥导通，六路全关并等待，才能转入正常换相。 */
    SignalsOff();
    DelayUs(COMMUTATION_BLANK_US);

    /* 步骤 10：使用预充电结束后的实际位置，不能一直沿用启动前的旧霍尔。 */
    hall = MotorHall_Read();
    if (!ValidHall(hall))
    {
        FaultOff(MOTOR_FAULT_HALL);
        __set_PRIMASK(primask);
        return 0U;
    }

    /*
     * 步骤 11：直接使用命令指定的占空比。main.c 传入 80，表示 8%。
     * motor.lastHall=0xFF 让下一步立即应用当前霍尔状态。
     * 记录当前时间，作为开始等待霍尔变化的计时起点。
     */
    motor.direction = direction;
    motor.dutyPermille = targetPermille;
    motor.lastHall = 0xFFU;
    motor.lastEdgeTick = HAL_GetTick();
    motor.state = MOTOR_RUNNING;

    /* 步骤 12：立即根据当前位置建立第一个换相状态。函数内部会再次检查状态。 */
    MotorCommutation_ApplyHall(hall);

    /* 步骤 13：返回启动结果，并恢复最外层进入 Start 前的中断屏蔽状态。 */
    const uint8_t started = (uint8_t)(motor.state == MOTOR_RUNNING);
    __set_PRIMASK(primask);
    return started;
}

/*
 * 串口 + 命令调用此函数。运行中提高占空比，返回结果的单位为千分比。
 * 返回 0 表示当前没有可以调整的运行输出。
 */
uint16_t MotorCommutation_IncreaseDuty(void)
{
    uint8_t upper;
    uint8_t lower;
    uint16_t nextDuty;
    const uint32_t primask = __get_PRIMASK();

    /* 步骤 1：短暂屏蔽中断，避免查相位和更新 CCR 之间发生换相。 */
    __disable_irq();

    if (motor.state != MOTOR_RUNNING || !ValidHall(motor.lastHall))
    {
        __set_PRIMASK(primask);
        return 0U;
    }

    /* 步骤 2：增加 5 个百分点。最后一步超过上限时取 50%。 */
    nextDuty = (uint16_t)(motor.dutyPermille + DUTY_STEP_PERMILLE);
    if (nextDuty > DUTY_MAX_PERMILLE)
    {
        nextDuty = DUTY_MAX_PERMILLE;
    }

    /* 步骤 3：按当前方向和霍尔码找到正在输出 PWM 的上桥。 */
    if (nextDuty != motor.dutyPermille)
    {
        GetPhases(motor.lastHall, &upper, &lower);
        motor.dutyPermille = nextDuty;

        /* CCR 预装载值在定时器更新时生效。后续换相继续使用 motor.dutyPermille。 */
        SetUpperDuty(upper, motor.dutyPermille);
    }

    /* 步骤 4：恢复调用前的中断状态，并返回当前占空比。 */
    __set_PRIMASK(primask);
    return nextDuty;
}

/*
 * 霍尔驱动的六步换相核心，主要由 HAL_GPIO_EXTI_Callback 调用。
 * 每次只允许一个上桥输出 PWM，另一个相的下桥保持高，其余全部关闭。
 * 首次启动也会直接调用一次，避免电机静止时等不到第一条霍尔边沿。
 */
void MotorCommutation_ApplyHall(uint8_t hall)
{
    uint8_t upper;
    uint8_t lower;
    /* 步骤 1：整个“检查 -> 全关 -> 等待 -> 建立新输出”作为一个临界区完成。 */
    const uint32_t primask = __get_PRIMASK();

    __disable_irq();

    /* 步骤 2：只有 RUNNING 才能打开信号。停机、预充电和故障中都直接返回。 */
    if (motor.initialized == 0U || motor.state != MOTOR_RUNNING)
    {
        __set_PRIMASK(primask);
        return;
    }

    /* 步骤 3：运行时读到 0/7 等无效值立即故障关断，并保持锁定。 */
    if (!ValidHall(hall))
    {
        FaultOff(MOTOR_FAULT_HALL);
        __set_PRIMASK(primask);
        return;
    }

    /* 步骤 4：重复的相同状态不重新触发 UG，也不刷新最后有效边沿时间。 */
    if (hall == motor.lastHall)
    {
        __set_PRIMASK(primask);
        return;
    }

    /*
     * 步骤 5：已有上一状态时，检查这次是否只有一个霍尔位改变。
     * XOR 得到变化位；x & (x-1) 非零表示不止一位变化。
     * 这可发现部分丢边沿/干扰问题，但不代替转向或机械位置的校验。
     */
    if (motor.lastHall != 0xFFU)
    {
        const uint8_t changed = hall ^ motor.lastHall;
        if ((changed & (uint8_t)(changed - 1U)) != 0U)
        {
            FaultOff(MOTOR_FAULT_HALL_JUMP);
            __set_PRIMASK(primask);
            return;
        }
    }

    /* 步骤 6：查表得到本次要用的上桥相、下桥相。 */
    GetPhases(hall, &upper, &lower);

    /* 步骤 7：撤销旧输出，再等待至少配置的全关时间。PA3 始终保持使能。 */
    SignalsOff();
    DelayUs(COMMUTATION_BLANK_US);

    /*
     * 步骤 8：只给选定上桥写入非零 CCR，其余通道已在 SignalsOff 中清零。
     * UG 把预装载值立即装入定时器，同时重新开始 PWM 计数相位。
     * 此时 MOE 仍为 0，装载过程中不会把旧状态输出出去。
     */
    SetUpperDuty(upper, motor.dutyPermille);
    TIM8->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim8, TIM_FLAG_UPDATE);

    /* 步骤 9：打开选定的一个下桥，再恢复上桥 MOE 总输出，形成新状态。 */
    LowerOn(lower);
    __HAL_TIM_MOE_ENABLE(&htim8);

    /* 步骤 10：只在成功建立新有效状态后，记录状态和时间，供去重与故障检查使用。 */
    motor.lastHall = hall;
    motor.lastEdgeTick = HAL_GetTick();

    __set_PRIMASK(primask);
}

/*
 * SysTick 每毫秒调用一次，检查启动阶段和运行中的霍尔反馈是否超时。
 * 不打印串口，不执行毫秒等待，让周期处理保持简短。
 */
void MotorCommutation_Tick1ms(void)
{
    uint32_t now;
    uint32_t primask;

    /* 步骤 1：SysTick 比本模块更早启动，未初始化时不访问控制流程。 */
    if (motor.initialized == 0U)
    {
        return;
    }

    /* 步骤 2：屏蔽中断后读取时间和运行状态，避免检查时霍尔回调更新计时起点。 */
    primask = __get_PRIMASK();
    __disable_irq();
    now = HAL_GetTick();

    /* 步骤 3：启动检查/预充电正常只需数毫秒，异常超过 100 ms 就关闭。 */
    if (motor.state == MOTOR_STARTING &&
        (uint32_t)(now - motor.startupTick) >= STARTUP_TIMEOUT_MS)
    {
        FaultOff(MOTOR_FAULT_TIMING);
    }

    /*
     * 步骤 4：仅在运行阶段检查最后一次有效霍尔变化距离现在是否已达 500 ms。
     * 堵转、霍尔掉线或驱动板保护都可能使霍尔反馈停止变化。
     */
    else if (motor.state == MOTOR_RUNNING &&
             (uint32_t)(now - motor.lastEdgeTick) >= STALL_TIMEOUT_MS)
    {
        FaultOff(MOTOR_FAULT_STALL);
    }

    /* 步骤 5：恢复原中断屏蔽状态。 */
    __set_PRIMASK(primask);
}

/*
 * 主循环查询接口：只复制数据，打印由调用者完成。
 * status 必须指向调用者提供的有效 MotorStatus 变量。
 */
void MotorCommutation_GetStatus(MotorStatus *status)
{
    /* 步骤 1：短暂屏蔽中断，避免复制到一半状态恰好被故障/换相中断改变。 */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();

    /* 步骤 2：复制阶段、原因、占空比，并读取当前霍尔；未把内部 0xFF 哨兵当作霍尔值输出。 */
    status->state = motor.state;
    status->fault = motor.fault;
    status->dutyPermille = motor.dutyPermille;
    status->hall = MotorHall_Read();

    /* 步骤 3：恢复原屏蔽状态，主循环此后可安全地打印这份局部快照。 */
    __set_PRIMASK(primask);
}
