// SPDX-License-Identifier: BSD-3-Clause
#include "asbridge/decode.h"
#include "asbridge/interpreter.h"

static uint64_t width_mask(unsigned width)
{
    return width == 32 ? UINT64_C(0xffffffff) : UINT64_MAX;
}

static uint64_t read_zero(const ASCPU *cpu, unsigned reg, unsigned width)
{
    return (reg == 31u ? 0 : cpu->x[reg]) & width_mask(width);
}

static void write_zero(ASCPU *cpu, unsigned reg, uint64_t value, unsigned width)
{
    if (reg != 31u) cpu->x[reg] = value & width_mask(width);
}

static uint64_t read_sp(const ASCPU *cpu, unsigned reg, unsigned width)
{
    return (reg == 31u ? cpu->sp : cpu->x[reg]) & width_mask(width);
}

static void write_sp(ASCPU *cpu, unsigned reg, uint64_t value, unsigned width)
{
    value &= width_mask(width);
    if (reg == 31u) cpu->sp = value;
    else cpu->x[reg] = value;
}

static void subtraction_flags(ASCPU *cpu, uint64_t a, uint64_t b,
                              uint64_t result, unsigned width)
{
    uint64_t mask = width_mask(width);
    uint64_t sign = UINT64_C(1) << (width - 1);
    a &= mask;
    b &= mask;
    result &= mask;
    uint32_t n = (result & sign) != 0;
    uint32_t z = result == 0;
    uint32_t carry = a >= b;
    uint32_t overflow = ((a ^ b) & (a ^ result) & sign) != 0;
    cpu->nzcv = (n << 31) | (z << 30) | (carry << 29) | (overflow << 28);
}

static int condition_holds(const ASCPU *cpu, unsigned condition)
{
    unsigned n = (cpu->nzcv >> 31) & 1u;
    unsigned z = (cpu->nzcv >> 30) & 1u;
    unsigned carry = (cpu->nzcv >> 29) & 1u;
    unsigned overflow = (cpu->nzcv >> 28) & 1u;
    int result;
    switch (condition >> 1) {
    case 0: result = z; break;
    case 1: result = carry; break;
    case 2: result = n; break;
    case 3: result = overflow; break;
    case 4: result = carry && !z; break;
    case 5: result = n == overflow; break;
    case 6: result = !z && (n == overflow); break;
    default: result = 1; break; /* AL and NV both always hold in A64. */
    }
    return (condition & 1u) && condition != 15u ? !result : result;
}

/* Architectural effective-address additions are unsigned modulo 2^64. */
static uint64_t add_offset(uint64_t base, int64_t offset)
{
    return base + (uint64_t)offset;
}

