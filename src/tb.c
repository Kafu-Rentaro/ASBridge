// SPDX-License-Identifier: BSD-3-Clause
#include <string.h>
#include "asbridge/decode.h"
#include "asbridge/tb.h"

static int terminal(ASIROp op)
{
    return op == ASIR_BRANCH || op == ASIR_BRANCH_ZERO ||
           op == ASIR_BRANCH_REG || op == ASIR_BRANCH_COND || op == ASIR_HALT ||
           op == ASIR_STORE || op == ASIR_STORE_PAIR64;
}

int as_tb_build(ASTranslationBlock *tb, const uint8_t *code, size_t size,
                uint64_t base, uint64_t pc)
{
    if (!tb || !code || pc < base || (pc & 3u)) return -1;
    memset(tb, 0, sizeof(*tb));
    tb->guest_pc = pc;
    while (tb->count < AS_TB_MAX_INSNS) {
        uint64_t offset = pc - base;
        if (pc > UINT64_MAX - 4 || offset > size || size - (size_t)offset < 4) {
            return tb->count ? 0 : -2;
        }
        ASTBInsn *insn = &tb->insn[tb->count];
        insn->pc = pc;
        const uint8_t *bytes = code + (size_t)offset;
        insn->raw = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                    ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
        /* Execute a valid prefix before handling the unsupported instruction. */
        if (!as_decode_ir(insn->raw, &insn->ir)) return tb->count ? 0 : -3;
        ++tb->count;
        pc += 4;
        if (terminal(insn->ir.op)) break;
    }
    return 0;
}
