/*
 * can_dlen.c — lunghezze di frame ammesse dal CAN FD.
 *
 * Logica pura, senza dipendenze dal silicio: sta qui e non nel driver perché
 * serve anche a src/isotp.c e ai test su host.
 *
 * Fino a 8 byte ogni lunghezza è rappresentabile; sopra, il CAN FD ammette
 * solo 12, 16, 20, 24, 32, 48 e 64. Un payload di 9 byte viaggia quindi in un
 * frame da 12, con tre byte di riempimento.
 */

#include "can.h"

uint8_t sbl_can_round_dlen(uint8_t len)
{
    static const uint8_t steps[] = { 12u, 16u, 20u, 24u, 32u, 48u, 64u };

    if (len <= 8u) {
        return len;
    }
    for (size_t i = 0u; i < (sizeof(steps) / sizeof(steps[0])); i++) {
        if (len <= steps[i]) {
            return steps[i];
        }
    }
    return SBL_CAN_MAX_DLEN;
}
