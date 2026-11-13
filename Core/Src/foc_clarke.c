#include "foc_clarke.h"

FocAlphaBetaCurrents FocClarke_Transform(float uAmps,
                                       float vAmps,
                                       float wAmps)
{
    FocAlphaBetaCurrents currents;

    /* α 轴沿 U 相。三相电流和为零时，该式等于 uAmps。 */
    currents.alphaAmps =
        (2.0f * uAmps - vAmps - wAmps) * FOC_CLARKE_ONE_THIRD;

    /* β 轴由 V、W 两相之差决定。V 大于 W 时，β 为正。 */
    currents.betaAmps = (vAmps - wAmps) * FOC_CLARKE_INV_SQRT_THREE;

    return currents;
}
