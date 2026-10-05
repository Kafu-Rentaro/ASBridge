// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_INTERPRETER_H
#define ASBRIDGE_INTERPRETER_H
#include <stdint.h>
#include "asbridge/cpu.h"
#include "asbridge/memory.h"
int as_step(ASCPU *cpu, ASMemory *mem, uint32_t insn);
int as_run(ASCPU *cpu, ASMemory *mem, uint64_t max_steps);
#endif
