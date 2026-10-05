// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_DECODE_H
#define ASBRIDGE_DECODE_H
#include <stdbool.h>
#include <stdint.h>

typedef enum ASOpcode {
    AS_OP_INVALID = 0,
    AS_OP_NOP,
    AS_OP_BRK,
    AS_OP_MOVZ,
    AS_OP_MOVK,
    AS_OP_ADD_IMM,
    AS_OP_SUB_IMM
} ASOpcode;

typedef struct ASDecoded {
    ASOpcode op;
    uint8_t rd;
    uint8_t rn;
    uint8_t shift;
    uint16_t imm16;
    uint16_t imm12;
} ASDecoded;

bool as_decode(uint32_t insn, ASDecoded *out);
#endif
