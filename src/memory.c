// SPDX-License-Identifier: BSD-3-Clause
#include "asbridge/memory.h"

static int memory_offset(const ASMemory *m, uint64_t address, size_t bytes, size_t *offset)
{
    if (!m || !m->data || address < m->base ||
        address > UINT64_MAX - (bytes - 1)) return -1;
    uint64_t delta = address - m->base;
    if (delta > m->size || bytes > m->size - (size_t)delta) return -1;
    *offset = (size_t)delta;
    return 0;
}

int as_mem_read32(const ASMemory *m, uint64_t address, uint32_t *value)
{
    size_t offset;
    if (!value || memory_offset(m, address, 4, &offset)) return -1;
    uint32_t result = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
        result |= (uint32_t)m->data[offset + byte] << (byte * 8);
    }
    *value = result;
    return 0;
}

int as_mem_write32(ASMemory *m, uint64_t address, uint32_t value)
{
    size_t offset;
    if (memory_offset(m, address, 4, &offset)) return -1;
    for (unsigned byte = 0; byte < 4; ++byte) {
        m->data[offset + byte] = (uint8_t)(value >> (byte * 8));
    }
    ++m->write_generation;
    return 0;
}

int as_mem_read64(const ASMemory *m, uint64_t address, uint64_t *value)
{
    size_t offset;
    if (!value || memory_offset(m, address, 8, &offset)) return -1;
    uint64_t result = 0;
    for (unsigned byte = 0; byte < 8; ++byte) {
        result |= (uint64_t)m->data[offset + byte] << (byte * 8);
    }
    *value = result;
    return 0;
}

int as_mem_write64(ASMemory *m, uint64_t address, uint64_t value)
{
    size_t offset;
    if (memory_offset(m, address, 8, &offset)) return -1;
    for (unsigned byte = 0; byte < 8; ++byte) {
        m->data[offset + byte] = (uint8_t)(value >> (byte * 8));
    }
    ++m->write_generation;
    return 0;
}
