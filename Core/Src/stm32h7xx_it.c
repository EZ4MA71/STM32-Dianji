#include "debug_uart.h"
#include "motor_commutation.h"
#include "motor_io.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
    /* 步骤 2：使用更新后的时间，检查启动和霍尔反馈是否超时。 */
    MotorCommutation_Tick1ms();
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart1);
}

void EXTI15_10_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(MOTOR_HALL_U_PIN);
    HAL_GPIO_EXTI_IRQHandler(MOTOR_HALL_V_PIN);
}

void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(MOTOR_HALL_W_PIN);
}