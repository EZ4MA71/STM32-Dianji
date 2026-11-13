#include "main.h"
#include "debug_uart.h"
#include "foc_pwm.h"
#include "foc_adc.h"
#include "foc_encoder.h"
#include <stdio.h>

#define LED1_GPIO_PORT GPIOE
#define LED1_GPIO_PIN GPIO_PIN_9
#define LED1_TOGGLE_INTERVAL_MS 500U

static void SystemClock_Config(void);
static void LED1_Init(void);

int main(void)
{
    uint32_t lastHeartbeat;
    uint32_t lastLedToggle;
    uint8_t received;

    /* 初始化 HAL、400 MHz 系统时钟、板载 LED 和调试串口。 */
    HAL_Init();
    SystemClock_Config();
    /* 先配置 TIM8 并关断功率级，再让 ADC 就绪，最后启动计数和采样触发。 */
    FocPwm_Init();
    FocAdc_Init();
    FocPwm_StartCounter();
    FocEncoder_Init();
    LED1_Init();
    DebugUART_Init();

    printf("\r\nSTM32H743VIT6 FOC UART ready\r\n");
    printf("SYSCLK=%lu Hz, HCLK=%lu Hz, PCLK2=%lu Hz\r\n",
           (unsigned long)HAL_RCC_GetSysClockFreq(),
           (unsigned long)HAL_RCC_GetHCLKFreq(),
           (unsigned long)HAL_RCC_GetPCLK2Freq());
    printf("USART1: PA9=TX, PA10=RX, 115200 8N1\r\n");
    printf("Type text to echo. Motor drive disabled.\r\n");
    lastHeartbeat = HAL_GetTick();
    lastLedToggle = lastHeartbeat;

    while (1)
    {
        /* USART1 中断接收，主循环取出字节并回显。 */
        if (DebugUART_ReadByte(&received))
        {
            if (DebugUART_Write(&received, 1U) != HAL_OK)
            {
                Error_Handler();
            }
        }

        /* 每 500 ms 翻转一次 LED，避免阻塞串口处理。 */
        if ((uint32_t)(HAL_GetTick() - lastLedToggle) >= LED1_TOGGLE_INTERVAL_MS)
        {
            lastLedToggle = HAL_GetTick();
            HAL_GPIO_TogglePin(LED1_GPIO_PORT, LED1_GPIO_PIN);
        }

        /* 每秒输出心跳和接收错误计数。 */
        if ((uint32_t)(HAL_GetTick() - lastHeartbeat) >= 1000U)
        {
            lastHeartbeat = HAL_GetTick();
            printf("\r\n[alive] tick=%lu ms, rx_errors=%lu, rx_dropped=%lu\r\n",
                   (unsigned long)lastHeartbeat,
                   (unsigned long)debugUartRxErrors,
                   (unsigned long)debugUartRxDropped);
        }
    }
}

static void LED1_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOE_CLK_ENABLE();
    /* LED1 is active-low. Preload the output high so it starts off. */
    HAL_GPIO_WritePin(LED1_GPIO_PORT, LED1_GPIO_PIN, GPIO_PIN_SET);
    gpio.Pin = LED1_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED1_GPIO_PORT, &gpio);
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clocks = {0};

    if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK)
    {
        Error_Handler();
    }
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY) == 0U)
    {
    }

    /* Board X2: 25 MHz crystal. 25 / 5 * 160 / 2 = 400 MHz. */
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
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_4) != HAL_OK)
    {
        Error_Handler();
    }
}

void Error_Handler(void)
{
    /* Set a debugger breakpoint here if clock or UART initialization fails. */
    __disable_irq();
    while (1)
    {
    }
}
