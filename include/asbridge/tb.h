// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_TB_H
#define ASBRIDGE_TB_H
#include <stddef.h>
#include <stdint.h>
#include "asbridge/ir.h"
#define AS_TB_MAX_INSNS 64
typedef struct ASTBInsn { uint64_t pc; uint32_t raw; ASIR ir; } ASTBInsn;
typedef struct ASTranslationBlock { uint64_t guest_pc; size_t count; ASTBInsn insn[AS_TB_MAX_INSNS]; } ASTranslationBlock;
int as_tb_build(ASTranslationBlock *tb,const uint8_t *code,size_t size,uint64_t base,uint64_t pc);
#endif
