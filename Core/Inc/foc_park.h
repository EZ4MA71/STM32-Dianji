#ifndef FOC_PARK_H
#define FOC_PARK_H

#include "foc_clarke.h"

typedef struct
{
    float dAmps;  /* 沿转子磁链方向的电流，单位 A。 */
    float qAmps;  /* 沿 d 轴正向超前 90° 的方向，单位 A。 */
} FocDqCurrents;

/* electricalAngleRad 是从 α 轴到转子 d 轴的电角度，单位为弧度。 */
FocDqCurrents FocPark_Transform(FocAlphaBetaCurrents currents,
                                float electricalAngleRad);

#endif
