// SPDX-License-Identifier: BSD-3-Clause
#include <string.h>
#include "asbridge/decode.h"

static int64_t sign_extend(uint64_t value, unsigned bits)
{
    uint64_t sign = UINT64_C(1) << (bits - 1);
    /* All callers supply at most 26 bits, so both casts fit. */
    return (int64_t)(value & (sign - 1)) - (int64_t)(value & sign);
}

/* Public AArch64 DecodeBitMasks algorithm, restricted to logical immediates.
 * N:~imms identifies an element size; the rotated element repeats to width. */
static bool logical_immediate(uint32_t insn, unsigned width, uint64_t *mask)
{
    unsigned n = (insn >> 22) & 1u;
    unsigned imms = (insn >> 10) & 63u;
    unsigned immr = (insn >> 16) & 63u;
    unsigned pattern = (n << 6) | ((~imms) & 63u);
    unsigned len = 0;
    for (unsigned bit = 1; bit < 7; ++bit) {
        if (pattern & (1u << bit)) len = bit;
    }
    if (len == 0 || (width == 32 && n)) return false;
    unsigned levels = (1u << len) - 1u;
    unsigned s = imms & levels;
    unsigned r = immr & levels;
    unsigned element_bits = 1u << len;
    if (s == levels || element_bits > width) return false;
    uint64_t element_mask = element_bits == 64 ? UINT64_MAX :
                            (UINT64_C(1) << element_bits) - 1;
    uint64_t element = (UINT64_C(1) << (s + 1)) - 1;
    if (r != 0) {
        element = ((element >> r) | (element << (element_bits - r))) & element_mask;
    }
    *mask = 0;
    for (unsigned bit = 0; bit < width; bit += element_bits) {
        *mask |= element << bit;
    }
    return true;
}

