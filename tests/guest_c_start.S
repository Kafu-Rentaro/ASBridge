.global _start
.extern entry
.text
_start:
    bl entry
    brk #0
