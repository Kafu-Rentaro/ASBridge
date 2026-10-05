// SPDX-License-Identifier: BSD-3-Clause
#include <string.h>
#include "asbridge/decode.h"

bool as_decode(uint32_t i, ASDecoded *d) {
    memset(d, 0, sizeof(*d));

    if (i == 0xD503201Fu) { d->op = AS_OP_NOP; return true; }
    if ((i & 0xFFE0001Fu) == 0xD4200000u) {
        d->op = AS_OP_BRK;
        d->imm16 = (uint16_t)((i >> 5) & 0xFFFFu);
        return true;
    }

    /* Move wide (64-bit): MOVZ/MOVK. */
    if ((i & 0xFF800000u) == 0xD2800000u) {
        d->op = AS_OP_MOVZ;
        d->rd = (uint8_t)(i & 31u);
        d->imm16 = (uint16_t)((i >> 5) & 0xFFFFu);
        d->shift = (uint8_t)(((i >> 21) & 3u) * 16u);
        return true;
    }
    if ((i & 0xFF800000u) == 0xF2800000u) {
        d->op = AS_OP_MOVK;
        d->rd = (uint8_t)(i & 31u);
        d->imm16 = (uint16_t)((i >> 5) & 0xFFFFu);
        d->shift = (uint8_t)(((i >> 21) & 3u) * 16u);
        return true;
    }

    /* ADD/SUB immediate, 64-bit, flags not set. */
    if ((i & 0xFF000000u) == 0x91000000u ||
        (i & 0xFF000000u) == 0xD1000000u) {
        d->op = ((i & 0x40000000u) != 0u) ? AS_OP_SUB_IMM : AS_OP_ADD_IMM;
        d->rd = (uint8_t)(i & 31u);
        d->rn = (uint8_t)((i >> 5) & 31u);
        d->imm12 = (uint16_t)((i >> 10) & 0xFFFu);
        d->shift = (uint8_t)(((i >> 22) & 1u) ? 12u : 0u);
        return true;
    }

    d->op = AS_OP_INVALID;
    return false;
}
