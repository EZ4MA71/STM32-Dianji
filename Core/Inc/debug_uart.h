#ifndef DEBUG_UART_H
#define DEBUG_UART_H

#include "stm32h7xx_hal.h"
#include <stddef.h>

#define DEBUG_UART_BAUDRATE 115200U

extern UART_HandleTypeDef huart1;
extern volatile uint32_t debug_uart_rx_dropped;
extern volatile uint32_t debug_uart_rx_errors;

void DebugUART_Init(void);
HAL_StatusTypeDef DebugUART_Write(const uint8_t *data, uint16_t size);
int DebugUART_ReadByte(uint8_t *byte);

#endif
