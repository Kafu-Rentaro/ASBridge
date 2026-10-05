// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_JIT_RUNTIME_H
#define ASBRIDGE_JIT_RUNTIME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "asbridge/cpu.h"
#include "asbridge/memory.h"

typedef struct ASJitCacheEntry ASJitCacheEntry;
typedef struct ASJitCache {
    ASJitCacheEntry *entries;
    size_t capacity;
    size_t next_slot;
    size_t code_bytes;
    uint64_t compilations;
    uint64_t hits;
    uint64_t executions;
    uint64_t fallback_steps;
    const ASMemory *memory;
    const uint8_t *memory_data;
    size_t memory_size;
    uint64_t memory_base;
    uint64_t memory_generation;
} ASJitCache;

bool as_jit_runtime_available(void);
int as_jit_cache_init(ASJitCache *cache, size_t capacity);
void as_jit_cache_invalidate(ASJitCache *cache);
void as_jit_cache_destroy(ASJitCache *cache);
/* 0: halted, 1: budget exhausted, -1: unsupported instruction,
 * -2: guest memory fault, -4: unavailable native runtime, -5: host allocation.
 * Direct writes to RAM require invalidation; instruction bytes are also checked
 * before reuse. Stores terminate TBs, and helper stores invalidate via generation.
 */
int as_jit_run(ASCPU *cpu, ASMemory *mem, ASJitCache *cache,
               uint64_t max_steps, bool allow_fallback);
#endif