int as_step(ASCPU *cpu, ASMemory *memory, uint32_t insn)
{
    if (!cpu) return -1;
    if ((cpu->pc & 3u) || cpu->pc > UINT64_MAX - 4) return -2;
    ASIR q;
    if (!as_decode_ir(insn, &q)) return -1;
    uint64_t next_pc = cpu->pc + 4;
    uint64_t value = 0, value2 = 0, a, b, result, base, address;
    switch (q.op) {
    case ASIR_NOP:
        break;
    case ASIR_HALT:
        cpu->halted = true;
        cpu->halt_imm = (uint16_t)q.imm;
        break;
    case ASIR_MOV_IMM:
        write_zero(cpu, q.rd, q.imm << q.shift, q.width);
        break;
    case ASIR_MOV_KEEP: {
        uint64_t mask = UINT64_C(0xffff) << q.shift;
        write_zero(cpu, q.rd, (read_zero(cpu, q.rd, q.width) & ~mask) |
                             ((q.imm << q.shift) & mask), q.width);
        break;
    }
    case ASIR_ADD_IMM:
        write_sp(cpu, q.rd, read_sp(cpu, q.rn, q.width) + q.imm, q.width);
        break;
    case ASIR_SUB_IMM:
        write_sp(cpu, q.rd, read_sp(cpu, q.rn, q.width) - q.imm, q.width);
        break;
    case ASIR_SUBS_IMM:
        a = read_sp(cpu, q.rn, q.width);
        result = (a - q.imm) & width_mask(q.width);
        subtraction_flags(cpu, a, q.imm, result, q.width);
        write_zero(cpu, q.rd, result, q.width);
        break;
    case ASIR_ADD_REG:
        write_zero(cpu, q.rd, read_zero(cpu, q.rn, q.width) +
                             (read_zero(cpu, q.rm, q.width) << q.shift), q.width);
        break;
    case ASIR_SUB_REG:
        write_zero(cpu, q.rd, read_zero(cpu, q.rn, q.width) -
                             (read_zero(cpu, q.rm, q.width) << q.shift), q.width);
        break;
    case ASIR_SUBS_REG:
        a = read_zero(cpu, q.rn, q.width);
        b = (read_zero(cpu, q.rm, q.width) << q.shift) & width_mask(q.width);
        result = (a - b) & width_mask(q.width);
        subtraction_flags(cpu, a, b, result, q.width);
        write_zero(cpu, q.rd, result, q.width);
        break;
    case ASIR_AND_IMM:
        write_sp(cpu, q.rd, read_zero(cpu, q.rn, q.width) & q.imm, q.width);
        break;
    case ASIR_ORR_IMM:
        write_sp(cpu, q.rd, read_zero(cpu, q.rn, q.width) | q.imm, q.width);
        break;
    case ASIR_EOR_IMM:
        write_sp(cpu, q.rd, read_zero(cpu, q.rn, q.width) ^ q.imm, q.width);
        break;
    case ASIR_AND_REG:
        write_zero(cpu, q.rd, read_zero(cpu, q.rn, q.width) &
                             (read_zero(cpu, q.rm, q.width) << q.shift), q.width);
        break;
    case ASIR_ORR_REG:
        write_zero(cpu, q.rd, read_zero(cpu, q.rn, q.width) |
                             (read_zero(cpu, q.rm, q.width) << q.shift), q.width);
        break;
    case ASIR_EOR_REG:
        write_zero(cpu, q.rd, read_zero(cpu, q.rn, q.width) ^
                             (read_zero(cpu, q.rm, q.width) << q.shift), q.width);
        break;
    case ASIR_MUL:
        write_zero(cpu, q.rd, read_zero(cpu, q.rn, q.width) *
                             read_zero(cpu, q.rm, q.width), q.width);
        break;
    case ASIR_LOAD:
        base = read_sp(cpu, q.rn, 64);
        address = q.addr_mode == AS_ADDR_POST ? base :
                  (q.addr_mode == AS_ADDR_PRE ? add_offset(base, q.offset) : base + q.imm);
        if (q.width == 32) {
            uint32_t temporary;
            if (as_mem_read32(memory, address, &temporary)) return -2;
            value = temporary;
        } else if (as_mem_read64(memory, address, &value)) return -2;
        write_zero(cpu, q.rd, value, q.width);
        if (q.addr_mode != AS_ADDR_OFFSET) write_sp(cpu, q.rn, add_offset(base, q.offset), 64);
        break;
    case ASIR_STORE:
        base = read_sp(cpu, q.rn, 64);
        address = q.addr_mode == AS_ADDR_POST ? base :
                  (q.addr_mode == AS_ADDR_PRE ? add_offset(base, q.offset) : base + q.imm);
        value = read_zero(cpu, q.rd, q.width);
        if (q.width == 32) {
            if (as_mem_write32(memory, address, (uint32_t)value)) return -2;
        } else if (as_mem_write64(memory, address, value)) return -2;
        if (q.addr_mode != AS_ADDR_OFFSET) write_sp(cpu, q.rn, add_offset(base, q.offset), 64);
        break;
    case ASIR_LOAD_PAIR64:
        base = read_sp(cpu, q.rn, 64);
        address = q.addr_mode == AS_ADDR_POST ? base : add_offset(base, q.offset);
        if (address > UINT64_MAX - 8 || as_mem_read64(memory, address, &value) ||
            as_mem_read64(memory, address + 8, &value2)) return -2;
        /* Neither architectural destination changes if the second access fails. */
        write_zero(cpu, q.rd, value, 64);
        write_zero(cpu, q.rt2, value2, 64);
        if (q.addr_mode != AS_ADDR_OFFSET) write_sp(cpu, q.rn, add_offset(base, q.offset), 64);
        break;
    case ASIR_STORE_PAIR64:
        base = read_sp(cpu, q.rn, 64);
        address = q.addr_mode == AS_ADDR_POST ? base : add_offset(base, q.offset);
        if (address > UINT64_MAX - 8 ||
            as_mem_write64(memory, address, read_zero(cpu, q.rd, 64)) ||
            as_mem_write64(memory, address + 8, read_zero(cpu, q.rt2, 64))) return -2;
        /* A normal second-lane fault may leave the first store, without writeback. */
        if (q.addr_mode != AS_ADDR_OFFSET) write_sp(cpu, q.rn, add_offset(base, q.offset), 64);
        break;
    case ASIR_BRANCH:
        if (q.rd == 30u) cpu->x[30] = next_pc;
        next_pc = add_offset(cpu->pc, q.offset);
        break;
    case ASIR_BRANCH_ZERO:
        if ((read_zero(cpu, q.rn, q.width) == 0) != (q.imm != 0)) {
            next_pc = add_offset(cpu->pc, q.offset);
        }
        break;
    case ASIR_BRANCH_REG:
        next_pc = read_zero(cpu, q.rn, 64);
        break;
    case ASIR_BRANCH_COND:
        if (condition_holds(cpu, q.cond)) next_pc = add_offset(cpu->pc, q.offset);
        break;
    case ASIR_ADR:
        write_zero(cpu, q.rd, add_offset(cpu->pc, q.offset), 64);
        break;
    case ASIR_ADRP:
        write_zero(cpu, q.rd, add_offset(cpu->pc & ~UINT64_C(0xfff), q.offset), 64);
        break;
    default:
        return -1;
    }
    cpu->pc = next_pc;
    return 0;
}

int as_run(ASCPU *cpu, ASMemory *memory, uint64_t max_steps)
{
    if (!cpu || !memory) return -1;
    while (!cpu->halted && max_steps != 0) {
        if ((cpu->pc & 3u) || cpu->pc > UINT64_MAX - 4) return -2;
        uint32_t insn;
        if (as_mem_read32(memory, cpu->pc, &insn)) return -2;
        int result = as_step(cpu, memory, insn);
        if (result) return result;
        --max_steps;
    }
    return cpu->halted ? 0 : 1;
}
