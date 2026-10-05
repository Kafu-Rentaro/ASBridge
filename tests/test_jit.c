// SPDX-License-Identifier: BSD-3-Clause
#if defined(__linux__)
#define _DEFAULT_SOURCE 1
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__x86_64__) && (defined(__unix__) || defined(__APPLE__))
#define JIT_RUNTIME_TESTS 1
#include <sys/mman.h>
#include <unistd.h>
#else
#define JIT_RUNTIME_TESTS 0
#endif
#include "asbridge/cpu.h"
#include "asbridge/decode.h"
#include "asbridge/interpreter.h"
#include "asbridge/jit.h"
#include "asbridge/tb.h"

static const char *scenario = "setup";
#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "%s:%d [%s]: %s\n", __FILE__, __LINE__, scenario, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)
#define RAM_SIZE 256u
#define RAM_BASE UINT64_C(0x9000)
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

typedef struct Outcome {
    ASCPU cpu;
    uint8_t ram[RAM_SIZE];
} Outcome;

static ASCPU initial_cpu(void)
{
    ASCPU cpu;
    as_cpu_reset(&cpu, 0x1000);
    for (unsigned r = 0; r < 31; ++r) {
        cpu.x[r] = UINT64_C(0xcafebabe00100000) + r * UINT64_C(0x1000101);
    }
    cpu.sp = RAM_BASE + 128;
    cpu.nzcv = 0x90001234u;
    cpu.halt_imm = 0x4567;
    return cpu;
}

static void cpu_equal(const ASCPU *actual, const ASCPU *expected)
{
    for (unsigned r = 0; r < 31; ++r) {
        if (actual->x[r] != expected->x[r]) {
            fprintf(stderr, "[%s] x%u: JIT=0x%llx reference=0x%llx\n", scenario,
                    r, (unsigned long long)actual->x[r],
                    (unsigned long long)expected->x[r]);
            CHECK(actual->x[r] == expected->x[r]);
        }
    }
    CHECK(actual->sp == expected->sp);
    CHECK(actual->pc == expected->pc);
    CHECK(actual->nzcv == expected->nzcv);
    CHECK(actual->halted == expected->halted);
    CHECK(actual->halt_imm == expected->halt_imm);
}

/* The words below are architectural A64 encodings, not hand-made ASIR. */
static Outcome differential(const char *name, const uint32_t *words, size_t count,
                            const ASCPU *initial, const uint8_t *initial_ram,
                            int expected_fault)
{
    scenario = name;
    CHECK(count > 0 && count <= AS_TB_MAX_INSNS);
    uint8_t code[AS_TB_MAX_INSNS * 4];
    for (size_t i = 0; i < count; ++i) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            code[i * 4 + byte] = (uint8_t)(words[i] >> (byte * 8));
        }
    }
    ASTranslationBlock tb;
    CHECK(as_tb_build(&tb, code, count * 4, initial->pc, initial->pc) == 0);
    CHECK(tb.count == count);
    uint8_t emitted[16384];
    ASJitCode jit_code;
    as_jit_init(&jit_code, emitted, sizeof(emitted));
    CHECK(as_jit_emit_tb_x86_64(&jit_code, &tb) == 0);
    CHECK(jit_code.size > 0 && jit_code.size <= sizeof(emitted));

    Outcome reference;
    reference.cpu = *initial;
    for (size_t i = 0; i < RAM_SIZE; ++i) {
        reference.ram[i] = initial_ram ? initial_ram[i] : (uint8_t)(i * 37 + 11);
    }
    ASMemory reference_mem = {.data = reference.ram, .size = RAM_SIZE, .base = RAM_BASE};
    int reference_fault = 0;
    for (size_t i = 0; i < tb.count; ++i) {
        reference_fault = as_step(&reference.cpu, &reference_mem, words[i]);
        if (reference_fault != 0) {
            break;
        }
    }
    CHECK(reference_fault == expected_fault);
#if JIT_RUNTIME_TESTS
    Outcome actual;
    actual.cpu = *initial;
    for (size_t i = 0; i < RAM_SIZE; ++i) {
        actual.ram[i] = initial_ram ? initial_ram[i] : (uint8_t)(i * 37 + 11);
    }
    ASMemory actual_mem = {.data = actual.ram, .size = RAM_SIZE, .base = RAM_BASE};
    ASJitContext ctx = {&actual.cpu, &actual_mem, -99};
    long page_size = sysconf(_SC_PAGESIZE);
    CHECK(page_size > 0);
    size_t map_size = (jit_code.size + (size_t)page_size - 1) /
                      (size_t)page_size * (size_t)page_size;
