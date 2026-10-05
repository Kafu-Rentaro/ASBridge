// SPDX-License-Identifier: BSD-3-Clause
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asbridge/cpu.h"
#include "asbridge/decode.h"
#include "asbridge/elf.h"
#include "asbridge/interpreter.h"
#include "asbridge/tb.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

static void decode_tests(void)
{
    ASIR q;
    CHECK(as_decode_ir(0xF9000022u, &q)); /* str x2, [x1] */
    CHECK(q.op == ASIR_STORE && q.rd == 2 && q.rn == 1 && q.width == 64);
    CHECK(as_decode_ir(0xB9400023u, &q)); /* ldr w3, [x1] */
    CHECK(q.op == ASIR_LOAD && q.rd == 3 && q.width == 32);
    CHECK(as_decode_ir(0xA9BF7BFDu, &q)); /* stp x29, x30, [sp, #-16]! */
    CHECK(q.op == ASIR_STORE_PAIR64 && q.rn == 31 && q.rd == 29 &&
          q.rt2 == 30 && q.addr_mode == AS_ADDR_PRE && q.offset == -16);
    CHECK(as_decode_ir(0xA8C17BFDu, &q)); /* ldp x29, x30, [sp], #16 */
    CHECK(q.op == ASIR_LOAD_PAIR64 && q.addr_mode == AS_ADDR_POST && q.offset == 16);
    CHECK(as_decode_ir(0xA9410C22u, &q)); /* ldp x2, x3, [x1, #16] */
    CHECK(q.addr_mode == AS_ADDR_OFFSET && q.offset == 16);
    CHECK(as_decode_ir(0xD2A24680u, &q)); /* movz x0, #0x1234, lsl #16 */
    CHECK(q.op == ASIR_MOV_IMM && q.imm == 0x1234 && q.shift == 16 && q.width == 64);
    CHECK(as_decode_ir(0xF280ACE0u, &q)); /* movk x0, #0x567 */
    CHECK(q.op == ASIR_MOV_KEEP && q.imm == 0x567 && q.width == 64);
    CHECK(!as_decode_ir(0xFFFFFFFFu, &q));
    CHECK(!as_decode_ir(0x52C00020u, &q)); /* reserved movz w0, lsl #32 */
    CHECK(!as_decode_ir(0x8B410020u, &q)); /* add x0, x1, x1, lsr #0 */
    CHECK(!as_decode_ir(0x8AC10020u, &q)); /* and x0, x1, x1, ror #0 */
}

static void stack_frame_test(void)
{
    ASCPU cpu;
    uint8_t ram[4096] = {0};
    ASMemory mem = {.data = ram, .size = sizeof(ram), .base = 0x80000000u};
    as_cpu_reset(&cpu, mem.base);
    cpu.sp = mem.base + 0x800;
    cpu.x[29] = 0x1111;
    cpu.x[30] = 0x2222;
    uint64_t initial_sp = cpu.sp;
    CHECK(as_step(&cpu, &mem, 0xA9BF7BFDu) == 0);
    CHECK(cpu.sp == initial_sp - 16);
    cpu.x[29] = cpu.x[30] = 0;
    CHECK(as_step(&cpu, &mem, 0xA8C17BFDu) == 0);
    CHECK(cpu.sp == initial_sp && cpu.x[29] == 0x1111 && cpu.x[30] == 0x2222);
}

static void program_test(void)
{
    ASCPU cpu;
    uint8_t ram[4096] = {0};
    ASMemory mem = {.data = ram, .size = sizeof(ram), .base = 0x80000000u};
    /* movz x0, #0x1234; add x0, x0, #16; sub x0, x0, #4; brk #0 */
    const uint32_t code[] = {0xD2824680u, 0x91004000u, 0xD1001000u, 0xD4200000u};
    for (size_t i = 0; i < sizeof(code) / sizeof(code[0]); ++i) {
        CHECK(as_mem_write32(&mem, mem.base + i * 4, code[i]) == 0);
    }
    as_cpu_reset(&cpu, mem.base);
    CHECK(as_run(&cpu, &mem, 16) == 0 && cpu.x[0] == 0x1240u);
    CHECK(cpu.halted && cpu.pc == mem.base + sizeof(code));
}

static void wreg_test(void)
{
    ASCPU cpu;
    uint8_t ram[4096] = {0};
    ASMemory mem = {.data = ram, .size = sizeof(ram), .base = 0x80000000u};
    as_cpu_reset(&cpu, mem.base);
    cpu.x[0] = UINT64_MAX;
    CHECK(as_step(&cpu, &mem, 0x52800020u) == 0); /* movz w0, #1 */
    CHECK(cpu.x[0] == 1);
    cpu.x[1] = mem.base + 0x100;
    cpu.x[2] = UINT64_C(0xdeadbeef12345678);
    CHECK(as_step(&cpu, &mem, 0xB9000022u) == 0); /* str w2, [x1] */
    cpu.x[3] = UINT64_MAX;
    CHECK(as_step(&cpu, &mem, 0xB9400023u) == 0); /* ldr w3, [x1] */
    CHECK(cpu.x[3] == UINT64_C(0x12345678));
}

static void tb_test(void)
{
    const uint8_t code[] = {
        0x20, 0x00, 0x80, 0xD2, /* movz x0, #1 */
        0x00, 0x08, 0x00, 0x91, /* add x0, x0, #2 */
        0x00, 0x00, 0x20, 0xD4, /* brk #0 */
        0x1F, 0x20, 0x03, 0xD5  /* nop, after terminator */
    };
    ASTranslationBlock tb;
    CHECK(as_tb_build(&tb, code, sizeof(code), 0x1000, 0x1000) == 0);
    CHECK(tb.count == 3 && tb.insn[2].ir.op == ASIR_HALT);
    CHECK(tb.insn[0].pc == 0x1000 && tb.insn[2].pc == 0x1008);
    CHECK(as_tb_build(&tb, code, sizeof(code), 0x1000, 0x100C) == 0);
    CHECK(tb.count == 1 && tb.insn[0].ir.op == ASIR_NOP);
    CHECK(as_tb_build(&tb, code, sizeof(code), 0x1000, 0x1010) != 0);
    CHECK(as_tb_build(&tb, code, sizeof(code), 0x1000, 0x1001) != 0);
}

static void elf_reject_test(void)
{
    uint8_t bad[64] = {0}, ram[4096] = {0};
    ASMemory mem = {.data = ram, .size = sizeof(ram), .base = 0x400000};
    ASELFImage image;
    CHECK(as_elf_load(bad, sizeof(bad), &mem, &image) != 0);
    bad[0] = 0x7f;
    bad[1] = 'E';
    bad[2] = 'L';
    bad[3] = 'F';
    bad[4] = 2;
    bad[5] = 1;
    CHECK(as_elf_load(bad, sizeof(bad), &mem, &image) != 0);
}

int main(void)
{
    decode_tests();
    stack_frame_test();
    program_test();
    wreg_test();
    tb_test();
    elf_reject_test();
    puts("ASCore v0.0.4: PASS");
    return 0;
}
