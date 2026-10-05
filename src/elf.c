// SPDX-License-Identifier: BSD-3-Clause
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "asbridge/elf.h"

#define ELF64_HEADER_SIZE 64u
#define ELF64_PROGRAM_HEADER_SIZE 56u
#define ET_EXEC 2u
#define EM_AARCH64 183u
#define EV_CURRENT 1u
#define PT_LOAD 1u
#define PF_X 1u

typedef struct {
    uint32_t type, flags;
    uint64_t offset, vaddr, filesz, memsz;
} ELFSegment;

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t read64(const uint8_t *p)
{
    return (uint64_t)read32(p) | ((uint64_t)read32(p + 4) << 32);
}

static ELFSegment read_segment(const uint8_t *p)
{
    ELFSegment segment = {
        .type = read32(p), .flags = read32(p + 4),
        .offset = read64(p + 8), .vaddr = read64(p + 16),
        .filesz = read64(p + 32), .memsz = read64(p + 40)
    };
    return segment;
}

static int file_range(size_t size, uint64_t offset, uint64_t length)
{
    return offset <= size && length <= size - (size_t)offset;
}

static int ranges_overlap(const void *a, size_t a_size,
                          const void *b, size_t b_size)
{
    uintptr_t a_addr = (uintptr_t)a, b_addr = (uintptr_t)b;
    if (!a_size || !b_size)
        return 0;
    return a_addr <= b_addr ? b_addr - a_addr < a_size
                            : a_addr - b_addr < b_size;
}

int as_elf_load(const void *image, size_t size, ASMemory *mem, ASELFImage *out)
{
    if (out)
        memset(out, 0, sizeof(*out));
    if (!image || !mem || !mem->data || !out || size < ELF64_HEADER_SIZE)
        return -1;

    /* The validation/copy passes require an immutable source image. */
    if (ranges_overlap(image, size, mem->data, mem->size))
        return -1;

    const uint8_t *bytes = image;
    if (bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' ||
        bytes[3] != 'F' || bytes[4] != 2 || bytes[5] != 1 ||
        bytes[6] != EV_CURRENT || read16(bytes + 16) != ET_EXEC ||
        read16(bytes + 18) != EM_AARCH64 ||
        read32(bytes + 20) != EV_CURRENT ||
        read16(bytes + 52) != ELF64_HEADER_SIZE)
        return -2;

    uint64_t entry = read64(bytes + 24), phoff = read64(bytes + 32);
    uint16_t phentsize = read16(bytes + 54), phnum = read16(bytes + 56);
    if (phentsize != ELF64_PROGRAM_HEADER_SIZE ||
        !file_range(size, phoff, (uint64_t)phentsize * phnum))
        return -3;

    ASELFImage loaded = { .entry = entry, .low = UINT64_MAX, .high = 0 };
    int executable_entry = 0;

    /* Validate every segment before modifying guest RAM or output metadata. */
    for (uint16_t n = 0; n < phnum; ++n) {
        const uint8_t *ph = bytes + (size_t)phoff + (size_t)n * phentsize;
        ELFSegment segment = read_segment(ph);
        if (segment.type != PT_LOAD)
            continue;
        if (segment.filesz > segment.memsz ||
            !file_range(size, segment.offset, segment.filesz))
            return -4;
        if (segment.memsz > UINT64_MAX - segment.vaddr ||
            segment.vaddr < mem->base)
            return -5;
        uint64_t offset = segment.vaddr - mem->base;
        if (offset > mem->size || segment.memsz > mem->size - (size_t)offset)
            return -5;
        if (!segment.memsz)
            continue;

        uint64_t end = segment.vaddr + segment.memsz;
        if (segment.vaddr < loaded.low)
            loaded.low = segment.vaddr;
        if (end > loaded.high)
            loaded.high = end;
        if ((segment.flags & PF_X) && entry >= segment.vaddr && entry < end)
            executable_entry = 1;
    }
    if (!executable_entry)
        return -6;

    for (uint16_t n = 0; n < phnum; ++n) {
        const uint8_t *ph = bytes + (size_t)phoff + (size_t)n * phentsize;
        ELFSegment segment = read_segment(ph);
        if (segment.type != PT_LOAD || !segment.memsz)
            continue;
        size_t offset = (size_t)(segment.vaddr - mem->base);
        memcpy(mem->data + offset, bytes + (size_t)segment.offset,
               (size_t)segment.filesz);
        memset(mem->data + offset + (size_t)segment.filesz, 0,
               (size_t)(segment.memsz - segment.filesz));
    }
    ++mem->write_generation;
    *out = loaded;
    return 0;
}
