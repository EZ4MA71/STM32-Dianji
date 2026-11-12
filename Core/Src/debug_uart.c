#include "debug_uart.h"
#include "main.h"

#define RX_BUFFER_SIZE 512U

UART_HandleTypeDef huart1;
volatile uint32_t debugUartRxDropped;
volatile uint32_t debugUartRxErrors;

static uint8_t rxByte;
static uint8_t rxBuffer[RX_BUFFER_SIZE];
static volatile uint32_t rxHead;
static volatile uint32_t rxTail;

void DebugUART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = DEBUG_UART_BAUDRATE;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    if (HAL_UART_Init(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_UARTEx_DisableFifoMode(&huart1) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_UART_Receive_IT(&huart1, &rxByte, 1U) != HAL_OK)
    {
        Error_Handler();
    }
}

HAL_StatusTypeDef DebugUART_Write(const uint8_t *data, uint16_t size)
{
    /* Foreground use only; do not printf from interrupt handlers. */
    return HAL_UART_Transmit(&huart1, data, size, 1000U);
}

int DebugUART_ReadByte(uint8_t *byte)
{
    const uint32_t tail = rxTail;
    if (tail == rxHead)
    {
        return 0;
    }

    __DMB();
    *byte = rxBuffer[tail];
    __DMB();
    rxTail = (tail + 1U) % RX_BUFFER_SIZE;
    return 1;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == USART1)
    {
        const uint32_t head = rxHead;
        const uint32_t next = (head + 1U) % RX_BUFFER_SIZE;
        if (next != rxTail)
        {
            rxBuffer[head] = rxByte;
            __DMB();
            rxHead = next;
        }
        else
        {
            ++debugUartRxDropped;
        }

        if (HAL_UART_Receive_IT(uart, &rxByte, 1U) != HAL_OK)
        {
            Error_Handler();
        }
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == USART1)
    {
        ++debugUartRxErrors;
        /* Overrun ends reception; framing/noise errors may leave it running. */
        if (uart->RxState == HAL_UART_STATE_READY)
        {
            if (HAL_UART_Receive_IT(uart, &rxByte, 1U) != HAL_OK)
            {
                Error_Handler();
            }
        }
    }
}