#if defined(MAP_ANONYMOUS)
    int anonymous_flag = MAP_ANONYMOUS;
#else
    int anonymous_flag = MAP_ANON;
#endif
    void *executable = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | anonymous_flag, -1, 0);
    CHECK(executable != MAP_FAILED);
    memcpy(executable, emitted, jit_code.size);
    CHECK(mprotect(executable, map_size, PROT_READ | PROT_EXEC) == 0);
    /* memcpy avoids an ISO C object/function-pointer cast warning. */
    void (*fn)(ASJitContext *);
    _Static_assert(sizeof(fn) == sizeof(executable), "POSIX function pointer size");
    memcpy(&fn, &executable, sizeof(fn));
    fn(&ctx);
    CHECK(munmap(executable, map_size) == 0);
    CHECK(ctx.fault == reference_fault);
    cpu_equal(&actual.cpu, &reference.cpu);
    CHECK(memcmp(actual.ram, reference.ram, RAM_SIZE) == 0);
    CHECK(actual_mem.write_generation == reference_mem.write_generation);
#endif
    return reference;
}

static Outcome one(const char *name, uint32_t raw, const ASCPU *cpu)
{
    return differential(name, &raw, 1, cpu, NULL, 0);
}

static void integer_tests(void)
{
    ASCPU cpu = initial_cpu();
    Outcome result = one("movz W zero extension", 0x52800020u, &cpu);
    CHECK(result.cpu.x[0] == 1 && result.cpu.pc == cpu.pc + 4);
    cpu.x[0] = UINT64_MAX;
    result = one("movk W zero extension", 0x72A24680u, &cpu);
    CHECK(result.cpu.x[0] == 0x1234FFFFu); /* movk w0, #0x1234, lsl #16 */
    result = one("movk X high halfword", 0xF2E24680u, &cpu);
    CHECK(result.cpu.x[0] == UINT64_C(0x1234ffffffffffff));
    one("movz XZR discards", 0xD280003Fu, &cpu);
    one("movk XZR discards", 0xF280003Fu, &cpu);

    cpu.x[1] = UINT64_C(0xdeadbeeffffffffe);
    cpu.x[2] = 3;
    result = one("add W immediate wrap", 0x11000C20u, &cpu); /* add w0,w1,#3 */
    CHECK(result.cpu.x[0] == 1);
    result = one("sub W immediate", 0x51000C20u, &cpu);
    CHECK(result.cpu.x[0] == 0xFFFFFFFBu);
    result = one("add W register wrap", 0x0B020020u, &cpu);
    CHECK(result.cpu.x[0] == 1);
    result = one("sub W register", 0x4B020020u, &cpu);
    CHECK(result.cpu.x[0] == 0xFFFFFFFBu);
    result = one("add X shifted register", 0x8B020C20u, &cpu); /* lsl #3 */
    CHECK(result.cpu.x[0] == cpu.x[1] + 24);
    result = one("sub X shifted register", 0xCB020C20u, &cpu);
    CHECK(result.cpu.x[0] == cpu.x[1] - 24);
    one("add register XZR source", 0x8B0203E0u, &cpu);
    one("add register XZR destination", 0x8B02003Fu, &cpu);
    one("sub register XZR source", 0xCB1F0020u, &cpu);
    one("add immediate X SP source", 0x910043E0u, &cpu); /* add x0,sp,#16 */
    result = one("sub immediate X SP destination", 0xD100401Fu, &cpu);
    CHECK(result.cpu.sp == cpu.x[0] - 16); /* sub sp,x0,#16 */
    cpu.sp = UINT64_C(0x12345678ffffffff);
    result = one("add WSP wraps and zero extends", 0x110007FFu, &cpu);
    CHECK(result.cpu.sp == 0); /* add wsp,wsp,#1 */

    cpu.x[1] = UINT64_C(0xaaaaaaaaf0f00f0f);
    cpu.x[2] = UINT64_C(0xbbbbbbbb0ff0ff00);
    result = one("and W", 0x0A020020u, &cpu);
    CHECK(result.cpu.x[0] == 0x00F00F00u);
    result = one("orr W", 0x2A020020u, &cpu);
    CHECK(result.cpu.x[0] == 0xFFF0FF0Fu);
    result = one("eor W", 0x4A020020u, &cpu);
    CHECK(result.cpu.x[0] == 0xFF00F00Fu);
    one("and X shifted", 0x8A020C20u, &cpu);
    one("orr X shifted", 0xAA020C20u, &cpu);
    one("eor X shifted", 0xCA020C20u, &cpu);
    one("logical XZR read", 0xAA1F0020u, &cpu);
    one("logical XZR discard", 0xCA02003Fu, &cpu);
    result = one("and W immediate", 0x12001C20u, &cpu); /* and w0,w1,#0xff */
    CHECK(result.cpu.x[0] == 0x0F);
    result = one("and X immediate", 0x92401C20u, &cpu); /* and x0,x1,#0xff */
    CHECK(result.cpu.x[0] == 0x0F);
    result = one("orr X rotated immediate", 0xB2781C20u, &cpu); /* #0xff00 */
    CHECK(result.cpu.x[0] == (cpu.x[1] | 0xFF00u));
    result = one("eor W repeated immediate", 0x5204CC20u, &cpu); /* #0xf0f0f0f0 */
    CHECK(result.cpu.x[0] == 0x0000FFFFu);
    result = one("logical immediate SP destination", 0x92401C3Fu, &cpu);
    CHECK(result.cpu.sp == 0x0F);
    result = one("logical immediate XZR source", 0xB2401FE0u, &cpu);
    CHECK(result.cpu.x[0] == 0xFF);

    cpu.x[1] = UINT64_MAX;
    cpu.x[2] = 3;
    result = one("mul W zero extension", 0x1B027C20u, &cpu);
    CHECK(result.cpu.x[0] == 0xFFFFFFFDu);
    result = one("mul X wrap", 0x9B027C20u, &cpu);
    CHECK(result.cpu.x[0] == UINT64_MAX - 2);
    result = one("mul XZR source", 0x9B1F7C20u, &cpu);
    CHECK(result.cpu.x[0] == 0);
    one("mul XZR discard", 0x9B027C3Fu, &cpu);
}

