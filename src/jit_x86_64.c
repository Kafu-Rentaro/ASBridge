// SPDX-License-Identifier: BSD-3-Clause
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "asbridge/jit.h"

/* The generated function uses the System V x86-64 ABI. R12 holds the context,
 * R13 holds the CPU, and R14 holds the original memory base across helper calls.
 * Three preserved-register pushes also align the stack before each call. */
void as_jit_init(ASJitCode *j, uint8_t *buffer, size_t capacity)
{
    if (j) {
        j->data = buffer;
        j->size = 0;
        j->capacity = capacity;
    }
}

static int emit(ASJitCode *j, const void *bytes, size_t size)
{
    if (j->size > j->capacity || size > j->capacity - j->size)
        return -1;
    memcpy(j->data + j->size, bytes, size);
    j->size += size;
    return 0;
}
static int byte(ASJitCode *j, uint8_t value) { return emit(j, &value, 1); }
static int word32(ASJitCode *j, uint32_t value)
{
    uint8_t bytes[4];
    for (unsigned i = 0; i < 4; ++i) bytes[i] = (uint8_t)(value >> (8 * i));
    return emit(j, bytes, sizeof(bytes));
}
static int word64(ASJitCode *j, uint64_t value)
{
    uint8_t bytes[8];
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (uint8_t)(value >> (8 * i));
    return emit(j, bytes, sizeof(bytes));
}
#define EMIT(j, ...) emit((j), (const uint8_t[]){__VA_ARGS__}, \
                         sizeof((const uint8_t[]){__VA_ARGS__}))
#define TRY(expression) do { if ((expression) != 0) return -1; } while (0)