bool as_decode_ir(uint32_t insn, ASIR *d)
{
    if (!d) return false;
    memset(d, 0, sizeof(*d));
    d->width = 64;
    if (insn == 0xD503201Fu) {
        d->op = ASIR_NOP;
        return true;
    }
    if ((insn & 0xFFE0001Fu) == 0xD4200000u) {
        d->op = ASIR_HALT;
        d->imm = (insn >> 5) & 0xffffu;
        return true;
    }
    if ((insn & 0x7F800000u) == 0x52800000u ||
        (insn & 0x7F800000u) == 0x72800000u) {
        d->op = (insn & 0x20000000u) ? ASIR_MOV_KEEP : ASIR_MOV_IMM;
        d->width = (insn >> 31) ? 64 : 32;
        d->rd = insn & 31u;
        d->shift = ((insn >> 21) & 3u) * 16u;
        if (d->width == 32 && d->shift >= 32) return false;
        d->imm = (insn >> 5) & 0xffffu;
        return true;
    }
    /* bit 23 is fixed zero; bit 22 alone selects the immediate shift. */
    if ((insn & 0x7F800000u) == 0x11000000u ||
        (insn & 0x7F800000u) == 0x51000000u ||
        (insn & 0x7F800000u) == 0x71000000u) {
        d->op = (insn & 0x20000000u) ? ASIR_SUBS_IMM :
                ((insn & 0x40000000u) ? ASIR_SUB_IMM : ASIR_ADD_IMM);
        d->width = (insn >> 31) ? 64 : 32;
        d->rd = insn & 31u;
        d->rn = (insn >> 5) & 31u;
        d->use_sp = 1; /* SUBS reads SP, but discards writes to register 31. */
        d->imm = (uint64_t)((insn >> 10) & 0xfffu) <<
                 (((insn >> 22) & 1u) ? 12u : 0u);
        return true;
    }
    if ((insn & 0x7F200000u) == 0x0B000000u ||
        (insn & 0x7F200000u) == 0x4B000000u ||
        (insn & 0x7F200000u) == 0x6B000000u) {
        /* The current ASIR shift represents LSL only. */
        if (((insn >> 22) & 3u) != 0) return false;
        d->op = (insn & 0x20000000u) ? ASIR_SUBS_REG :
                ((insn & 0x40000000u) ? ASIR_SUB_REG : ASIR_ADD_REG);
        d->width = (insn >> 31) ? 64 : 32;
        d->rd = insn & 31u;
        d->rn = (insn >> 5) & 31u;
        d->rm = (insn >> 16) & 31u;
        d->shift = (insn >> 10) & 0x3fu;
        if (d->width == 32 && d->shift >= 32) return false;
        return true;
    }
    if ((insn & 0x1F800000u) == 0x12000000u) {
        unsigned opc = (insn >> 29) & 3u;
        if (opc == 3u) return false; /* ANDS needs a flag-setting IR. */
        d->op = opc == 0 ? ASIR_AND_IMM :
                (opc == 1 ? ASIR_ORR_IMM : ASIR_EOR_IMM);
        d->width = (insn >> 31) ? 64 : 32;
        d->rd = insn & 31u;
        d->rn = (insn >> 5) & 31u;
        d->use_sp = 1; /* Logical immediates can write SP, but read XZR. */
        return logical_immediate(insn, d->width, &d->imm);
    }
    if ((insn & 0x7F200000u) == 0x0A000000u ||
        (insn & 0x7F200000u) == 0x2A000000u ||
        (insn & 0x7F200000u) == 0x4A000000u) {
        if (((insn >> 22) & 3u) != 0) return false;
        unsigned opc = (insn >> 29) & 3u;
        d->op = opc == 0 ? ASIR_AND_REG :
                (opc == 1 ? ASIR_ORR_REG : ASIR_EOR_REG);
        d->width = (insn >> 31) ? 64 : 32;
        d->rd = insn & 31u;
        d->rn = (insn >> 5) & 31u;
        d->rm = (insn >> 16) & 31u;
        d->shift = (insn >> 10) & 0x3fu;
        if (d->width == 32 && d->shift >= 32) return false;
        return true;
    }
    if ((insn & 0x7FE0FC00u) == 0x1B007C00u) {
        d->op = ASIR_MUL;
        d->width = (insn >> 31) ? 64 : 32;
        d->rd = insn & 31u;
        d->rn = (insn >> 5) & 31u;
        d->rm = (insn >> 16) & 31u;
        return true;
    }
    /* Unsigned offset, integer W/X only: V=0 and opc=00/01. */
    if ((insn & 0xBFC00000u) == 0xB9000000u ||
        (insn & 0xBFC00000u) == 0xB9400000u) {
        d->op = (insn & 0x00400000u) ? ASIR_LOAD : ASIR_STORE;
        d->width = (insn & 0x40000000u) ? 64 : 32;
        d->rd = insn & 31u;
        d->rn = (insn >> 5) & 31u;
        d->use_sp = 1;
        d->imm = ((insn >> 10) & 0xfffu) * (d->width / 8u);
        return true;
    }
    /* Pre/post-index, integer W/X only: fix size[1], V, and opc[1]. */
    if ((insn & 0xBFA00C00u) == 0xB8000400u ||
        (insn & 0xBFA00C00u) == 0xB8000C00u) {
        d->op = (insn & 0x00400000u) ? ASIR_LOAD : ASIR_STORE;
        d->width = (insn & 0x40000000u) ? 64 : 32;
        d->rd = insn & 31u;
        d->rn = (insn >> 5) & 31u;
        d->use_sp = 1;
        d->offset = sign_extend((insn >> 12) & 0x1ffu, 9);
        d->addr_mode = ((insn >> 10) & 3u) == 1u ? AS_ADDR_POST : AS_ADDR_PRE;
        /* Overlapping base/data writeback is architecturally constrained. */
        if (d->rn != 31u && d->rn == d->rd) return false;
        return true;
    }
    /* Signed offset, pre-index or post-index pair, integer 64-bit only. */
    if ((insn & 0xFE000000u) == 0xA8000000u) {
        unsigned mode = (insn >> 23) & 3u;
        if (mode == 0u) return false; /* Non-temporal pair is not implemented. */
        d->op = (insn & 0x00400000u) ? ASIR_LOAD_PAIR64 : ASIR_STORE_PAIR64;
        d->rd = insn & 31u;
        d->rt2 = (insn >> 10) & 31u;
        d->rn = (insn >> 5) & 31u;
        d->use_sp = 1;
        d->offset = sign_extend((insn >> 15) & 0x7fu, 7) * 8;
        d->addr_mode = mode == 1u ? AS_ADDR_POST :
                       (mode == 3u ? AS_ADDR_PRE : AS_ADDR_OFFSET);
        if (d->op == ASIR_LOAD_PAIR64 && d->rd == d->rt2) return false;
        if (d->addr_mode != AS_ADDR_OFFSET && d->rn != 31u &&
            (d->rn == d->rd || d->rn == d->rt2)) return false;
        return true;
    }
    if ((insn & 0xFF000010u) == 0x54000000u) {
        d->op = ASIR_BRANCH_COND;
        d->cond = insn & 15u;
        d->offset = sign_extend((insn >> 5) & 0x7ffffu, 19) * 4;
        return true;
    }
    if ((insn & 0x1F000000u) == 0x10000000u) {
        uint64_t immediate = ((uint64_t)((insn >> 5) & 0x7ffffu) << 2) |
                             ((insn >> 29) & 3u);
        d->op = (insn & 0x80000000u) ? ASIR_ADRP : ASIR_ADR;
        d->rd = insn & 31u;
        d->offset = sign_extend(immediate, 21) * (d->op == ASIR_ADRP ? 4096 : 1);
        return true;
    }
    if ((insn & 0xFC000000u) == 0x14000000u ||
        (insn & 0xFC000000u) == 0x94000000u) {
        d->op = ASIR_BRANCH;
        d->rd = (insn & 0x80000000u) ? 30u : 31u;
        d->offset = sign_extend(insn & 0x03ffffffu, 26) * 4;
        return true;
    }
    if ((insn & 0x7E000000u) == 0x34000000u) {
        d->op = ASIR_BRANCH_ZERO;
        d->width = (insn >> 31) ? 64 : 32;
        d->rn = insn & 31u;
        d->imm = (insn >> 24) & 1u;
        d->offset = sign_extend((insn >> 5) & 0x7ffffu, 19) * 4;
        return true;
    }
    if ((insn & 0xFFFFFC1Fu) == 0xD61F0000u ||
        (insn & 0xFFFFFC1Fu) == 0xD65F0000u) {
        d->op = ASIR_BRANCH_REG;
        d->rn = (insn >> 5) & 31u;
        return true;
    }
    return false;
}
