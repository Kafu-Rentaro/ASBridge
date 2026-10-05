// SPDX-License-Identifier: BSD-3-Clause
#include <assert.h>
#include <stdio.h>
#include "asbridge/cpu.h"
#include "asbridge/interpreter.h"

int main(void) {
    ASCPU cpu;
    as_cpu_reset(&cpu, 0x80000000u);

    assert(as_step(&cpu, 0xD2824680u) == 0); /* movz x0,#0x1234 */
    assert(cpu.x[0] == 0x1234u);

    assert(as_step(&cpu, 0x91004000u) == 0); /* add x0,x0,#16 */
    assert(cpu.x[0] == 0x1244u);

    assert(as_step(&cpu, 0xD1001000u) == 0); /* sub x0,x0,#4 */
    assert(cpu.x[0] == 0x1240u);

    assert(as_step(&cpu, 0xD503201Fu) == 0); /* nop */

    assert(as_step(&cpu, 0xD4200000u) == 0); /* brk #0 */
    assert(cpu.halted);

    puts("ASCore v0.0.1: PASS");
    return 0;
}
