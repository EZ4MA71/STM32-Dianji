#ifndef FOC_ENCODER_H
#define FOC_ENCODER_H

#include <stdint.h>

typedef struct
{
    uint32_t count;          /* TIM2 当前的原始位置计数。 */
    uint32_t indexCount;     /* 收到的 Z 上升沿次数。 */
    uint32_t indexPosition;  /* 最近一次 Z 上升沿对应的 TIM2 计数。 */
    int32_t indexDelta;      /* 最近两次 Z 上升沿之间的有符号计数差。 */
} FocEncoderSnapshot;

/* 系统时钟配置完成后调用一次。 */
void FocEncoder_Init(void);

/* 取得一组位置和索引快照。 */
void FocEncoder_Read(FocEncoderSnapshot *snapshot);

#endif