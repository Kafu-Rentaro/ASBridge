// SPDX-License-Identifier: BSD-3-Clause
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "asbridge/cpu.h"
#include "asbridge/decode.h"
#include "asbridge/interpreter.h"

static void test_decode(void) {
    ASIR q;
    assert(as_decode_ir(0xF9000022u, &q) && q.op == ASIR_STORE64 && q.rn == 1 && q.rd == 2);
    assert(as_decode_ir(0xF9400023u, &q) && q.op == ASIR_LOAD64  && q.rn == 1 && q.rd == 3);
    assert(as_decode_ir(0x14000002u, &q) && q.op == ASIR_BRANCH && q.offset == 8 && q.rd == 31);
    assert(as_decode_ir(0x94000002u, &q) && q.op == ASIR_BRANCH && q.offset == 8 && q.rd == 30);
    assert(as_decode_ir(0xB4000040u, &q) && q.op == ASIR_BRANCH_ZERO && q.rn == 0 && q.imm == 0);
    assert(as_decode_ir(0xB5000040u, &q) && q.op == ASIR_BRANCH_ZERO && q.rn == 0 && q.imm == 1);
    assert(as_decode_ir(0xD65F03C0u, &q) && q.op == ASIR_BRANCH_REG && q.rn == 30);
}

static void test_program(void) {
    ASCPU c;
    uint8_t ram[0x1000] = {0};
    ASMemory m = {ram, sizeof(ram), 0x80000000u};
    uint32_t p[] = {
        0xD2824680u, /* movz x0,#0x1234 */
        0x91004000u, /* add  x0,x0,#16 */
        0xD1001000u, /* sub  x0,x0,#4 */
        0xD4200000u  /* brk  #0 */
    };
    memcpy(ram, p, sizeof(p));
    as_cpu_reset(&c, m.base);
    assert(as_run(&c, &m, 16) == 0);
    assert(c.x[0] == 0x1240u);
    assert(c.halted && c.pc == m.base + sizeof(p));
}

static void test_memory(void) {
    ASCPU c;
    uint8_t ram[0x1000] = {0};
    ASMemory m = {ram, sizeof(ram), 0x80000000u};
    as_cpu_reset(&c, m.base);
    c.x[1] = m.base + 0x100;
    c.x[2] = UINT64_C(0x1122334455667788);
    assert(as_step(&c, &m, 0xF9000022u) == 0);
    c.x[3] = 0;
    assert(as_step(&c, &m, 0xF9400023u) == 0);
    assert(c.x[3] == c.x[2]);
}

static void test_control_flow(void) {
    ASCPU c;
    uint8_t ram[0x1000] = {0};
    ASMemory m = {ram, sizeof(ram), 0x80000000u};
    as_cpu_reset(&c, m.base);
    assert(as_step(&c, &m, 0x94000002u) == 0); /* bl +8 */
    assert(c.x[30] == m.base + 4 && c.pc == m.base + 8);
    assert(as_step(&c, &m, 0xD65F03C0u) == 0); /* ret */
    assert(c.pc == m.base + 4);
}

int main(void) {
    test_decode();
    test_program();
    test_memory();
    test_control_flow();
    puts("ASCore v0.0.2: PASS");
    return 0;
}