static void put64(uint8_t *ram, size_t offset, uint64_t value)
{
    CHECK(offset <= RAM_SIZE - 8);
    for (unsigned byte = 0; byte < 8; ++byte) {
        ram[offset + byte] = (uint8_t)(value >> (byte * 8));
    }
}

static uint64_t get64(const uint8_t *ram, size_t offset)
{
    uint64_t value = 0;
    CHECK(offset <= RAM_SIZE - 8);
    for (unsigned byte = 0; byte < 8; ++byte) {
        value |= (uint64_t)ram[offset + byte] << (byte * 8);
    }
    return value;
}

static void memory_tests(void)
{
    uint8_t ram[RAM_SIZE];
    memset(ram, 0xA5, sizeof(ram));
    put64(ram, 64, UINT64_C(0x1122334489abcdef));
    put64(ram, 80, UINT64_C(0x9988776655443322));
    ASCPU cpu = initial_cpu();
    cpu.x[1] = RAM_BASE + 64;
    cpu.x[2] = UINT64_C(0x0123456789abcdef);
    uint32_t raw = 0xF9000822u; /* str x2,[x1,#16] */
    Outcome result = differential("str X unsigned offset", &raw, 1, &cpu, ram, 0);
    CHECK(get64(result.ram, 80) == cpu.x[2]);
    CHECK(get64(result.ram, 64) == get64(ram, 64));
    raw = 0xF9400823u; /* ldr x3,[x1,#16] */
    result = differential("ldr X unsigned offset", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.x[3] == UINT64_C(0x9988776655443322));
    raw = 0xB9001022u; /* str w2,[x1,#16] */
    result = differential("str W preserves adjacent bytes", &raw, 1, &cpu, ram, 0);
    CHECK(get64(result.ram, 80) == UINT64_C(0x9988776689abcdef));
    raw = 0xB9400023u; /* ldr w3,[x1] */
    result = differential("ldr W zero extends", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.x[3] == 0x89ABCDEFu);
    raw = 0xF8408423u; /* ldr x3,[x1],#8 */
    result = differential("ldr X post-index", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.x[1] == cpu.x[1] + 8 && result.cpu.x[3] == get64(ram, 64));
    raw = 0xF8408C23u; /* ldr x3,[x1,#8]! */
    result = differential("ldr X pre-index", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.x[1] == cpu.x[1] + 8 && result.cpu.x[3] == get64(ram, 72));
    raw = 0xF81F8C22u; /* str x2,[x1,#-8]! */
    result = differential("str X negative pre-index", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.x[1] == cpu.x[1] - 8 && get64(result.ram, 56) == cpu.x[2]);
    raw = 0xB8004422u; /* str w2,[x1],#4 */
    result = differential("str W post-index", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.x[1] == cpu.x[1] + 4);
    CHECK(get64(result.ram, 64) == UINT64_C(0x1122334489abcdef));
    raw = 0xB8404C23u; /* ldr w3,[x1,#4]! */
    result = differential("ldr W pre-index", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.x[1] == cpu.x[1] + 4 && result.cpu.x[3] == 0x11223344u);

    raw = 0xF900003Fu; /* str xzr,[x1] */
    result = differential("str XZR stores zero", &raw, 1, &cpu, ram, 0);
    CHECK(get64(result.ram, 64) == 0);
    raw = 0xF940003Fu; /* ldr xzr,[x1] */
    result = differential("ldr XZR discards", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.sp == cpu.sp);
    cpu.sp = RAM_BASE + 64;
    raw = 0xF94003E3u; /* ldr x3,[sp] */
    result = differential("ldr SP base", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.x[3] == get64(ram, 64));
    raw = 0xF90003E2u; /* str x2,[sp] */
    result = differential("str SP base", &raw, 1, &cpu, ram, 0);
    CHECK(get64(result.ram, 64) == cpu.x[2]);

    const uint32_t bad_access[] = {
        0xF9400023u, /* ldr x3,[x1] */
        0xF9000022u, /* str x2,[x1] */
        0xF8408423u, /* ldr x3,[x1],#8 */
        0xF8008422u, /* str x2,[x1],#8 */
        0xB9400023u, /* ldr w3,[x1] */
        0xB9000022u, /* str w2,[x1] */
        0xF940003Fu, /* ldr xzr,[x1] still faults */
        0xF900003Fu  /* str xzr,[x1] still faults */
    };
    const uint64_t bad_addresses[] = {RAM_BASE - 1, RAM_BASE + RAM_SIZE - 2,
                                      RAM_BASE + RAM_SIZE, UINT64_MAX - 3};
    for (size_t a = 0; a < ARRAY_COUNT(bad_addresses); ++a) {
        cpu.x[1] = bad_addresses[a];
        for (size_t op = 0; op < ARRAY_COUNT(bad_access); ++op) {
            result = differential("memory fault leaves state intact", &bad_access[op],
                                  1, &cpu, ram, -2);
            cpu_equal(&result.cpu, &cpu);
            CHECK(memcmp(result.ram, ram, RAM_SIZE) == 0);
        }
    }
    cpu.x[1] = RAM_BASE + RAM_SIZE - 8;
    raw = 0xF8408C23u; /* ldr x3,[x1,#8]! */
    result = differential("pre-index load fault has no writeback", &raw, 1, &cpu, ram, -2);
    cpu_equal(&result.cpu, &cpu);
    raw = 0xF8008C22u; /* str x2,[x1,#8]! */
    result = differential("pre-index store fault has no writeback", &raw, 1, &cpu, ram, -2);
    cpu_equal(&result.cpu, &cpu);

    /* A later fault preserves earlier effects and reports the faulting guest PC. */
    const uint32_t faulting_load[] = {0xD28000E4u, 0xF9400023u, 0xD2800124u};
    cpu.x[1] = RAM_BASE + RAM_SIZE;
    result = differential("load fault exits TB at current instruction", faulting_load,
                          ARRAY_COUNT(faulting_load), &cpu, ram, -2);
    CHECK(result.cpu.x[4] == 7 && result.cpu.pc == cpu.pc + 4);
    CHECK(result.cpu.x[3] == cpu.x[3]);
    const uint32_t faulting_store[] = {0xD28000E4u, 0xF9000022u};
    result = differential("store fault preserves preceding instruction", faulting_store,
                          ARRAY_COUNT(faulting_store), &cpu, ram, -2);
    CHECK(result.cpu.x[4] == 7 && result.cpu.pc == cpu.pc + 4);
    CHECK(memcmp(result.ram, ram, RAM_SIZE) == 0);
}