static uint32_t register_offset(unsigned reg, int sp)
{
    return (uint32_t)(reg == 31 && sp ? offsetof(ASCPU, sp) :
                      offsetof(ASCPU, x) + reg * sizeof(uint64_t));
}
/* Load into RAX or RCX; register 31 is interpreted explicitly at each use. */
static int load_reg(ASJitCode *j, unsigned reg, unsigned width, int sp, int rcx)
{
    if (reg == 31 && !sp) return EMIT(j, 0x31, rcx ? 0xC9 : 0xC0);
    TRY(byte(j, width == 64 ? 0x49 : 0x41));
    TRY(EMIT(j, 0x8B, rcx ? 0x8D : 0x85));
    return word32(j, register_offset(reg, sp));
}
static int store_rax(ASJitCode *j, unsigned reg, int sp)
{
    if (reg == 31 && !sp) return 0;
    /* Even W results are stored as 64 bits; their upper half must be zero. */
    TRY(EMIT(j, 0x49, 0x89, 0x85));
    return word32(j, register_offset(reg, sp));
}
static int immediate(ASJitCode *j, uint64_t value, int rcx)
{
    TRY(EMIT(j, 0x48, rcx ? 0xB9 : 0xB8));
    return word64(j, value);
}
static int store_pc(ASJitCode *j)
{
    TRY(EMIT(j, 0x49, 0x89, 0x85));
    return word32(j, (uint32_t)offsetof(ASCPU, pc));
}
static int set_pc(ASJitCode *j, uint64_t pc)
{
    TRY(immediate(j, pc, 0));
    return store_pc(j);
}
static int epilogue(ASJitCode *j)
{
    return EMIT(j, 0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0xC3);
}
static int shift_rcx(ASJitCode *j, unsigned width, unsigned shift)
{
    if (!shift) return 0;
    if (width == 64) TRY(byte(j, 0x48));
    return EMIT(j, 0xC1, 0xE1, (uint8_t)shift);
}
static int binary(ASJitCode *j, ASIROp op, unsigned width)
{
    if (width == 64) TRY(byte(j, 0x48));
    switch (op) {
    case ASIR_ADD_IMM: case ASIR_ADD_REG: return EMIT(j, 0x01, 0xC8);
    case ASIR_SUB_IMM: case ASIR_SUB_REG:
    case ASIR_SUBS_IMM: case ASIR_SUBS_REG: return EMIT(j, 0x29, 0xC8);
    case ASIR_AND_REG: case ASIR_AND_IMM: return EMIT(j, 0x21, 0xC8);
    case ASIR_ORR_REG: case ASIR_ORR_IMM: return EMIT(j, 0x09, 0xC8);
    case ASIR_EOR_REG: case ASIR_EOR_IMM: return EMIT(j, 0x31, 0xC8);
    case ASIR_MUL: return EMIT(j, 0x0F, 0xAF, 0xC1);
    default: return -1;
    }
}
static int sub_flags(ASJitCode *j)
{
    /* Capture flags before any instruction changes them. ARM subtraction C is
     * the inverse of the x86 borrow flag; N/Z/V are SF/ZF/OF respectively. */
    TRY(EMIT(j, 0x0F, 0x98, 0xC2,       /* sets dl */
                0x0F, 0x94, 0xC1,       /* setz cl */
                0x40, 0x0F, 0x93, 0xC6, /* setnc sil */
                0x40, 0x0F, 0x90, 0xC7, /* seto dil */
                0x0F, 0xB6, 0xD2,
                0x0F, 0xB6, 0xC9,
                0x40, 0x0F, 0xB6, 0xF6,
                0x40, 0x0F, 0xB6, 0xFF,
                0xC1, 0xE2, 31,
                0xC1, 0xE1, 30,
                0xC1, 0xE6, 29,
                0xC1, 0xE7, 28,
                0x09, 0xCA, 0x09, 0xF2, 0x09, 0xFA,
                0x41, 0x89, 0x95));
    return word32(j, (uint32_t)offsetof(ASCPU, nzcv));
}
static int branch_select(ASJitCode *j, uint64_t next, uint64_t target,
                         unsigned x86_condition)
{
    /* MOV does not alter the predicate flags used by CMOV. */
    TRY(immediate(j, next, 0));
    TRY(immediate(j, target, 1));
    TRY(EMIT(j, 0x48, 0x0F, (uint8_t)(0x40 | x86_condition), 0xC1));
    return store_pc(j);
}
static int conditional_branch(ASJitCode *j, const ASIR *q, uint64_t pc)
{
    if (q->cond >= 14) return set_pc(j, pc + (uint64_t)q->offset);
    TRY(EMIT(j, 0x41, 0x8B, 0x95));
    TRY(word32(j, (uint32_t)offsetof(ASCPU, nzcv)));
    unsigned predicate;
    if (q->cond < 8) {
        const uint32_t masks[] = {UINT32_C(1) << 30, UINT32_C(1) << 29,
                                  UINT32_C(1) << 31, UINT32_C(1) << 28};
        TRY(EMIT(j, 0xF7, 0xC2)); /* test edx, mask */
        TRY(word32(j, masks[q->cond / 2]));
        predicate = q->cond & 1 ? 4 : 5; /* even takes when bit set */
    } else if (q->cond < 10) {
        TRY(EMIT(j, 0x81, 0xE2));
        TRY(word32(j, UINT32_C(0x60000000)));
        TRY(EMIT(j, 0x81, 0xFA));
        TRY(word32(j, UINT32_C(0x20000000)));
        predicate = q->cond == 8 ? 4 : 5;
    } else {
        TRY(EMIT(j, 0x89, 0xD0, 0xC1, 0xE8, 3, 0x31, 0xD0,
                    0x25)); /* eax = (N xor V) in bit 28 */
        TRY(word32(j, UINT32_C(0x10000000)));
        if (q->cond >= 12) {
            TRY(EMIT(j, 0x81, 0xE2));
            TRY(word32(j, UINT32_C(0x40000000)));
            TRY(EMIT(j, 0x09, 0xD0));
        }
        TRY(EMIT(j, 0x85, 0xC0));
        predicate = q->cond & 1 ? 5 : 4;
    }
    return branch_select(j, pc + 4, pc + (uint64_t)q->offset, predicate);
}
static int call_helper(ASJitCode *j, const void *function, size_t pointer_size)
{
    uint64_t address = 0;
    if (pointer_size > sizeof(address)) return -1;
    memcpy(&address, function, pointer_size);
    TRY(immediate(j, address, 0));
    TRY(EMIT(j, 0xFF, 0xD0, 0x85, 0xC0, 0x74, 7));
    return epilogue(j); /* failing helper exits without subsequent state writes */
}
static int memory_operation(ASJitCode *j, const ASIR *q, uint64_t pc)
{
    int pair = q->op == ASIR_LOAD_PAIR64 || q->op == ASIR_STORE_PAIR64;
    uint64_t displacement = q->addr_mode == AS_ADDR_POST ? 0 :
        (pair || q->addr_mode == AS_ADDR_PRE ? (uint64_t)q->offset : q->imm);
    TRY(set_pc(j, pc));
    TRY(EMIT(j, 0x4D, 0x8B, 0xB5)); /* r14 = original base (SP allowed) */
    TRY(word32(j, register_offset(q->rn, 1)));
    TRY(EMIT(j, 0x4C, 0x89, 0xE7, 0xBE)); /* rdi = context; esi = rd */
    TRY(word32(j, q->rd));
    if (pair) {
        TRY(EMIT(j, 0xBA));
        TRY(word32(j, q->rt2));
        TRY(EMIT(j, 0x4C, 0x89, 0xF1)); /* rcx = EA */
        if (displacement) {
            TRY(immediate(j, displacement, 0));
            TRY(EMIT(j, 0x48, 0x01, 0xC1));
        }
        int (*fn)(ASJitContext *, unsigned, unsigned, uint64_t) =
            q->op == ASIR_LOAD_PAIR64 ? as_jit_load_pair : as_jit_store_pair;
        TRY(call_helper(j, &fn, sizeof(fn)));
    } else {
        TRY(EMIT(j, 0x4C, 0x89, 0xF2)); /* rdx = EA */
        if (displacement) {
            TRY(immediate(j, displacement, 0));
            TRY(EMIT(j, 0x48, 0x01, 0xC2));
        }
        TRY(byte(j, 0xB9));
        TRY(word32(j, q->width));
        int (*fn)(ASJitContext *, unsigned, uint64_t, unsigned) =
            q->op == ASIR_LOAD ? as_jit_load : as_jit_store;
        TRY(call_helper(j, &fn, sizeof(fn)));
    }
    if (q->addr_mode != AS_ADDR_OFFSET) {
        TRY(EMIT(j, 0x4C, 0x89, 0xF0)); /* rax = original base */
        TRY(immediate(j, (uint64_t)q->offset, 1));
        TRY(EMIT(j, 0x48, 0x01, 0xC8));
        TRY(store_rax(j, q->rn, 1));
    }
    return 0;
}

