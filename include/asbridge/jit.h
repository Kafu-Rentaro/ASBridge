// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_JIT_H
#define ASBRIDGE_JIT_H
#include <stddef.h>
#include <stdint.h>
#include "asbridge/cpu.h"
#include "asbridge/memory.h"
#include "asbridge/tb.h"
typedef struct ASJitCode { uint8_t *data; size_t size; size_t capacity; } ASJitCode;
typedef struct ASJitContext { ASCPU *cpu; ASMemory *mem; int fault; } ASJitContext;
void as_jit_init(ASJitCode *j,uint8_t *buffer,size_t capacity);
int as_jit_emit_tb_x86_64(ASJitCode *j,const ASTranslationBlock *tb);
int as_jit_load(ASJitContext *ctx,unsigned reg,uint64_t addr,unsigned width);
int as_jit_store(ASJitContext *ctx,unsigned reg,uint64_t addr,unsigned width);
#endif