static void pair_tests(void)
{
    uint8_t ram[RAM_SIZE];
    memset(ram, 0x5A, sizeof(ram));
    ASCPU cpu = initial_cpu();
    cpu.x[1] = RAM_BASE + 64;
    cpu.x[2] = UINT64_C(0x0123456789abcdef);
    cpu.x[3] = UINT64_C(0xfedcba9876543210);
    uint32_t raw = 0xA9010C22u; /* stp x2,x3,[x1,#16] */
    Outcome result = differential("stp nonzero offset", &raw, 1, &cpu, ram, 0);
    CHECK(get64(result.ram, 80) == cpu.x[2] && get64(result.ram, 88) == cpu.x[3]);
    CHECK(get64(result.ram, 64) == get64(ram, 64));
    put64(ram, 80, 0x12345678);
    put64(ram, 88, UINT64_C(0x1122334455667788));
    raw = 0xA9410C22u; /* ldp x2,x3,[x1,#16] */
    result = differential("ldp nonzero offset", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.x[2] == 0x12345678 && result.cpu.x[3] == get64(ram, 88));
    raw = 0xA9BF0C22u; /* stp x2,x3,[x1,#-16]! */
    result = differential("stp X base pre-index", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.x[1] == cpu.x[1] - 16);
    CHECK(get64(result.ram, 48) == cpu.x[2] && get64(result.ram, 56) == cpu.x[3]);
    raw = 0xA8C10C22u; /* ldp x2,x3,[x1],#16 */
    result = differential("ldp X base post-index", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.x[1] == cpu.x[1] + 16);

    raw = 0xA9BF7BFDu; /* stp x29,x30,[sp,#-16]! */
    Outcome saved = differential("canonical SP frame push", &raw, 1, &cpu, ram, 0);
    CHECK(saved.cpu.sp == cpu.sp - 16);
    CHECK(get64(saved.ram, 112) == cpu.x[29] && get64(saved.ram, 120) == cpu.x[30]);
    ASCPU changed = saved.cpu;
    changed.x[29] = changed.x[30] = 0;
    raw = 0xA8C17BFDu; /* ldp x29,x30,[sp],#16 */
    result = differential("canonical SP frame pop", &raw, 1, &changed, saved.ram, 0);
    CHECK(result.cpu.sp == cpu.sp && result.cpu.x[29] == cpu.x[29] &&
          result.cpu.x[30] == cpu.x[30]);
    raw = 0xA9007FFFu; /* stp xzr,xzr,[sp] */
    result = differential("stp XZR lanes", &raw, 1, &cpu, ram, 0);
    CHECK(get64(result.ram, 128) == 0 && get64(result.ram, 136) == 0);
    raw = 0xA9407FE2u; /* ldp x2,xzr,[sp] */
    result = differential("ldp XZR lanes", &raw, 1, &cpu, ram, 0);
    CHECK(result.cpu.sp == cpu.sp);

    const uint32_t failing_pairs[] = {
        0xA9400C22u, /* ldp x2,x3,[x1] */
        0xA8C10C22u, /* ldp x2,x3,[x1],#16 */
        0xA9C10C22u, /* ldp x2,x3,[x1,#16]! */
        0xA9000C22u, /* stp x2,x3,[x1] */
        0xA8810C22u, /* stp x2,x3,[x1],#16 */
        0xA9810C22u  /* stp x2,x3,[x1,#16]! */
    };
    for (size_t i = 0; i < ARRAY_COUNT(failing_pairs); ++i) {
        cpu.x[1] = RAM_BASE + RAM_SIZE - 8 - (i == 2 || i == 5 ? 16 : 0);
        result = differential("pair second lane fault", &failing_pairs[i], 1, &cpu, ram, -2);
        CHECK(result.cpu.pc == cpu.pc && result.cpu.x[1] == cpu.x[1]);
        CHECK(result.cpu.x[2] == cpu.x[2] && result.cpu.x[3] == cpu.x[3]);
        if (i < 3) {
            CHECK(memcmp(result.ram, ram, RAM_SIZE) == 0);
        }
    }
    cpu.x[1] = RAM_BASE - 1;
    raw = 0xA8C10C22u;
    result = differential("pair first lane load fault", &raw, 1, &cpu, ram, -2);
    cpu_equal(&result.cpu, &cpu);
    CHECK(memcmp(result.ram, ram, RAM_SIZE) == 0);
    raw = 0xA8810C22u;
    result = differential("pair first lane store fault", &raw, 1, &cpu, ram, -2);
    cpu_equal(&result.cpu, &cpu);
    CHECK(memcmp(result.ram, ram, RAM_SIZE) == 0);
    cpu.sp = RAM_BASE + 8;
    raw = 0xA9BF7BFDu;
    result = differential("SP pair pre-index fault has no writeback", &raw, 1, &cpu, ram, -2);
    cpu_equal(&result.cpu, &cpu);
    CHECK(memcmp(result.ram, ram, RAM_SIZE) == 0);
    cpu.sp = RAM_BASE + RAM_SIZE - 8;
    raw = 0xA8C17BFDu;
    result = differential("SP pair second lane fault", &raw, 1, &cpu, ram, -2);
    cpu_equal(&result.cpu, &cpu);
}

