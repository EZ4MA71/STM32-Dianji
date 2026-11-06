#include "main.h"

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
}

void HAL_UART_MspInit(UART_HandleTypeDef *uart)
{
    if (uart->Instance == USART1)
    {
        GPIO_InitTypeDef gpio = {0};
        RCC_PeriphCLKInitTypeDef clock = {0};

        clock.PeriphClockSelection = RCC_PERIPHCLK_USART1;
        clock.Usart16ClockSelection = RCC_USART16CLKSOURCE_D2PCLK2;
        if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
        {
            Error_Handler();
        }

        __HAL_RCC_GPIOA_CLK_ENABLE();
        __HAL_RCC_USART1_CLK_ENABLE();

        /* These are the only peripheral GPIO pins configured by this project. */
        gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_LOW;
        gpio.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOA, &gpio);

        HAL_NVIC_SetPriority(USART1_IRQn, 5U, 0U);
        HAL_NVIC_EnableIRQ(USART1_IRQn);
    }
}
