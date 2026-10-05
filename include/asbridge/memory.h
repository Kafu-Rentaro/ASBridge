// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_MEMORY_H
#define ASBRIDGE_MEMORY_H
#include <stddef.h>
#include <stdint.h>
/* Successful stores conservatively invalidate translations through this counter.
 * Direct data modifications require explicit cache invalidation or validation. */
typedef struct ASMemory { uint8_t *data; size_t size; uint64_t base; uint64_t write_generation; } ASMemory;
int as_mem_read32(const ASMemory *m,uint64_t addr,uint32_t *out);
int as_mem_write32(ASMemory *m,uint64_t addr,uint32_t value);
int as_mem_read64(const ASMemory *m,uint64_t addr,uint64_t *out);
int as_mem_write64(ASMemory *m,uint64_t addr,uint64_t value);
#endif