static void flag_tests(void)
{
    /* Independent architectural subtraction corner cases: N Z C V. */
    static const struct {
        uint64_t a, b;
        uint32_t flags;
    } w_cases[] = {
        {0, 0, 0x60000000u},
        {0, 1, 0x80000000u},
        {0x80000000u, 1, 0x30000000u},
        {0x7FFFFFFFu, 0xFFFFFFFFu, 0x90000000u},
        {0xFFFFFFFFu, 1, 0xA0000000u},
    }, x_cases[] = {
        {0, 0, 0x60000000u},
        {0, 1, 0x80000000u},
        {UINT64_C(0x8000000000000000), 1, 0x30000000u},
        {UINT64_C(0x7fffffffffffffff), UINT64_MAX, 0x90000000u},
        {UINT64_MAX, 1, 0xA0000000u},
    };
    ASCPU cpu = initial_cpu();
    for (size_t i = 0; i < ARRAY_COUNT(w_cases); ++i) {
        cpu.x[1] = UINT64_C(0xdeadbeef00000000) | w_cases[i].a;
        cpu.x[2] = UINT64_C(0xcafebabe00000000) | w_cases[i].b;
        Outcome result = one("subs W flags and zero extension", 0x6B020020u, &cpu);
        CHECK(result.cpu.nzcv == w_cases[i].flags);
        CHECK(result.cpu.x[0] == (uint32_t)(w_cases[i].a - w_cases[i].b));
        result = one("cmp W register discards result", 0x6B02003Fu, &cpu);
        CHECK(result.cpu.x[0] == cpu.x[0] && result.cpu.nzcv == w_cases[i].flags);
    }
    for (size_t i = 0; i < ARRAY_COUNT(x_cases); ++i) {
        cpu.x[1] = x_cases[i].a;
        cpu.x[2] = x_cases[i].b;
        Outcome result = one("subs X flags", 0xEB020020u, &cpu);
        CHECK(result.cpu.nzcv == x_cases[i].flags);
        CHECK(result.cpu.x[0] == x_cases[i].a - x_cases[i].b);
        result = one("cmp X register discards result", 0xEB02003Fu, &cpu);
        CHECK(result.cpu.x[0] == cpu.x[0] && result.cpu.nzcv == x_cases[i].flags);
    }
    cpu.x[1] = 0;
    Outcome result = one("subs W immediate flags", 0x71000420u, &cpu);
    CHECK(result.cpu.x[0] == UINT32_MAX && result.cpu.nzcv == 0x80000000u);
    cpu.x[1] = UINT64_C(0x8000000000000000);
    result = one("subs X immediate overflow", 0xF1000420u, &cpu);
    CHECK(result.cpu.x[0] == UINT64_C(0x7fffffffffffffff) &&
          result.cpu.nzcv == 0x30000000u);
    cpu.sp = 16;
    result = one("cmp SP reads SP rather than XZR", 0xF10043FFu, &cpu);
    CHECK(result.cpu.nzcv == 0x60000000u && result.cpu.sp == 16);
    cpu.sp = UINT64_C(0xcafebabe00000010);
    result = one("cmp WSP truncates input", 0x710043FFu, &cpu);
    CHECK(result.cpu.nzcv == 0x60000000u && result.cpu.sp == cpu.sp);
    cpu.x[1] = 24;
    cpu.x[2] = 3;
    result = one("subs X LSL register", 0xEB020C20u, &cpu);
    CHECK(result.cpu.x[0] == 0 && result.cpu.nzcv == 0x60000000u);
    result = one("subs register XZR first operand", 0xEB0203E0u, &cpu);
    CHECK(result.cpu.x[0] == UINT64_MAX - 2 && result.cpu.nzcv == 0x80000000u);
}

