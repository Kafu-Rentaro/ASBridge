// SPDX-License-Identifier: BSD-3-Clause
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asbridge/cpu.h"
#include "asbridge/decode.h"
#include "asbridge/interpreter.h"
#include "asbridge/memory.h"
#include "asbridge/tb.h"

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

static void logical_immediate_masks(void)
{
    /* Generate each rotated run bit by bit, independently of DecodeBitMasks. */
    for (unsigned width = 32; width <= 64; width *= 2) {
        for (unsigned element_bits = 2; element_bits <= width; element_bits *= 2) {
            for (unsigned ones = 1; ones < element_bits; ++ones) {
                for (unsigned rotation = 0; rotation < element_bits; ++rotation) {
                    uint64_t expected = 0;
                    for (unsigned bit = 0; bit < width; ++bit) {
                        if ((bit + rotation) % element_bits < ones) {
                            expected |= UINT64_C(1) << bit;
                        }
                    }
                    unsigned imms = ((~(2u * element_bits - 1u)) & 63u) | (ones - 1u);
                    uint32_t insn = 0x12000000u | (width == 64 ? 0x80000000u : 0) |
                                    (element_bits == 64 ? 0x00400000u : 0) |
                                    (rotation << 16) | (imms << 10) | (1u << 5);
                    for (unsigned opc = 0; opc < 3; ++opc) {
                        ASIR ir;
                        CHECK(as_decode_ir(insn | (opc << 29), &ir));
                        CHECK(ir.imm == expected && ir.width == width && ir.use_sp == 1);
                        CHECK(ir.op == (opc == 0 ? ASIR_AND_IMM :
                                        (opc == 1 ? ASIR_ORR_IMM : ASIR_EOR_IMM)));
                    }
                }
            }
        }
    }
    ASIR ir;
    CHECK(!as_decode_ir(0x12400000u, &ir)); /* N=1 in a W form. */
    CHECK(!as_decode_ir(0x1200FC00u, &ir)); /* Reserved all-one imms. */
    CHECK(!as_decode_ir(0x1200F800u, &ir)); /* Reserved one-bit element. */
    CHECK(!as_decode_ir(0x72000000u, &ir)); /* ANDS is not implemented. */

    ASCPU cpu;
    as_cpu_reset(&cpu, 0x1000);
    cpu.x[1] = UINT64_MAX;
    cpu.sp = UINT64_C(0xdeadbeef12345678);
    ASMemory memory = {0};
    CHECK(as_step(&cpu, &memory, 0x12007820u) == 0); /* and w0,w1,#0x7fffffff */
    CHECK(cpu.x[0] == 0x7fffffffu);
    CHECK(as_step(&cpu, &memory, 0x320003FFu) == 0); /* orr wsp,wzr,#1 */
    CHECK(cpu.sp == 1);
}

static void all_conditions(void)
{
    ASMemory memory = {0};
    for (unsigned flags = 0; flags < 16; ++flags) {
        unsigned n = (flags >> 3) & 1u, z = (flags >> 2) & 1u;
        unsigned carry = (flags >> 1) & 1u, overflow = flags & 1u;
        const int expected[16] = {
            z, !z, carry, !carry, n, !n, overflow, !overflow,
            carry && !z, !carry || z, n == overflow, n != overflow,
            !z && n == overflow, z || n != overflow, 1, 1
        };
        for (unsigned condition = 0; condition < 16; ++condition) {
            ASCPU cpu;
            as_cpu_reset(&cpu, 0x1000);
            cpu.nzcv = flags << 28;
            /* b.cond +8; taken and not-taken targets are distinct. */
            CHECK(as_step(&cpu, &memory, 0x54000040u | condition) == 0);
            CHECK(cpu.pc == (expected[condition] ? 0x1008u : 0x1004u));
        }
    }
}

static void unsupported_instruction_classes(void)
{
    const uint32_t unsupported[] = {
        0x38400420u, /* ldrb w0,[x1],#0 */
        0x78400C20u, /* ldrh w0,[x1,#0]! */
        0x38800C20u, /* ldrsb x0,[x1,#0]! */
        0x38C00420u, /* ldrsb w0,[x1],#0 */
        0xB8800C20u, /* ldrsw x0,[x1,#0]! */
        0xFD400020u, /* ldr d0,[x1] */
        0xFC400C20u, /* ldr d0,[x1,#0]! */
        0xAD400420u, /* ldp q0,q1,[x1] */
        0xF8400020u, /* ldur x0,[x1] */
        0xF8616820u, /* ldr x0,[x1,x1] */
        0xF8400C21u, /* ldr x1,[x1,#0]! constrained writeback overlap */
        0xA9400020u, /* ldp x0,x0,[x1] constrained destination overlap */
        0xA8C00420u, /* ldp x0,x1,[x1],#0 constrained writeback overlap */
        0x11800020u  /* reserved add immediate bit 23 */
    };
    ASIR ir;
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        CHECK(!as_decode_ir(unsupported[i], &ir));
    }
}

