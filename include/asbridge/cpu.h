// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_CPU_H
#define ASBRIDGE_CPU_H
#include <stdbool.h>
#include <stdint.h>

typedef struct ASCPU {
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    uint32_t nzcv;
    bool halted;
    uint16_t halt_imm;
} ASCPU;

void as_cpu_reset(ASCPU *cpu, uint64_t entry);
#endif