static void branch_tests(void)
{
    ASCPU cpu = initial_cpu();
    Outcome result = one("B positive", 0x14000003u, &cpu); /* b .+12 */
    CHECK(result.cpu.pc == cpu.pc + 12 && result.cpu.x[30] == cpu.x[30]);
    result = one("B negative", 0x17FFFFFEu, &cpu); /* b .-8 */
    CHECK(result.cpu.pc == cpu.pc - 8);
    result = one("BL link and target", 0x94000003u, &cpu);
    CHECK(result.cpu.pc == cpu.pc + 12 && result.cpu.x[30] == cpu.pc + 4);
    cpu.x[5] = 0x123400;
    result = one("BR register", 0xD61F00A0u, &cpu); /* br x5 */
    CHECK(result.cpu.pc == cpu.x[5]);
    cpu.x[30] = 0xABC000;
    result = one("RET default link register", 0xD65F03C0u, &cpu);
    CHECK(result.cpu.pc == cpu.x[30]);
    result = one("RET explicit register", 0xD65F00A0u, &cpu); /* ret x5 */
    CHECK(result.cpu.pc == cpu.x[5]);
    result = one("BR XZR reads zero", 0xD61F03E0u, &cpu);
    CHECK(result.cpu.pc == 0);

    const uint32_t zero_branches[] = {0x34000061u, 0x35000061u,
                                      0xB4000061u, 0xB5000061u};
    const uint64_t values[] = {0, 1, UINT64_C(0x100000000)};
    for (size_t op = 0; op < ARRAY_COUNT(zero_branches); ++op) {
        for (size_t value = 0; value < ARRAY_COUNT(values); ++value) {
            cpu.x[1] = values[value];
            result = one("CBZ/CBNZ W/X taken and fallthrough", zero_branches[op], &cpu);
            int is_zero = op < 2 ? (uint32_t)cpu.x[1] == 0 : cpu.x[1] == 0;
            int taken = (op & 1u) ? !is_zero : is_zero;
            CHECK(result.cpu.pc == cpu.pc + (taken ? 12u : 4u));
        }
    }
    result = one("CBZ XZR always zero", 0xB400007Fu, &cpu);
    CHECK(result.cpu.pc == cpu.pc + 12);
    result = one("CBNZ XZR fallthrough", 0xB500007Fu, &cpu);
    CHECK(result.cpu.pc == cpu.pc + 4);

    /* Each bit selects an NZCV nibble. All sixteen B.cond predicates. */
    const uint16_t taken_masks[16] = {
        0xF0F0, 0x0F0F, 0xCCCC, 0x3333, 0xFF00, 0x00FF, 0xAAAA, 0x5555,
        0x0C0C, 0xF3F3, 0xAA55, 0x55AA, 0x0A05, 0xF5FA, 0xFFFF, 0xFFFF
    };
    for (unsigned cond = 0; cond < 16; ++cond) {
        for (unsigned flags = 0; flags < 16; ++flags) {
            cpu.nzcv = ((uint32_t)flags << 28) | 0x1234;
            result = one("B.cond full condition and NZCV truth table",
                         0x54000060u | cond, &cpu); /* b.cond .+12 */
            int taken = (taken_masks[cond] >> flags) & 1u;
            CHECK(result.cpu.pc == cpu.pc + (taken ? 12u : 4u));
            CHECK(result.cpu.nzcv == cpu.nzcv);
        }
    }
    cpu.nzcv = 0x40000000u;
    result = one("B.cond negative displacement", 0x54FFFFC0u, &cpu); /* b.eq .-8 */
    CHECK(result.cpu.pc == cpu.pc - 8);
}

