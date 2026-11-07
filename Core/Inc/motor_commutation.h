#ifndef MOTOR_COMMUTATION_H
#define MOTOR_COMMUTATION_H

#include "stm32h7xx_hal.h"

/*
 * 本文件是电机控制模块的“接口说明”。
 * main.c 通过这些接口启动/停止；霍尔中断通过 ApplyHall 完成换相；
 * SysTick 通过 Tick1ms 检查启动和霍尔反馈的故障超时。
 */

/* g 命令使用正转，r 命令使用反转。 */
typedef enum
{
    MOTOR_DIR_FORWARD = 0, /* 使用已经核对过的正转换相表 */
    MOTOR_DIR_REVERSE = 1  /* 按所给反转表交换上桥相与下桥相 */
} MotorDirection;

/* 当前所处阶段；串口打印的 state 就是下面的数值。 */
typedef enum
{
    MOTOR_STOPPED = 0,  /* 已关闭驱动，可以接受新的启动命令 */
    MOTOR_STARTING = 1, /* 正在检查霍尔或进行自举预充电 */
    MOTOR_RUNNING = 2, /* 已使能驱动，按照霍尔反馈换相 */
    MOTOR_FAULT = 3    /* 故障锁定；处理原因后先 x 确认，再 g 启动 */
} MotorState;

/*
 * 故障原因；串口打印的 fault 就是下面的数值。
 * state 表示“现在在哪个阶段”，fault 表示“为什么进入故障”。
 */
typedef enum
{
    MOTOR_FAULT_NONE = 0,      /* 没有软件故障 */
    MOTOR_FAULT_HALL = 1,      /* 霍尔码无效，或启动前两次采样不一致 */
    MOTOR_FAULT_HALL_JUMP = 2, /* 一次变化中有多个霍尔位同时改变 */
    MOTOR_FAULT_STALL = 3,     /* 运行时连续 500 ms 未观察到有效霍尔变化 */
    MOTOR_FAULT_PARAMETER = 4, /* 占空比或转向参数不符合本示例限制 */
    MOTOR_FAULT_TIMING = 5,    /* 微秒时基检查失败，或启动阶段超时 */
    MOTOR_FAULT_SOFTWARE = 6   /* 通用错误处理或 CPU 异常触发关断 */
} MotorFault;

/* GetStatus 填写这个结构体，主循环再把快照打印出来。 */
typedef struct
{
    MotorState state;        /* 当前运行阶段 */
    MotorFault fault;        /* 最近一次尚未确认的故障 */
    uint16_t dutyPermille;  /* 当前占空比，单位为千分之一；80 表示 8% */
    uint8_t hall;            /* 当前三路输入组成的霍尔码：W4 + V2 + U1 */
} MotorStatus;

/*
 * 步骤 1：MotorIO_Init() 后调用一次。
 * 配置微秒时基，启动三个 PWM 通道的计数/通道功能，但最终保持
 * MOE=0、PA3=0，所以不会在初始化结束后直接驱动电机。
 */
void MotorCommutation_Init(void);

/*
 * 步骤 2：仅在主循环、中断已开启时调用。
 * targetPermille 是预充电结束后直接使用的占空比，本示例接受 30~500。
 * 返回 1 表示启动流程已建立输出；不代表电机已经达到某个转速。
 * 返回 0 表示本次没有启动成功，使用 GetStatus 查询原因。
 * 内部包含 HAL_Delay；不要放在霍尔等中断回调中调用。
 */
uint8_t MotorCommutation_Start(uint16_t targetPermille,
                               MotorDirection direction);

/*
 * 串口 + 调用。运行时每次增加 50/1000，最多到 500/1000。
 * 返回当前占空比，单位为千分比。返回 0 表示当前没有运行输出。
 */
uint16_t MotorCommutation_IncreaseDuty(void);

/*
 * 步骤 3：串口 x 调用。
 * 关闭驱动和六路控制信号，并确认/清除软件故障。
 * 电机随后自由滑行；本接口不执行主动刹车。
 */
void MotorCommutation_Stop(void);

/*
 * 霍尔回调调用：读取霍尔后，立即根据该状态完成下一次换相。
 * 已停止、正在预充电或处于故障状态时，不会打开输出。
 */
void MotorCommutation_ApplyHall(uint8_t hall);

/*
 * SysTick_Handler 中先 HAL_IncTick()，再调用本函数。
 * 检查启动异常超时，以及运行中无有效霍尔变化的故障超时。
 * 不包含固定运行 1 秒后停止的逻辑。
 */
void MotorCommutation_Tick1ms(void);

/* 将状态复制到调用者提供的结构体；复制时短暂屏蔽中断以保持一致。 */
void MotorCommutation_GetStatus(MotorStatus *status);

/*
 * Error_Handler 和 CPU 异常入口调用，尽力立即关闭功率级。
 * 保留故障状态，不自动重启；这个函数不负责等待电机转轴停止。
 */
void MotorCommutation_EmergencyStop(void);

#endif
