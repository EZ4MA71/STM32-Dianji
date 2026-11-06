#include "debug_uart.h"
#include <stdio.h>

/* Keil MicroLIB printf output is routed to USART1, without semihosting. */
int fputc(int character, FILE *stream)
{
    const uint8_t byte = (uint8_t)character;
    (void)stream;
    return (DebugUART_Write(&byte, 1U) == HAL_OK) ? character : EOF;
}

int ferror(FILE *stream)
{
    (void)stream;
    return 0;
}
