// SPDX-License-Identifier: BSD-3-Clause
#include <string.h>
#include "asbridge/decode.h"
#include "asbridge/tb.h"
static int terminal(ASIROp op){return op==ASIR_BRANCH||op==ASIR_BRANCH_ZERO||op==ASIR_BRANCH_REG||op==ASIR_BRANCH_COND||op==ASIR_HALT;}
int as_tb_build(ASTranslationBlock *tb,const uint8_t *code,size_t size,uint64_t base,uint64_t pc){if(!tb||!code||pc<base)return-1;memset(tb,0,sizeof(*tb));tb->guest_pc=pc;while(tb->count<AS_TB_MAX_INSNS){uint64_t off=pc-base;if(off>size||size-(size_t)off<4)return tb->count?0:-2;ASTBInsn*x=&tb->insn[tb->count];x->pc=pc;memcpy(&x->raw,code+(size_t)off,4);if(!as_decode_ir(x->raw,&x->ir))return-3;tb->count++;pc+=4;if(terminal(x->ir.op))break;}return 0;}
