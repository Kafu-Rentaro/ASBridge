// SPDX-License-Identifier: BSD-3-Clause
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "asbridge/interpreter.h"
#include "asbridge/jit_runtime.h"

static void program(ASMemory *mem, const uint32_t *code, size_t count) {
    for (size_t k = 0; k < count; ++k) assert(!as_mem_write32(mem, mem->base + k * 4, code[k]));
}
static void same_cpu(const ASCPU *a, const ASCPU *b) {
    assert(!memcmp(a->x, b->x, sizeof(a->x)));
    assert(a->sp == b->sp && a->pc == b->pc && a->nzcv == b->nzcv);
    assert(a->halted == b->halted && a->halt_imm == b->halt_imm);
}
static void compare_run(ASMemory *mem, ASCPU *cpu, ASJitCache *cache,
                        uint64_t budget, int expected) {
    uint8_t copy[256];
    assert(mem->size <= sizeof(copy));
    memcpy(copy, mem->data, mem->size);
    ASMemory reference_mem = *mem;
    reference_mem.data = copy;
    ASCPU reference = *cpu;
    assert(as_run(&reference, &reference_mem, budget) == expected);
    assert(as_jit_run(cpu, mem, cache, budget, false) == expected);
    same_cpu(cpu, &reference);
    assert(!memcmp(mem->data, copy, mem->size));
}

static void loop_cache_test(void) {
    uint8_t ram[256] = {0};
    ASMemory mem = {.data = ram, .size = sizeof(ram), .base = 0x1000};
    const uint32_t code[] = {0xD2800000u, 0xD28000A1u, 0x91000400u,
                             0xF1000421u, 0x54FFFFC1u, 0xD4200000u};
    program(&mem, code, sizeof(code) / sizeof(code[0]));
    ASCPU cpu;
    as_cpu_reset(&cpu, mem.base);
    ASJitCache cache;
    assert(!as_jit_cache_init(&cache, 8));
    compare_run(&mem, &cpu, &cache, 100, 0);
    assert(cpu.x[0] == 5 && cache.hits >= 3 && cache.compilations >= 2);
    assert(cache.code_bytes && cache.fallback_steps == 0);
    as_jit_cache_invalidate(&cache);
    assert(cache.code_bytes == 0);
    as_jit_cache_destroy(&cache);
}

static void budget_fault_test(void) {
    uint8_t ram[256] = {0};
    ASMemory mem = {.data = ram, .size = sizeof(ram), .base = 0x1000};
    const uint32_t code[] = {0xD2800020u, 0x91000400u, 0xD4200000u};
    program(&mem, code, 3);
    ASCPU cpu;
    ASJitCache cache;
    assert(!as_jit_cache_init(&cache, 2));
    as_cpu_reset(&cpu, mem.base);
    compare_run(&mem, &cpu, &cache, 0, 1);
    assert(!cache.compilations);
    compare_run(&mem, &cpu, &cache, 1, 1);
    assert(cpu.x[0] == 1 && cpu.pc == mem.base + 4);
    compare_run(&mem, &cpu, &cache, 1, 1);
    compare_run(&mem, &cpu, &cache, 1, 0);
    assert(cpu.x[0] == 2);
    const uint32_t bad[] = {0xD2800020u, 0xFFFFFFFFu};
    program(&mem, bad, 2);
    as_cpu_reset(&cpu, mem.base);
    compare_run(&mem, &cpu, &cache, 10, -1);
    assert(cpu.x[0] == 1 && cpu.pc == mem.base + 4);
    assert(as_jit_run(&cpu, &mem, &cache, 1, true) == -1 && cache.fallback_steps == 1);
    assert(!as_mem_write32(&mem, mem.base, 0xF9400020u));
    as_cpu_reset(&cpu, mem.base);
    cpu.x[1] = UINT64_MAX - 3;
    compare_run(&mem, &cpu, &cache, 10, -2);
    as_cpu_reset(&cpu, mem.base + sizeof(ram));
    compare_run(&mem, &cpu, &cache, 10, -2);
    as_jit_cache_destroy(&cache);
}

static void invalidation_test(void) {
    uint8_t ram[256] = {0}, other[256] = {0};
    ASMemory mem = {.data = ram, .size = sizeof(ram), .base = 0x1000};
    ASJitCache cache;
    assert(!as_jit_cache_init(&cache, 1));
    ASCPU cpu;
    const uint32_t code[] = {0xD2800020u, 0xD4200000u};
    program(&mem, code, 2);
    as_cpu_reset(&cpu, mem.base);
    compare_run(&mem, &cpu, &cache, 10, 0);
    uint64_t compiled = cache.compilations;
    as_cpu_reset(&cpu, mem.base);
    compare_run(&mem, &cpu, &cache, 10, 0);
    assert(cache.compilations == compiled && cache.hits);
    /* Direct mutation is detected on reuse even before explicit invalidation. */
    ram[0] = 0xE0;
    as_cpu_reset(&cpu, mem.base);
    compare_run(&mem, &cpu, &cache, 10, 0);
    assert(cpu.x[0] == 7 && cache.compilations == compiled + 1);
    assert(!as_mem_write32(&mem, mem.base, 0xD2800120u));
    as_cpu_reset(&cpu, mem.base);
    compare_run(&mem, &cpu, &cache, 10, 0);
    assert(cpu.x[0] == 9 && cache.compilations == compiled + 2);
    mem.data = other;
    program(&mem, code, 2);
    as_cpu_reset(&cpu, mem.base);
    compare_run(&mem, &cpu, &cache, 10, 0);
    assert(cpu.x[0] == 1);
    /* Guest store replaces an instruction following the store in the same page. */
    const uint32_t selfmod[] = {0xB9000022u, 0xD503201Fu, 0xD2800020u, 0xD4200000u};
    program(&mem, selfmod, 4);
    as_cpu_reset(&cpu, mem.base);
    cpu.x[1] = mem.base + 8;
    cpu.x[2] = 0xD28000E0u;
    compare_run(&mem, &cpu, &cache, 10, 0);
    assert(cpu.x[0] == 7);
    as_jit_cache_destroy(&cache);
}

int main(void) {
    if (!as_jit_runtime_available()) { puts("ASJit runtime: native execution skipped on this host"); return 0; }
    loop_cache_test();
    budget_fault_test();
    invalidation_test();
    puts("ASJit runtime/cache: PASS");
    return 0;
}