static void address_and_block_tests(void)
{
    ASCPU cpu = initial_cpu();
    cpu.pc = 0x1FFC;
    Outcome result = one("ADR positive displacement", 0x10000080u, &cpu);
    CHECK(result.cpu.x[0] == cpu.pc + 16);
    result = one("ADR negative displacement", 0x10FFFFE0u, &cpu);
    CHECK(result.cpu.x[0] == cpu.pc - 4);
    result = one("ADRP rounds PC to page", 0xB0000000u, &cpu);
    CHECK(result.cpu.x[0] == 0x2000);
    result = one("ADRP negative displacement", 0xF0FFFFE0u, &cpu);
    CHECK(result.cpu.x[0] == 0);
    one("ADR XZR discards", 0x1000009Fu, &cpu);
    one("ADRP XZR discards", 0xB000001Fu, &cpu);
    result = one("NOP advances PC", 0xD503201Fu, &cpu);
    CHECK(result.cpu.pc == cpu.pc + 4);
    result = one("BRK immediate is retained", 0xD4224680u, &cpu);
    CHECK(result.cpu.halted && result.cpu.halt_imm == 0x1234 && result.cpu.pc == cpu.pc + 4);

    cpu.x[1] = 7;
    const uint32_t code[] = {0xD2800020u, 0x91000800u, 0x9B017C00u,
                             0x72A24680u, 0xD4224680u};
    result = differential("multi-instruction integer TB", code, ARRAY_COUNT(code),
                          &cpu, NULL, 0);
    CHECK(result.cpu.x[0] == 0x12340015u && result.cpu.pc == cpu.pc + sizeof(code));
    CHECK(result.cpu.halted && result.cpu.halt_imm == 0x1234);
}

