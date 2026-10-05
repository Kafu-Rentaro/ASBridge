// SPDX-License-Identifier: BSD-3-Clause
.global _start
.extern entry_loop
.text
_start:
    bl entry_loop
    brk #0
