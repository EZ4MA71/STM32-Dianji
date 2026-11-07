/*
 * 程序入口与串口命令处理。
 * 主流程：HAL/时钟 -> 电机接口 -> 换相模块禁能初始化 -> LED/串口 ->
 * 等待命令 g/r/+/x/?。主循环不按固定时间切换相序，换相来自霍尔中断。
 */
#include "main.h"
#include "debug_uart.h"
#include "motor_io.h"
#include "motor_commutation.h"
#include <stdio.h>

/* LED1 接 PE9，低电平点亮；每 500 ms 翻转一次，完整闪烁周期约 1 s。 */
#define LED1_GPIO_PORT GPIOE
#define LED1_GPIO_PIN GPIO_PIN_9
#define LED1_TOGGLE_INTERVAL_MS 500U

/* 保存主循环两次调用之间需要记住的监测状态。 */
typedef struct
{
    uint32_t lastHallLogTick;
    MotorState reportedState;
    MotorFault reportedFault;
} MotorAppContext;

static MotorAppContext motorApp =
{
    .reportedState = MOTOR_STOPPED,
    .reportedFault = MOTOR_FAULT_NONE
};

static void SystemClock_Config(void);
static void LED1_Init(void);
static void motorAppInit(void);
static void motorAppInitConsole(void);
static void motorAppProcess(void);

int main(void)
{
    uint32_t lastLedToggle;

    /* 初始化 HAL 和 400 MHz 时钟，再配置电机接口。 */
    HAL_Init();
    SystemClock_Config();
    motorAppInit();

    LED1_Init();
    motorAppInitConsole();
    lastLedToggle = HAL_GetTick();

    while (1)
    {
        /* 每轮处理串口命令并打印电机状态。 */
        motorAppProcess();

        /* 到达间隔后翻转 LED，主循环继续运行。 */
        if ((uint32_t)(HAL_GetTick() - lastLedToggle) >=
            LED1_TOGGLE_INTERVAL_MS)
        {
            lastLedToggle = HAL_GetTick();
            HAL_GPIO_TogglePin(LED1_GPIO_PORT, LED1_GPIO_PIN);
        }
    }
}

/* 准备 PWM 和霍尔输入，驱动板保持关断直到收到启动命令。 */
static void motorAppInit(void)
{
    MotorIO_Init();
    MotorCommutation_Init();
}

/* 开启串口、打印命令，并记录霍尔日志的起始时间。 */
static void motorAppInitConsole(void)
{
    DebugUART_Init();

    printf("\r\nSTM32H743 motor start/stop bring-up\r\n");
    printf("SYSCLK=%lu Hz, PWM=10 kHz\r\n",
           (unsigned long)HAL_RCC_GetSysClockFreq());
    printf("g starts forward. r starts reverse. Both start at 8%%.\r\n");
    printf("+ adds 5 percentage points up to 50%%. x stops. ? shows status.\r\n");
    printf("No timed run stop. No Hall edge for 500 ms -> fault.\r\n");

    motorApp.lastHallLogTick = HAL_GetTick();
}

/* 手动查询和状态变化通知共用这一种输出格式。 */
static void motorAppPrintStatus(const MotorStatus *status)
{
    printf("[motor] state=%u fault=%u duty=%u/1000 hall=%u\r\n",
           (unsigned int)status->state,
           (unsigned int)status->fault,
           (unsigned int)status->dutyPermille,
           (unsigned int)status->hall);
}

/* 解释一个串口字节。换向前先发 x，等转轴停稳后再启动。 */
static void motorAppHandleCommand(uint8_t received)
{
    /* g 选择正转，r 选择反转。两个方向的起步占空比均为 8%。 */
    if (received == 'g' || received == 'G' ||
        received == 'r' || received == 'R')
    {
        const MotorDirection direction =
            (received == 'r' || received == 'R')
                ? MOTOR_DIR_REVERSE : MOTOR_DIR_FORWARD;
        const uint8_t started = MotorCommutation_Start(80U, direction);
        printf("[start] %s\r\n", started ? "OK" : "REJECTED");
    }
    /* 运行中按 + 增加 5 个百分点，最高 50%。 */
    else if (received == '+')
    {
        const uint16_t dutyPermille = MotorCommutation_IncreaseDuty();
        if (dutyPermille != 0U)
        {
            printf("[duty] %u.%u%%\r\n",
                   (unsigned int)(dutyPermille / 10U),
                   (unsigned int)(dutyPermille % 10U));
        }
        else
        {
            printf("[duty] REJECTED. Motor must be running.\r\n");
        }
    }
    /* x 关闭驱动并清除软件故障。转子随后自由滑行。 */
    else if (received == 'x' || received == 'X')
    {
        MotorCommutation_Stop();
        printf("[stop] driver disabled; motor coasts\r\n");
    }
    /* ? 先取状态快照，再打印。 */
    else if (received == '?')
    {
        MotorStatus status;
        MotorCommutation_GetStatus(&status);
        motorAppPrintStatus(&status);
    }
    /* 忽略回车换行，回显其他字符。 */
    else if (received != '\r' && received != '\n')
    {
        if (DebugUART_Write(&received, 1U) != HAL_OK)
        {
            Error_Handler();
        }
    }
}