static void helper_tests(void)
{
    scenario = "safe memory helpers";
    uint8_t ram[RAM_SIZE] = {0};
    ASMemory mem = {.data = ram, .size = RAM_SIZE, .base = RAM_BASE};
    ASCPU cpu = initial_cpu();
    ASJitContext ctx = {&cpu, &mem, 0};
    cpu.x[1] = UINT64_C(0x1122334455667788);
    CHECK(as_jit_store(&ctx, 1, RAM_BASE + 16, 64) == 0);
    CHECK(as_jit_load(&ctx, 2, RAM_BASE + 16, 64) == 0 && cpu.x[2] == cpu.x[1]);
    cpu.x[3] = UINT64_MAX;
    CHECK(as_jit_store(&ctx, 3, RAM_BASE + 32, 32) == 0);
    CHECK(as_jit_load(&ctx, 4, RAM_BASE + 32, 32) == 0 && cpu.x[4] == UINT32_MAX);
    CHECK(as_jit_store(&ctx, 31, RAM_BASE + 16, 64) == 0 && get64(ram, 16) == 0);
    uint64_t old_sp = cpu.sp;
    CHECK(as_jit_load(&ctx, 31, RAM_BASE + 32, 32) == 0 && cpu.sp == old_sp);
    uint64_t old_x0 = cpu.x[0];
    CHECK(as_jit_load(&ctx, 0, RAM_BASE + RAM_SIZE - 4, 64) == -2 && ctx.fault == -2);
    CHECK(cpu.x[0] == old_x0);
    CHECK(as_jit_load(&ctx, 0, UINT64_MAX - 3, 64) == -2);
    CHECK(as_jit_load(&ctx, 32, RAM_BASE, 64) != 0);
    CHECK(as_jit_load(&ctx, 0, RAM_BASE, 16) != 0);
    CHECK(as_jit_load(NULL, 0, RAM_BASE, 64) != 0);
    CHECK(as_jit_store(&ctx, 32, RAM_BASE, 64) != 0);
    CHECK(as_jit_store(&ctx, 0, RAM_BASE, 16) != 0);
    CHECK(as_jit_store(NULL, 0, RAM_BASE, 64) != 0);
}

static void emitter_tests(void)
{
    scenario = "emitter bounds and unsupported ASIR";
    uint8_t output[16384];
    ASTranslationBlock tb = {0};
    tb.guest_pc = 0x1000;
    tb.count = AS_TB_MAX_INSNS;
    for (size_t i = 0; i < tb.count; ++i) {
        tb.insn[i].pc = tb.guest_pc + i * 4;
        tb.insn[i].ir = (ASIR){.op = ASIR_MOV_IMM, .rd = 0, .width = 64, .imm = i};
    }
    ASJitCode jit;
    as_jit_init(&jit, output, sizeof(output));
    CHECK(as_jit_emit_tb_x86_64(&jit, &tb) == 0 && jit.size > 0);
    size_t required = jit.size;
    memset(output, 0xCC, sizeof(output));
    as_jit_init(&jit, output, required - 1);
    CHECK(as_jit_emit_tb_x86_64(&jit, &tb) != 0);
    CHECK(jit.size == 0 && output[required - 1] == 0xCC);
    memset(output, 0xCC, sizeof(output));
    as_jit_init(&jit, output, 0);
    CHECK(as_jit_emit_tb_x86_64(&jit, &tb) != 0 && output[0] == 0xCC);
    tb.count = 1;
    tb.insn[0].ir.op = (ASIROp)999;
    as_jit_init(&jit, output, sizeof(output));
    CHECK(as_jit_emit_tb_x86_64(&jit, &tb) == -2 && jit.size == 0);
    tb.insn[0].ir = (ASIR){.op = ASIR_ADD_REG, .rd = 0, .rn = 1,
                                .rm = 2, .width = 32, .shift = 32};
    as_jit_init(&jit, output, sizeof(output));
    CHECK(as_jit_emit_tb_x86_64(&jit, &tb) == -2 && jit.size == 0);
}

int main(void)
{
    helper_tests();
    emitter_tests();
    integer_tests();
    memory_tests();
    pair_tests();
    flag_tests();
    branch_tests();
    address_and_block_tests();
#if JIT_RUNTIME_TESTS
    puts("ASJIT v0.0.4 differential execution: PASS");
#else
    puts("ASJIT v0.0.4 emission/reference: PASS (runtime skipped on this host)");
#endif
    return 0;
}
