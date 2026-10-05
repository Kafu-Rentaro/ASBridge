// SPDX-License-Identifier: BSD-3-Clause
#ifndef ASBRIDGE_DECODE_H
#define ASBRIDGE_DECODE_H
#include <stdbool.h>
#include <stdint.h>
#include "asbridge/ir.h"
bool as_decode_ir(uint32_t insn, ASIR *out);
#endif
