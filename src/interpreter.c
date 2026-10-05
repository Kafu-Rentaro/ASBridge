// SPDX-License-Identifier: BSD-3-Clause
#include "asbridge/decode.h"
#include "asbridge/interpreter.h"

static uint64_t read_gpr(const ASCPU *cpu, unsigned r) {
    return r == 31u ? 0u : cpu->x[r];
}

static void write_gpr(ASCPU *cpu, unsigned r, uint64_t v) {
    if (r != 31u) cpu->x[r] = v;
}

int as_step(ASCPU *cpu, uint32_t insn) {
    ASDecoded d;
    if (!as_decode(insn, &d)) return -1;

    switch (d.op) {
    case AS_OP_NOP:
        break;
    case AS_OP_BRK:
        cpu->halted = true;
        cpu->halt_imm = d.imm16;
        break;
    case AS_OP_MOVZ:
        write_gpr(cpu, d.rd, (uint64_t)d.imm16 << d.shift);
        break;
    case AS_OP_MOVK: {
        uint64_t mask = UINT64_C(0xFFFF) << d.shift;
        uint64_t v = (read_gpr(cpu, d.rd) & ~mask) |
                     ((uint64_t)d.imm16 << d.shift);
        write_gpr(cpu, d.rd, v);
        break;
    }
    case AS_OP_ADD_IMM:
        write_gpr(cpu, d.rd, read_gpr(cpu, d.rn) + ((uint64_t)d.imm12 << d.shift));
        break;
    case AS_OP_SUB_IMM:
        write_gpr(cpu, d.rd, read_gpr(cpu, d.rn) - ((uint64_t)d.imm12 << d.shift));
        break;
    default:
        return -1;
    }

    cpu->pc += 4;
    return 0;
}
