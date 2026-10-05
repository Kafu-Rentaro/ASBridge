.global _start
.extern entry_int
.text
_start:
    bl entry_int
    brk #0
