#ifndef FOC_CLARKE_H
#define FOC_CLARKE_H

/* 幅值不变 Clarke 变换的系数。 */
#define FOC_CLARKE_ONE_THIRD       (1.0f / 3.0f)
#define FOC_CLARKE_INV_SQRT_THREE  0.57735026919f

typedef struct
{
    float alphaAmps;  /* 静止坐标系 α 轴电流，单位 A。 */
    float betaAmps;   /* 静止坐标系 β 轴电流，单位 A。 */
} FocAlphaBetaCurrents;

/* 输入为同一帧的 U、V、W 相电流，单位 A。α 轴沿 U 相。 */
FocAlphaBetaCurrents FocClarke_Transform(float uAmps,
                                       float vAmps,
                                       float wAmps);

#endif