static void subtraction_flags(void)
{
    struct FlagCase { uint64_t a, b, result; uint32_t nzcv; unsigned width; };
    const struct FlagCase cases[] = {
        {0, 0, 0, 0x60000000u, 64},
        {0, 1, UINT64_MAX, 0x80000000u, 64},
        {UINT64_C(0x8000000000000000), 1, UINT64_C(0x7fffffffffffffff), 0x30000000u, 64},
        {UINT64_C(0x7fffffffffffffff), UINT64_MAX, UINT64_C(0x8000000000000000), 0x90000000u, 64},
        {UINT64_C(0xdeadbeef80000000), 1, 0x7fffffffu, 0x30000000u, 32},
        {0, 0xffffffffu, 1, 0, 32},
        {0xffffffffu, 1, 0xfffffffeu, 0xa0000000u, 32},
        {0x7fffffffu, 0xffffffffu, 0x80000000u, 0x90000000u, 32}
    };
    ASMemory memory = {0};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        ASCPU cpu;
        as_cpu_reset(&cpu, 0x1000);
        cpu.x[0] = cases[i].a;
        cpu.x[1] = cases[i].b;
        cpu.x[2] = UINT64_MAX;
        uint32_t insn = (cases[i].width == 64 ? 0xEB010002u : 0x6B010002u);
        CHECK(as_step(&cpu, &memory, insn) == 0);
        CHECK(cpu.x[2] == cases[i].result && cpu.nzcv == cases[i].nzcv);
    }
    ASCPU cpu;
    as_cpu_reset(&cpu, 0x1000);
    cpu.sp = 16;
    CHECK(as_step(&cpu, &memory, 0xF10043FFu) == 0); /* cmp sp,#16 */
    CHECK(cpu.nzcv == 0x60000000u && cpu.sp == 16);
    CHECK(as_step(&cpu, &memory, 0xEB0103FFu) == 0); /* cmp xzr,x1; x1=0 */
    CHECK(cpu.nzcv == 0x60000000u && cpu.sp == 16);
    cpu.x[0] = 4;
    cpu.x[1] = UINT64_C(0x80000001);
    CHECK(as_step(&cpu, &memory, 0x6B010402u) == 0); /* subs w2,w0,w1,lsl #1 */
    CHECK(cpu.x[2] == 2 && cpu.nzcv == 0x20000000u);
}

static void memory_semantics(void)
{
    uint8_t ram[32] = {0};
    ASMemory memory = {ram, sizeof(ram), 0x8000, 0};
    uint64_t value = 0;
    CHECK(as_mem_write64(&memory, 0x8000, UINT64_C(0x8877665544332211)) == 0);
    const uint8_t expected_bytes[] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
    CHECK(memcmp(ram, expected_bytes, sizeof(expected_bytes)) == 0);
    CHECK(memory.write_generation == 1);
    CHECK(as_mem_read64(&memory, 0x8000, &value) == 0 && value == UINT64_C(0x8877665544332211));
    CHECK(as_mem_write64(&memory, 0x8019, 0) == -1 && memory.write_generation == 1);

    ASCPU cpu;
    as_cpu_reset(&cpu, 0x1000);
    cpu.x[0] = 11;
    cpu.x[1] = 22;
    cpu.x[2] = memory.base;
    CHECK(as_step(&cpu, &memory, 0xA9010440u) == 0); /* stp x0,x1,[x2,#16] */
    CHECK(as_mem_read64(&memory, memory.base + 16, &value) == 0 && value == 11);
    CHECK(as_mem_read64(&memory, memory.base + 24, &value) == 0 && value == 22);
    cpu.x[0] = cpu.x[1] = 0;
    CHECK(as_step(&cpu, &memory, 0xA9410440u) == 0); /* ldp x0,x1,[x2,#16] */
    CHECK(cpu.x[0] == 11 && cpu.x[1] == 22 && cpu.x[2] == memory.base);

    cpu.x[2] = memory.base + 24;
    ASCPU before = cpu;
    CHECK(as_step(&cpu, &memory, 0xA8C10440u) == -2); /* ldp x0,x1,[x2],#16 */
    CHECK(memcmp(&before, &cpu, sizeof(cpu)) == 0);
    cpu.x[0] = 33;
    cpu.x[1] = 44;
    before = cpu;
    uint64_t generation = memory.write_generation;
    CHECK(as_step(&cpu, &memory, 0xA8810440u) == -2); /* stp x0,x1,[x2],#16 */
    CHECK(memcmp(&before, &cpu, sizeof(cpu)) == 0);
    CHECK(as_mem_read64(&memory, memory.base + 24, &value) == 0 && value == 33);
    CHECK(memory.write_generation == generation + 1);

    cpu.x[0] = 55;
    cpu.x[2] = memory.base;
    CHECK(as_step(&cpu, &memory, 0xF8008C40u) == 0); /* str x0,[x2,#8]! */
    CHECK(cpu.x[2] == memory.base + 8);
    CHECK(as_step(&cpu, &memory, 0xB85FC441u) == 0); /* ldr w1,[x2],#-4 */
    CHECK(cpu.x[1] == 55 && cpu.x[2] == memory.base + 4);

    memory.base = UINT64_MAX - 7;
    CHECK(as_mem_write64(&memory, memory.base, 99) == 0);
    CHECK(as_mem_read64(&memory, memory.base + 1, &value) == -1);
    cpu.x[2] = memory.base;
    generation = memory.write_generation;
    CHECK(as_step(&cpu, &memory, 0xA9000440u) == -2); /* second lane would wrap */
    CHECK(memory.write_generation == generation);
    CHECK(as_mem_read64(&memory, memory.base, &value) == 0 && value == 99);
}

