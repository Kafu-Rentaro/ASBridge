# ASBridge bootstrap guest ABI

v0.0.3 intentionally defines only a tiny bring-up ABI.

- Guest architecture: AArch64 little-endian ELF64.
- The ELF entry point becomes the initial guest PC.
- PT_LOAD segments are copied into bounded guest RAM and BSS tails are zero-filled.
- `BRK #imm` stops the bootstrap interpreter and records the immediate.
- General-purpose register `x0` is reported by the `asrun` utility.

This is a test ABI, not a Linux ABI or an Apple platform ABI. It exists only to make independently compiled ARM64 payloads reproducible during early ASCore development.
