// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_IR_H
#define ASBRIDGE_IR_H
#include <stdint.h>
typedef enum ASIROp {
 ASIR_NOP=0,ASIR_MOV_IMM,ASIR_MOV_KEEP,ASIR_ADD_IMM,ASIR_SUB_IMM,
 ASIR_SUBS_IMM,ASIR_LOAD64,ASIR_STORE64,ASIR_BRANCH,ASIR_BRANCH_ZERO,
 ASIR_BRANCH_REG,ASIR_BRANCH_COND,ASIR_ADR,ASIR_ADRP,ASIR_HALT
} ASIROp;
typedef struct ASIR { ASIROp op; uint8_t rd,rn,shift,cond; uint64_t imm; int64_t offset; } ASIR;
#endif