static void pc_and_tb_bounds(void)
{
    uint8_t code[16] = {0};
    ASMemory memory = {code, sizeof(code), 0x1000, 0};
    CHECK(as_mem_write32(&memory, 0x1000, 0xD503201Fu) == 0);
    CHECK(as_mem_write32(&memory, 0x1004, 0xFFFFFFFFu) == 0);
    ASTranslationBlock tb;
    CHECK(as_tb_build(&tb, code, sizeof(code), memory.base, memory.base) == 0);
    CHECK(tb.count == 1 && tb.insn[0].raw == 0xD503201Fu);
    CHECK(as_tb_build(&tb, code, sizeof(code), memory.base, memory.base + 4) == -3);
    CHECK(as_tb_build(&tb, code, sizeof(code), memory.base, memory.base + 1) < 0);
    CHECK(as_tb_build(&tb, code, sizeof(code), memory.base, UINT64_MAX - 3) == -2);
    CHECK(as_tb_build(&tb, code, 4, UINT64_MAX - 7, UINT64_MAX - 7) == 0);
    CHECK(tb.count == 1);

    ASCPU cpu;
    as_cpu_reset(&cpu, UINT64_MAX - 3);
    ASCPU before = cpu;
    CHECK(as_step(&cpu, &memory, 0xD503201Fu) == -2);
    CHECK(memcmp(&before, &cpu, sizeof(cpu)) == 0);
    CHECK(as_run(&cpu, &memory, 1) == -2);
    as_cpu_reset(&cpu, memory.base + 1);
    CHECK(as_run(&cpu, &memory, 1) == -2);
    as_cpu_reset(&cpu, memory.base);
    CHECK(as_run(&cpu, &memory, 1) == 1 && cpu.pc == memory.base + 4);
    CHECK(as_run(&cpu, &memory, 1) == -1 && cpu.pc == memory.base + 4);
}

static void page_addresses(void)
{
    ASCPU cpu;
    ASMemory memory = {0};
    ASIR ir;
    CHECK(as_decode_ir(0xB0000000u, &ir) && ir.op == ASIR_ADRP && ir.offset == 4096);
    CHECK(as_decode_ir(0xF0FFFFE1u, &ir) && ir.op == ASIR_ADRP && ir.offset == -4096);
    as_cpu_reset(&cpu, 0x1abc);
    CHECK(as_step(&cpu, &memory, 0xB0000000u) == 0 && cpu.x[0] == 0x2000);
    CHECK(as_step(&cpu, &memory, 0xF0FFFFE1u) == 0 && cpu.x[1] == 0);
    CHECK(as_step(&cpu, &memory, 0x10FFFFE2u) == 0 && cpu.x[2] == cpu.pc - 8);
}

int main(void)
{
    logical_immediate_masks();
    all_conditions();
    unsupported_instruction_classes();
    subtraction_flags();
    memory_semantics();
    pc_and_tb_bounds();
    page_addresses();
    puts("ASCore reference semantics: PASS");
    return 0;
}
