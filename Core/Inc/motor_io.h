#ifndef MOTOR_IO_H
#define MOTOR_IO_H

#include "stm32h7xx_hal.h"

/* 上桥臂：TIM8 CH1、CH2、CH3 */
#define MOTOR_PWM_PORT       GPIOC
#define MOTOR_UH_PIN         GPIO_PIN_6
#define MOTOR_VH_PIN         GPIO_PIN_7
#define MOTOR_WH_PIN         GPIO_PIN_8

/* 下桥臂：按 H_PWM-L_ON 方式使用普通 GPIO */
#define MOTOR_UL_PORT        GPIOA
#define MOTOR_UL_PIN         GPIO_PIN_5

#define MOTOR_VL_PORT        GPIOB
#define MOTOR_VL_PIN         GPIO_PIN_14

#define MOTOR_WL_PORT        GPIOB
#define MOTOR_WL_PIN         GPIO_PIN_15

/* 驱动使能：低电平关闭，高电平使能 */
#define MOTOR_ENABLE_PORT    GPIOA
#define MOTOR_ENABLE_PIN     GPIO_PIN_3

/* 霍尔输入 */
#define MOTOR_HALL_PORT      GPIOD
#define MOTOR_HALL_U_PIN     GPIO_PIN_14
#define MOTOR_HALL_V_PIN     GPIO_PIN_15
#define MOTOR_HALL_W_PIN     GPIO_PIN_7

/*
 * TIM8 = 200 MHz
 * PSC  = 19
 * ARR  = 999
 *
 * PWM = 200000000 / 20 / 1000 = 10000 Hz
 */
#define MOTOR_PWM_PERIOD     999U

extern TIM_HandleTypeDef htim8;

/* 初始化控制引脚、PWM 定时器及霍尔中断 */
void MotorIO_Init(void);

/* 读取当前霍尔码：bit2=W，bit1=V，bit0=U */
uint8_t MotorHall_Read(void);

/* 取得霍尔快照；返回 1 表示有待处理的更新 */
uint8_t MotorHall_TakeSnapshot(uint8_t *state, uint32_t *irqCount);

#endif