static int terminal(ASIROp op)
{
    return op == ASIR_BRANCH || op == ASIR_BRANCH_ZERO ||
           op == ASIR_BRANCH_REG || op == ASIR_BRANCH_COND || op == ASIR_HALT;
}
static int valid_ir(const ASIR *q)
{
    if (q->rd > 31 || q->rn > 31 || q->rm > 31 || q->rt2 > 31 ||
        (q->width != 32 && q->width != 64) || q->use_sp > 1)
        return 0;
    switch (q->op) {
    case ASIR_MOV_IMM: case ASIR_MOV_KEEP:
        return q->shift < q->width && q->shift % 16 == 0 && q->imm <= 0xFFFF;
    case ASIR_ADD_REG: case ASIR_SUB_REG: case ASIR_SUBS_REG:
    case ASIR_AND_REG: case ASIR_ORR_REG: case ASIR_EOR_REG:
        return q->shift < q->width;
    case ASIR_MUL: return q->shift == 0;
    case ASIR_LOAD: case ASIR_STORE:
        return q->addr_mode <= AS_ADDR_PRE &&
               (q->addr_mode == AS_ADDR_OFFSET || q->rn == 31 || q->rn != q->rd);
    case ASIR_LOAD_PAIR64: case ASIR_STORE_PAIR64:
        return q->width == 64 && q->addr_mode <= AS_ADDR_PRE &&
               (q->op != ASIR_LOAD_PAIR64 || q->rd != q->rt2) &&
               (q->addr_mode == AS_ADDR_OFFSET || q->rn == 31 ||
                (q->rn != q->rd && q->rn != q->rt2));
    case ASIR_BRANCH_COND: return q->cond <= 15;
    case ASIR_BRANCH_ZERO: return q->imm <= 1;
    case ASIR_BRANCH: return q->rd == 30 || q->rd == 31;
    case ASIR_NOP: case ASIR_HALT: case ASIR_ADD_IMM: case ASIR_SUB_IMM:
    case ASIR_SUBS_IMM: case ASIR_BRANCH_REG: case ASIR_ADR: case ASIR_ADRP:
    case ASIR_AND_IMM: case ASIR_ORR_IMM: case ASIR_EOR_IMM:
        return 1;
    default: return 0;
    }
}
static int lower_instruction(ASJitCode *j, const ASTBInsn *instruction)
{
    const ASIR *q = &instruction->ir;
    uint64_t pc = instruction->pc;
    switch (q->op) {
    case ASIR_NOP: break;
    case ASIR_MOV_IMM:
        TRY(immediate(j, (q->imm << q->shift) &
                      (q->width == 32 ? UINT32_MAX : UINT64_MAX), 0));
        TRY(store_rax(j, q->rd, 0));
        break;
    case ASIR_MOV_KEEP: {
        uint64_t mask = UINT64_C(0xFFFF) << q->shift;
        TRY(load_reg(j, q->rd, q->width, 0, 0));
        TRY(immediate(j, ~mask, 1));
        TRY(EMIT(j, 0x48, 0x21, 0xC8));
        TRY(immediate(j, (q->imm << q->shift) & mask, 1));
        TRY(EMIT(j, 0x48, 0x09, 0xC8));
        TRY(store_rax(j, q->rd, 0));
        break;
    }
    case ASIR_ADD_IMM: case ASIR_SUB_IMM: case ASIR_SUBS_IMM:
    case ASIR_AND_IMM: case ASIR_ORR_IMM: case ASIR_EOR_IMM: {
        int arith = q->op == ASIR_ADD_IMM || q->op == ASIR_SUB_IMM;
        int subs = q->op == ASIR_SUBS_IMM;
        TRY(load_reg(j, q->rn, q->width, arith || subs, 0));
        TRY(immediate(j, q->imm, 1));
        TRY(binary(j, q->op, q->width));
        if (subs) TRY(sub_flags(j));
        TRY(store_rax(j, q->rd, !subs));
        break;
    }
    case ASIR_ADD_REG: case ASIR_SUB_REG: case ASIR_SUBS_REG:
    case ASIR_AND_REG: case ASIR_ORR_REG: case ASIR_EOR_REG: case ASIR_MUL:
        TRY(load_reg(j, q->rn, q->width, 0, 0));
        TRY(load_reg(j, q->rm, q->width, 0, 1));
        TRY(shift_rcx(j, q->width, q->shift));
        TRY(binary(j, q->op, q->width));
        if (q->op == ASIR_SUBS_REG) TRY(sub_flags(j));
        TRY(store_rax(j, q->rd, 0));
        break;
    case ASIR_LOAD: case ASIR_STORE:
    case ASIR_LOAD_PAIR64: case ASIR_STORE_PAIR64:
        TRY(memory_operation(j, q, pc));
        break;
    case ASIR_BRANCH:
        if (q->rd == 30) {
            TRY(immediate(j, pc + 4, 0));
            TRY(store_rax(j, 30, 0));
        }
        return set_pc(j, pc + (uint64_t)q->offset);
    case ASIR_BRANCH_REG:
        TRY(load_reg(j, q->rn, 64, 0, 0));
        return store_pc(j);
    case ASIR_BRANCH_ZERO:
        TRY(load_reg(j, q->rn, q->width, 0, 1));
        if (q->width == 64) TRY(byte(j, 0x48));
        TRY(EMIT(j, 0x85, 0xC9));
        return branch_select(j, pc + 4, pc + (uint64_t)q->offset,
                             q->imm ? 5 : 4);
    case ASIR_BRANCH_COND:
        return conditional_branch(j, q, pc);
    case ASIR_ADR: case ASIR_ADRP:
        TRY(immediate(j, (q->op == ASIR_ADR ? pc : pc & ~UINT64_C(0xFFF)) +
                        (uint64_t)q->offset, 0));
        TRY(store_rax(j, q->rd, 0));
        break;
    case ASIR_HALT:
        TRY(EMIT(j, 0x41, 0xC6, 0x85));
        TRY(word32(j, (uint32_t)offsetof(ASCPU, halted)));
        TRY(byte(j, 1));
        TRY(EMIT(j, 0x66, 0x41, 0xC7, 0x85));
        TRY(word32(j, (uint32_t)offsetof(ASCPU, halt_imm)));
        TRY(byte(j, (uint8_t)q->imm));
        TRY(byte(j, (uint8_t)(q->imm >> 8)));
        break;
    default: return -1; /* the public entry point validates before emission */
    }
    return set_pc(j, pc + 4);
}

