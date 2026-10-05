// SPDX-License-Identifier: BSD-3-Clause
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "asbridge/elf.h"

#define IMAGE_SIZE 512u
#define RAM_SIZE 128u
#define GUEST_BASE UINT64_C(0x400000)
#define PH0 64u
#define PH1 120u

typedef struct {
    uint8_t image[IMAGE_SIZE];
    uint8_t ram[RAM_SIZE];
    ASMemory memory;
} Fixture;

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (8 * i));
}

static void put64(uint8_t *p, uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i)
        p[i] = (uint8_t)(value >> (8 * i));
}

static void fixture_init(Fixture *f)
{
    memset(f->image, 0, sizeof(f->image));
    memset(f->ram, 0x5a, sizeof(f->ram));
    f->memory = (ASMemory){ .data = f->ram, .size = sizeof(f->ram),
                           .base = GUEST_BASE, .write_generation = 9 };
    uint8_t *b = f->image;
    b[0] = 0x7f; b[1] = 'E'; b[2] = 'L'; b[3] = 'F';
    b[4] = 2; b[5] = 1; b[6] = 1;
    put16(b + 16, 2);             /* ET_EXEC */
    put16(b + 18, 183);           /* EM_AARCH64 */
    put32(b + 20, 1);             /* EV_CURRENT */
    put64(b + 24, GUEST_BASE + 16);
    put64(b + 32, PH0);
    put16(b + 52, 64);
    put16(b + 54, 56);
    put16(b + 56, 2);
    put32(b + PH0, 1);            /* PT_LOAD */
    put32(b + PH0 + 4, 5);        /* PF_R | PF_X */
    put64(b + PH0 + 8, 256);
    put64(b + PH0 + 16, GUEST_BASE + 16);
    put64(b + PH0 + 32, 4);
    put64(b + PH0 + 40, 16);
    put64(b + PH0 + 48, 1);
    put32(b + PH1, 1);
    put32(b + PH1 + 4, 6);        /* PF_R | PF_W */
    put64(b + PH1 + 8, 272);
    put64(b + PH1 + 16, GUEST_BASE + 64);
    put64(b + PH1 + 32, 8);
    put64(b + PH1 + 40, 8);
    put64(b + PH1 + 48, 1);
    b[256] = 0x11; b[257] = 0x22; b[258] = 0x33; b[259] = 0x44;
    for (unsigned i = 0; i < 8; ++i)
        b[272 + i] = (uint8_t)(0xa0 + i);
}

static void assert_rejected(Fixture *f, size_t image_size)
{
    uint8_t before[RAM_SIZE];
    memcpy(before, f->ram, sizeof(before));
    uint64_t generation = f->memory.write_generation;
    ASELFImage out = { .entry = 11, .low = 22, .high = 33 };
    assert(as_elf_load(f->image, image_size, &f->memory, &out) < 0);
    assert(out.entry == 0 && out.low == 0 && out.high == 0);
    assert(memcmp(f->ram, before, sizeof(before)) == 0);
    assert(f->memory.write_generation == generation);
}

static void valid_and_bss_test(void)
{
    Fixture f;
    fixture_init(&f);
    ASELFImage out;
    assert(as_elf_load(f.image, sizeof(f.image), &f.memory, &out) == 0);
    assert(out.entry == GUEST_BASE + 16);
    assert(out.low == GUEST_BASE + 16 && out.high == GUEST_BASE + 72);
    assert(memcmp(f.ram + 16, f.image + 256, 4) == 0);
    for (unsigned i = 20; i < 32; ++i)
        assert(f.ram[i] == 0);
    assert(memcmp(f.ram + 64, f.image + 272, 8) == 0);
    assert(f.ram[0] == 0x5a && f.ram[15] == 0x5a);
    assert(f.ram[32] == 0x5a && f.ram[63] == 0x5a);
    assert(f.ram[72] == 0x5a && f.ram[127] == 0x5a);
    assert(f.memory.write_generation == 10);

    /* A wholly zero-filled segment can start at the end of the file. */
    fixture_init(&f);
    put64(f.image + PH1 + 8, sizeof(f.image));
    put64(f.image + PH1 + 32, 0);
    assert(as_elf_load(f.image, sizeof(f.image), &f.memory, &out) == 0);
    for (unsigned i = 64; i < 72; ++i)
        assert(f.ram[i] == 0);

    /* Empty PT_LOAD segments at the RAM end do not expand loaded bounds. */
    fixture_init(&f);
    put64(f.image + PH1 + 16, GUEST_BASE + sizeof(f.ram));
    put64(f.image + PH1 + 32, 0);
    put64(f.image + PH1 + 40, 0);
    assert(as_elf_load(f.image, sizeof(f.image), &f.memory, &out) == 0);
    assert(out.high == GUEST_BASE + 32);
}

