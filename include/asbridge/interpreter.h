// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_INTERPRETER_H
#define ASBRIDGE_INTERPRETER_H
#include <stdint.h>
#include "asbridge/cpu.h"
int as_step(ASCPU *cpu, uint32_t insn);
#endif
