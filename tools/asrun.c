// SPDX-License-Identifier: BSD-3-Clause
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asbridge/cpu.h"
#include "asbridge/elf.h"
#include "asbridge/interpreter.h"
#include "asbridge/jit_runtime.h"

static bool same_cpu(const ASCPU *a, const ASCPU *b) {
    return !memcmp(a->x, b->x, sizeof(a->x)) && a->sp == b->sp && a->pc == b->pc &&
           a->nzcv == b->nzcv && a->halted == b->halted && a->halt_imm == b->halt_imm;
}

int main(int argc, char **argv) {
    bool jit = false, compare = false, fallback = false;
    const char *path;
    if (argc == 2) path = argv[1];
    else if (argc == 3 && (!strcmp(argv[1], "--jit") ||
             !strcmp(argv[1], "--jit-fallback") || !strcmp(argv[1], "--compare"))) {
        path = argv[2];
        jit = true;
        compare = !strcmp(argv[1], "--compare");
        fallback = !strcmp(argv[1], "--jit-fallback");
    } else {
        fprintf(stderr, "usage: asrun [--jit|--jit-fallback|--compare] guest.elf\n");
        return 2;
    }
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); return 2; }
    if (fseek(file, 0, SEEK_END)) { fclose(file); return 2; }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET)) { fclose(file); return 2; }
    const size_t ram_size = 64u * 1024u * 1024u;
    unsigned char *image = malloc((size_t)length), *ram = calloc(1, ram_size);
    if (!image || !ram) { fclose(file); free(image); free(ram); return 2; }
    if (fread(image, 1, (size_t)length, file) != (size_t)length) {
        fclose(file); free(image); free(ram); return 2;
    }
    fclose(file);
    ASMemory memory = {.data = ram, .size = ram_size, .base = 0x400000u};
    ASELFImage elf;
    int result = as_elf_load(image, (size_t)length, &memory, &elf);
    free(image);
    if (result) { fprintf(stderr, "ELF load failed: %d\n", result); free(ram); return 1; }
    ASCPU cpu;
    as_cpu_reset(&cpu, elf.entry);
    cpu.sp = memory.base + memory.size - 16;
    ASJitCache cache = {0};
    unsigned char *reference_ram = NULL;
    ASCPU reference;
    int reference_result = 0;
    if (compare) {
        reference_ram = malloc(ram_size);
        if (!reference_ram) { free(ram); return 2; }
        memcpy(reference_ram, ram, ram_size);
        reference = cpu;
        ASMemory refmem = memory;
        refmem.data = reference_ram;
        reference_result = as_run(&reference, &refmem, 1000000);
    }
    if (jit) {
        result = as_jit_cache_init(&cache, 128);
        if (!result) result = as_jit_run(&cpu, &memory, &cache, 1000000, fallback);
    } else result = as_run(&cpu, &memory, 1000000);
    if (compare && (reference_result != result || !same_cpu(&cpu, &reference) ||
                    memcmp(ram, reference_ram, ram_size))) {
        fprintf(stderr, "Interpreter/JIT state mismatch: ref=%d jit=%d ref_pc=0x%llx jit_pc=0x%llx\n",
                reference_result, result, (unsigned long long)reference.pc, (unsigned long long)cpu.pc);
        result = -6;
    }
    if (result) {
        uint32_t raw;
        fprintf(stderr, "execution failed: %d pc=0x%llx", result, (unsigned long long)cpu.pc);
        if (!as_mem_read32(&memory, cpu.pc, &raw)) fprintf(stderr, " instruction=0x%08x", raw);
        fprintf(stderr, "\n");
    } else {
        printf("halt imm=%u x0=0x%llx sp=0x%llx\n", cpu.halt_imm,
               (unsigned long long)cpu.x[0], (unsigned long long)cpu.sp);
        if (jit) printf("jit compiled=%llu hits=%llu executed=%llu fallback=%llu code_bytes=%zu%s\n",
                        (unsigned long long)cache.compilations, (unsigned long long)cache.hits,
                        (unsigned long long)cache.executions, (unsigned long long)cache.fallback_steps,
                        cache.code_bytes, compare ? " state=match" : "");
    }
    as_jit_cache_destroy(&cache);
    free(reference_ram);
    free(ram);
    return result ? 1 : 0;
}
