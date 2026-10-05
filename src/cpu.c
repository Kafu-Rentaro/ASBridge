// SPDX-License-Identifier: BSD-3-Clause
#include <string.h>
#include "asbridge/cpu.h"

void as_cpu_reset(ASCPU *cpu, uint64_t entry) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->pc = entry;
}