int as_jit_emit_tb_x86_64(ASJitCode *j, const ASTranslationBlock *tb)
{
    if (!j || !tb || !j->data || j->size > j->capacity ||
        !tb->count || tb->count > AS_TB_MAX_INSNS)
        return -1;
    size_t initial_size = j->size;
    for (size_t i = 0; i < tb->count; ++i) {
        if (!valid_ir(&tb->insn[i].ir) ||
            (terminal(tb->insn[i].ir.op) && i + 1 != tb->count))
            return -2;
    }
    if (EMIT(j, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                0x49, 0x89, 0xFC, 0x4D, 0x8B, 0xAC, 0x24) ||
        word32(j, (uint32_t)offsetof(ASJitContext, cpu)) ||
        EMIT(j, 0x41, 0xC7, 0x84, 0x24) ||
        word32(j, (uint32_t)offsetof(ASJitContext, fault)) || word32(j, 0))
        goto failure;
    for (size_t i = 0; i < tb->count; ++i)
        if (lower_instruction(j, &tb->insn[i])) goto failure;
    if (epilogue(j)) goto failure;
    return 0;
failure:
    j->size = initial_size;
    return -1;
}

static int helper_valid(ASJitContext *ctx, unsigned reg, unsigned width)
{
    if (!ctx) return -1;
    if (!ctx->cpu || !ctx->mem || reg > 31 ||
        (width != 32 && width != 64)) {
        ctx->fault = -1;
        return -1;
    }
    return 0;
}
static int memory_fault(ASJitContext *ctx) { ctx->fault = -2; return -2; }
int as_jit_load(ASJitContext *ctx, unsigned reg, uint64_t addr, unsigned width)
{
    if (helper_valid(ctx, reg, width)) return -1;
    uint64_t value;
    if (width == 32) {
        uint32_t word;
        if (as_mem_read32(ctx->mem, addr, &word)) return memory_fault(ctx);
        value = word;
    } else if (as_mem_read64(ctx->mem, addr, &value)) return memory_fault(ctx);
    if (reg != 31) ctx->cpu->x[reg] = value;
    return 0;
}
int as_jit_store(ASJitContext *ctx, unsigned reg, uint64_t addr, unsigned width)
{
    if (helper_valid(ctx, reg, width)) return -1;
    uint64_t value = reg == 31 ? 0 : ctx->cpu->x[reg];
    int result = width == 32 ? as_mem_write32(ctx->mem, addr, (uint32_t)value) :
                              as_mem_write64(ctx->mem, addr, value);
    return result ? memory_fault(ctx) : 0;
}
int as_jit_load_pair(ASJitContext *ctx, unsigned reg, unsigned reg2, uint64_t addr)
{
    if (helper_valid(ctx, reg, 64) || helper_valid(ctx, reg2, 64)) return -1;
    uint64_t value, value2;
    if (addr > UINT64_MAX - 8 || as_mem_read64(ctx->mem, addr, &value) ||
        as_mem_read64(ctx->mem, addr + 8, &value2)) return memory_fault(ctx);
    /* Loads commit both destinations only after both accesses succeed. */
    if (reg != 31) ctx->cpu->x[reg] = value;
    if (reg2 != 31) ctx->cpu->x[reg2] = value2;
    return 0;
}
int as_jit_store_pair(ASJitContext *ctx, unsigned reg, unsigned reg2, uint64_t addr)
{
    if (helper_valid(ctx, reg, 64) || helper_valid(ctx, reg2, 64)) return -1;
    uint64_t value = reg == 31 ? 0 : ctx->cpu->x[reg];
    uint64_t value2 = reg2 == 31 ? 0 : ctx->cpu->x[reg2];
    /* Match the reference backend: the first store may persist when the second
     * access faults, but generated base writeback has not happened yet. */
    if (addr > UINT64_MAX - 8 || as_mem_write64(ctx->mem, addr, value) ||
        as_mem_write64(ctx->mem, addr + 8, value2)) return memory_fault(ctx);
    return 0;
}
