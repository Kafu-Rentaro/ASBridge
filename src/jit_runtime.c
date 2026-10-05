// SPDX-License-Identifier: BSD-3-Clause
#define _DEFAULT_SOURCE
#include <stdlib.h>
#include <string.h>
#if defined(__x86_64__) && (defined(__unix__) || defined(__APPLE__))
#include <sys/mman.h>
#include <unistd.h>
#define AS_NATIVE_JIT 1
#else
#define AS_NATIVE_JIT 0
#endif
#include "asbridge/interpreter.h"
#include "asbridge/jit.h"
#include "asbridge/jit_runtime.h"

typedef void (*ASJitFunction)(ASJitContext *);
struct ASJitCacheEntry {
    ASTranslationBlock tb;
    void *executable;
    size_t mapping_size;
    size_t code_size;
    ASJitFunction function;
};

bool as_jit_runtime_available(void) { return AS_NATIVE_JIT != 0; }

static void release_entry(ASJitCacheEntry *entry) {
#if AS_NATIVE_JIT
    if (entry->executable) munmap(entry->executable, entry->mapping_size);
#endif
    memset(entry, 0, sizeof(*entry));
}

int as_jit_cache_init(ASJitCache *cache, size_t capacity) {
    if (!cache || !capacity || capacity > SIZE_MAX / sizeof(ASJitCacheEntry)) return -5;
    memset(cache, 0, sizeof(*cache));
    cache->entries = calloc(capacity, sizeof(*cache->entries));
    if (!cache->entries) return -5;
    cache->capacity = capacity;
    return 0;
}

void as_jit_cache_invalidate(ASJitCache *cache) {
    if (!cache) return;
    for (size_t k = 0; k < cache->capacity; ++k) release_entry(&cache->entries[k]);
    cache->next_slot = 0;
    cache->code_bytes = 0;
    cache->memory = NULL;
}

void as_jit_cache_destroy(ASJitCache *cache) {
    if (!cache) return;
    as_jit_cache_invalidate(cache);
    free(cache->entries);
    memset(cache, 0, sizeof(*cache));
}

/* Executable allocation is deliberately independent of ASIR byte emission. */
static int make_executable(ASJitCacheEntry *entry, const ASJitCode *code) {
#if AS_NATIVE_JIT
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0 || code->size > SIZE_MAX - ((size_t)page - 1)) return -5;
    size_t size = ((code->size + (size_t)page - 1) / (size_t)page) * (size_t)page;
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (ptr == MAP_FAILED) return -5;
    memcpy(ptr, code->data, code->size);
    if (mprotect(ptr, size, PROT_READ | PROT_EXEC)) {
        munmap(ptr, size);
        return -5;
    }
    _Static_assert(sizeof(ASJitFunction) == sizeof(void *), "native function pointer size");
    memcpy(&entry->function, &ptr, sizeof(ptr));
    entry->executable = ptr;
    entry->mapping_size = size;
    entry->code_size = code->size;
    return 0;
#else
    (void)entry;
    (void)code;
    return -4;
#endif
}

static int compile_entry(ASJitCacheEntry *entry) {
    size_t capacity = AS_TB_MAX_INSNS * 1024u;
    uint8_t *bytes = malloc(capacity);
    if (!bytes) return -5;
    ASJitCode code;
    as_jit_init(&code, bytes, capacity);
    int result = as_jit_emit_tb_x86_64(&code, &entry->tb);
    if (result == -2) result = -1;
    else if (result) result = -5;
    else result = make_executable(entry, &code);
    free(bytes);
    return result;
}

static void bind_memory(ASJitCache *cache, const ASMemory *mem) {
    if (cache->memory != mem || cache->memory_data != mem->data ||
        cache->memory_size != mem->size || cache->memory_base != mem->base ||
        cache->memory_generation != mem->write_generation) {
        as_jit_cache_invalidate(cache);
        cache->memory = mem;
        cache->memory_data = mem->data;
        cache->memory_size = mem->size;
        cache->memory_base = mem->base;
        cache->memory_generation = mem->write_generation;
    }
}

static bool bytes_match(const ASJitCacheEntry *entry, const ASMemory *mem) {
    for (size_t k = 0; k < entry->tb.count; ++k) {
        uint32_t raw;
        if (as_mem_read32(mem, entry->tb.insn[k].pc, &raw) || raw != entry->tb.insn[k].raw)
            return false;
    }
    return true;
}

static int lookup(ASJitCache *cache, ASMemory *mem, uint64_t pc, ASJitCacheEntry **out) {
    bind_memory(cache, mem);
    for (size_t k = 0; k < cache->capacity; ++k) {
        ASJitCacheEntry *entry = &cache->entries[k];
        if (!entry->executable || entry->tb.guest_pc != pc) continue;
        if (bytes_match(entry, mem)) {
            ++cache->hits;
            *out = entry;
            return 0;
        }
        cache->code_bytes -= entry->code_size;
        release_entry(entry);
    }
    ASJitCacheEntry fresh = {0};
    uint32_t first;
    if (as_mem_read32(mem, pc, &first)) return -2;
    int result = as_tb_build(&fresh.tb, mem->data, mem->size, mem->base, pc);
    if (result) return result == -3 ? -1 : -2;
    result = compile_entry(&fresh);
    if (result) return result;
    ASJitCacheEntry *entry = &cache->entries[cache->next_slot];
    cache->next_slot = (cache->next_slot + 1) % cache->capacity;
    cache->code_bytes -= entry->code_size;
    release_entry(entry);
    *entry = fresh;
    cache->code_bytes += entry->code_size;
    ++cache->compilations;
    *out = entry;
    return 0;
}

int as_jit_run(ASCPU *cpu, ASMemory *mem, ASJitCache *cache,
               uint64_t max_steps, bool allow_fallback) {
    if (!cpu || !mem || !mem->data || !cache || !cache->entries) return -2;
    if (!as_jit_runtime_available()) return -4;
    while (!cpu->halted && max_steps) {
        ASJitCacheEntry *entry;
        int result = lookup(cache, mem, cpu->pc, &entry);
        if (result) {
            if (result != -1 || !allow_fallback) return result;
            uint32_t raw;
            if (as_mem_read32(mem, cpu->pc, &raw)) return -2;
            result = as_step(cpu, mem, raw);
            ++cache->fallback_steps;
            if (result) return result;
            --max_steps;
            continue;
        }
        ASJitCacheEntry partial = {0};
        ASJitCacheEntry *active = entry;
        if (entry->tb.count > max_steps) {
            partial.tb = entry->tb;
            partial.tb.count = (size_t)max_steps;
            result = compile_entry(&partial);
            if (result) return result;
            active = &partial;
        }
        size_t count = active->tb.count;
        ASJitContext ctx = {cpu, mem, 0};
        active->function(&ctx);
        ++cache->executions;
        if (active == &partial) release_entry(&partial);
        if (ctx.fault) return ctx.fault;
        max_steps -= count;
    }
    return cpu->halted ? 0 : 1;
}
