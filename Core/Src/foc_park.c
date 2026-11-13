#include "foc_park.h"
#include <math.h>

FocDqCurrents FocPark_Transform(FocAlphaBetaCurrents currents,
                                float electricalAngleRad)
{
    const float cosine = cosf(electricalAngleRad);
    const float sine = sinf(electricalAngleRad);
    FocDqCurrents result;

    /* d 轴在 αβ 平面上的单位向量为 (cosθ, sinθ)。 */
    result.dAmps = currents.alphaAmps * cosine + currents.betaAmps * sine;

    /* q 轴由 d 轴沿正方向转过 90°，单位向量为 (-sinθ, cosθ)。 */
    result.qAmps = -currents.alphaAmps * sine + currents.betaAmps * cosine;

    return result;
}
