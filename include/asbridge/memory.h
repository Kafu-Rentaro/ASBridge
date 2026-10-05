// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_MEMORY_H
#define ASBRIDGE_MEMORY_H
#include <stddef.h>
#include <stdint.h>
typedef struct ASMemory { uint8_t *data; size_t size; uint64_t base; } ASMemory;
int as_mem_read64(const ASMemory *m, uint64_t addr, uint64_t *out);
int as_mem_write64(ASMemory *m, uint64_t addr, uint64_t value);
#endif