static void invalid_header_test(void)
{
    const unsigned offsets[] = { 0, 4, 5, 6 };
    Fixture f;
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        fixture_init(&f);
        f.image[offsets[i]] = 0;
        assert_rejected(&f, sizeof(f.image));
    }
    const unsigned fields16[] = { 16, 18, 52, 54 };
    for (unsigned i = 0; i < sizeof(fields16) / sizeof(fields16[0]); ++i) {
        fixture_init(&f);
        put16(f.image + fields16[i], 1);
        assert_rejected(&f, sizeof(f.image));
    }
    fixture_init(&f);
    put32(f.image + 20, 0);
    assert_rejected(&f, sizeof(f.image));
    fixture_init(&f);
    put16(f.image + 16, 3);        /* ET_DYN is not relocated by this loader. */
    assert_rejected(&f, sizeof(f.image));
    fixture_init(&f);
    put16(f.image + 52, 65);
    assert_rejected(&f, sizeof(f.image));
    fixture_init(&f);
    put16(f.image + 54, 57);
    assert_rejected(&f, sizeof(f.image));
}

static void truncated_and_file_range_test(void)
{
    Fixture f;
    fixture_init(&f);
    assert_rejected(&f, 63);
    assert_rejected(&f, PH1 + 55);
    assert_rejected(&f, 279);      /* The second segment's data is truncated. */
    fixture_init(&f);
    put64(f.image + 32, UINT64_MAX - 16);
    assert_rejected(&f, sizeof(f.image));
    fixture_init(&f);
    put16(f.image + 56, UINT16_MAX);
    assert_rejected(&f, sizeof(f.image));
    fixture_init(&f);
    put64(f.image + PH1 + 8, IMAGE_SIZE);
    assert_rejected(&f, sizeof(f.image));
    fixture_init(&f);
    put64(f.image + PH1 + 8, UINT64_MAX - 3);
    assert_rejected(&f, sizeof(f.image));
    fixture_init(&f);
    put64(f.image + PH1 + 32, 9);  /* filesz must not exceed memsz. */
    assert_rejected(&f, sizeof(f.image));
    fixture_init(&f);
    put64(f.image + PH1 + 32, UINT64_MAX);
    put64(f.image + PH1 + 40, UINT64_MAX);
    assert_rejected(&f, sizeof(f.image));
}

static void guest_range_test(void)
{
    const uint64_t addresses[] = {
        GUEST_BASE - 1, GUEST_BASE + RAM_SIZE - 4,
        GUEST_BASE + RAM_SIZE + 1, UINT64_MAX - 3
    };
    Fixture f;
    for (unsigned i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        fixture_init(&f);
        put64(f.image + PH1 + 16, addresses[i]);
        assert_rejected(&f, sizeof(f.image));
    }
    fixture_init(&f);
    put64(f.image + PH1 + 40, UINT64_MAX);
    assert_rejected(&f, sizeof(f.image));

    /* The offset fits RAM, but the exclusive virtual end cannot be represented. */
    fixture_init(&f);
    f.memory.base = UINT64_MAX - 127;
    put64(f.image + PH0 + 16, f.memory.base + 16);
    put64(f.image + 24, f.memory.base + 16);
    put64(f.image + PH1 + 16, UINT64_MAX - 7);
    assert_rejected(&f, sizeof(f.image));
}

static void executable_entry_test(void)
{
    const uint64_t entries[] = {
        GUEST_BASE + 48,          /* Hole between PT_LOAD segments. */
        GUEST_BASE + 64,          /* Loaded data without PF_X. */
        GUEST_BASE + 72,          /* Exclusive loaded high boundary. */
        GUEST_BASE + 128
    };
    Fixture f;
    for (unsigned i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
        fixture_init(&f);
        put64(f.image + 24, entries[i]);
        assert_rejected(&f, sizeof(f.image));
    }
    fixture_init(&f);
    put32(f.image + PH0 + 4, 4);
    assert_rejected(&f, sizeof(f.image));
    fixture_init(&f);
    put32(f.image + PH0, 0);
    put32(f.image + PH1, 0);
    assert_rejected(&f, sizeof(f.image));
    fixture_init(&f);
    put16(f.image + 56, 0);
    assert_rejected(&f, sizeof(f.image));
    fixture_init(&f);
    put64(f.image + PH0 + 32, 0);
    put64(f.image + PH0 + 40, 0);
    assert_rejected(&f, sizeof(f.image));
}

static void invalid_arguments_and_alias_test(void)
{
    Fixture f;
    fixture_init(&f);
    ASELFImage out = { .entry = 11, .low = 22, .high = 33 };
    assert(as_elf_load(NULL, sizeof(f.image), &f.memory, &out) < 0);
    assert(out.entry == 0 && out.low == 0 && out.high == 0);
    assert(as_elf_load(f.image, sizeof(f.image), NULL, &out) < 0);
    assert(as_elf_load(f.image, sizeof(f.image), &f.memory, NULL) < 0);
    f.memory.data = NULL;
    assert(as_elf_load(f.image, sizeof(f.image), &f.memory, &out) < 0);
    assert(out.entry == 0 && out.low == 0 && out.high == 0);

    uint8_t before[IMAGE_SIZE];
    memcpy(before, f.image, sizeof(before));
    f.memory.data = f.image + 256;
    assert(as_elf_load(f.image, sizeof(f.image), &f.memory, &out) < 0);
    assert(memcmp(before, f.image, sizeof(before)) == 0);
    assert(f.memory.write_generation == 9);
    assert(out.entry == 0 && out.low == 0 && out.high == 0);
}

int main(void)
{
    valid_and_bss_test();
    invalid_header_test();
    truncated_and_file_range_test();
    guest_range_test();
    executable_entry_test();
    invalid_arguments_and_alias_test();
    puts("ASCore ELF loader: PASS");
    return 0;
}