/* 处理当前缓冲区中的命令，再输出状态变化和霍尔监测信息。 */
static void motorAppProcess(void)
{
    uint8_t received;
    uint8_t hall;
    uint32_t hallIrqCount;
    MotorStatus status;

    /* 步骤 1：取出串口接收缓冲中的字节。 */
    while (DebugUART_ReadByte(&received))
    {
        motorAppHandleCommand(received);
    }

    /* 步骤 2：阶段或故障变化时输出一条状态。 */
    MotorCommutation_GetStatus(&status);
    if (status.state != motorApp.reportedState ||
        status.fault != motorApp.reportedFault)
    {
        motorApp.reportedState = status.state;
        motorApp.reportedFault = status.fault;
        motorAppPrintStatus(&status);
    }

    /* 步骤 3：读取霍尔快照，日志最多每 100 ms 打印一次。 */
    // if (MotorHall_TakeSnapshot(&hall, &hallIrqCount) &&
    //     (uint32_t)(HAL_GetTick() - motorApp.lastHallLogTick) >= 100U)
    // {
    //     motorApp.lastHallLogTick = HAL_GetTick();
    //     printf("[hall] U=%u V=%u W=%u code=%u irq=%lu\r\n",
    //            (unsigned int)(hall & 1U),
    //            (unsigned int)((hall >> 1U) & 1U),
    //            (unsigned int)((hall >> 2U) & 1U),
    //            (unsigned int)hall,
    //            (unsigned long)hallIrqCount);
    // }
}

/* 初始化 PE9 LED，这部分不参与电机驱动。 */
static void LED1_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 步骤 1：开启 GPIOE 时钟，之后才能配置和操作 PE9。 */
    __HAL_RCC_GPIOE_CLK_ENABLE();
    /* 步骤 2：先写高电平让低有效 LED 熄灭，再切换为输出模式，减少切换闪烁。 */
    HAL_GPIO_WritePin(LED1_GPIO_PORT, LED1_GPIO_PIN, GPIO_PIN_SET);
    /* 步骤 3：配置普通推挽输出，低速设置足以满足 LED 的慢速翻转。 */
    gpio.Pin = LED1_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED1_GPIO_PORT, &gpio);
}

/*
 * 沿用当前工程的 400 MHz 配置。
 * CPU 的 DWT 使用 SystemCoreClock，TIM8 还要经过总线和自身分频。
 */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    /* 步骤 1：选择当前板使用的 LDO 供电方式。 */
    if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK)
    {
        Error_Handler();
    }
    /* 步骤 2：选择电压性能档位，等待调节完成后再切换高系统时钟。 */
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY) == 0U)
    {
    }

    /*
     * 步骤 3：用 HSE 外部晶振驱动 PLL。
     * 当前板 X2=25 MHz；VCO=25/5*160=800 MHz；
     * SYSCLK 使用 PLL P 输出，800/2=400 MHz。
     * Q/R 为其他 PLL 输出的分频，不改变 CPU 所用的 P 输出频率。
     */
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = 5U;
    oscillator.PLL.PLLN = 160U;
    oscillator.PLL.PLLP = 2U;
    oscillator.PLL.PLLQ = 4U;
    oscillator.PLL.PLLR = 2U;
    oscillator.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    oscillator.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    oscillator.PLL.PLLFRACN = 0U;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        Error_Handler();
    }

    /*
     * 步骤 4：SYSCLK 取 PLL，HCLK=400/2=200 MHz；
     * 各 APB 分频为 2，PCLK=100 MHz。TIM8 分频在 motor_io.c 中设置。
     */
    clocks.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK
                     | RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1
                     | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.SYSCLKDivider = RCC_SYSCLK_DIV1;
    clocks.AHBCLKDivider = RCC_HCLK_DIV2;
    clocks.APB3CLKDivider = RCC_APB3_DIV2;
    clocks.APB1CLKDivider = RCC_APB1_DIV2;
    clocks.APB2CLKDivider = RCC_APB2_DIV2;
    clocks.APB4CLKDivider = RCC_APB4_DIV2;
    /* 步骤 5：配置 Flash 等待周期，并正式切换系统和总线时钟。 */
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }
}

/*
 * 通用不可继续错误入口。
 * 先关功率级再停软件，避免仅关闭中断后硬件 PWM 仍持续输出。
 */
void Error_Handler(void)
{
    /* 步骤 1：尽力撤销使能和六路信号，兼容尚未完整初始化的情况。 */
    MotorCommutation_EmergencyStop();
    /* 步骤 2：屏蔽普通中断并停留定位，不自动恢复运行。 */
    __disable_irq();
    while (1)
    {
    }
}
