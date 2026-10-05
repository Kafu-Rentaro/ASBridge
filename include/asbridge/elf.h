// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_ELF_H
#define ASBRIDGE_ELF_H
#include <stddef.h>
#include <stdint.h>
#include "asbridge/memory.h"
typedef struct ASELFImage { uint64_t entry; uint64_t low; uint64_t high; } ASELFImage;
int as_elf_load(const void *image, size_t image_size, ASMemory *mem, ASELFImage *out);
#endif
