# AGENTS.md — Codex development guide for ASBridge

## Mission
ASBridge is a clean-room ARM64 execution / dynamic binary translation environment for x86-64. Keep ASCore generic. The interpreter is the architectural reference; the x86-64 JIT must consume the same ASIR.

Current development line: **v0.0.4 — translation blocks and baseline x86-64 JIT**.

P0-P5 are implemented on this line. Re-run the validation gates below before
proposing merge. Native ELF execution is available through `asrun --jit`, explicit
fallback through `--jit-fallback`, and full interpreter/JIT state comparison
through `--compare`. Compile and compare all guests with
`bash tests/check_guests.sh build` on x86-64 Linux with Clang/LLD.
The next platform milestones are documented in `docs/ROADMAP.md`; OS boot and GPU
acceleration remain future work.

## Non-negotiable clean-room rules
- Implement ARM64 behavior primarily from public architecture documentation.
- Do not copy source code from QEMU, m1n1, XNU, Apple proprietary software, or other license-incompatible implementations.
- External implementations may be used only as behavioral/interoperability references.
- Never commit Apple binaries, firmware, keys, kernelcaches, iBoot images, proprietary DeviceTrees, or other copyrighted Apple payloads.
- Keep Apple/platform-personality work out of ASCore.
- New ASBridge C/C header/assembly source should carry `SPDX-License-Identifier: BSD-3-Clause`.
- Record any incorporated third-party material in `THIRD_PARTY.md`.

## Architecture
Pipeline:
```
AArch64 guest bytes
 -> decoder
 -> ASIR
 -> translation block (TB)
 -> interpreter OR x86-64 JIT
 -> ASCPU / ASMemory state
```

Important invariants:
1. Do not create a second ARM64 decoder in the JIT.
2. JIT lowering operates on ASIR only.
3. Interpreter behavior is the reference behavior.
4. A JIT operation is not complete until differential tests compare interpreter and JIT state.
5. TBs end at control-flow or HALT operations.
   Stores also end runtime-built TBs so guest code modifications take effect
   before the next instruction is translated or fetched from the cache.
6. Keep executable-memory allocation separate from byte emission.
7. Prefer correctness and explicit state updates over optimization in v0.0.4.

## Current important files
- `include/asbridge/cpu.h` — architectural CPU state.
- `include/asbridge/ir.h` — ASIR.
- `src/decode.c` — ARM64 -> ASIR.
- `src/interpreter.c` — reference execution.
- `include/asbridge/tb.h`, `src/tb.c` — translation blocks.
- `include/asbridge/jit.h`, `src/jit_x86_64.c` — baseline x86-64 JIT.
- `include/asbridge/jit_runtime.h`, `src/jit_runtime.c` — native dispatch/cache.
- `include/asbridge/memory.h`, `src/memory.c` — bounded guest RAM.
- `src/elf.c` — bounded ELF64/AArch64 loader.
- `tests/test_core.c` — unit/differential tests.
- `tests/test_{reference,jit,elf,runtime}.c` — focused correctness/differential tests.
- `tests/guest_*.{c,S}` — independently compiled AArch64 payloads.
- `docs/JIT.md` — JIT design notes.

## v0.0.4 JIT ABI
`ASJitContext` owns pointers to `ASCPU`, `ASMemory`, and a fault result. Memory helpers must preserve bounds checking; generated code must not directly dereference arbitrary guest addresses.

Baseline strategy:
- Keep canonical guest registers in `ASCPU.x[]`.
- Use host scratch registers temporarily.
- W-register writes must zero-extend into the corresponding X register.
- XZR reads as zero and discards writes.
- SP semantics must remain distinct from XZR semantics.
- Do not add a register allocator yet.

## Immediate work queue
Work in this order unless a failing test requires an earlier fix:

### P0 — keep CI green
1. Run CMake build and CTest.
2. Build all AArch64 guest payloads used by CI.
3. Fix ELF/link layout or loader correctness without weakening guest-memory bounds.
4. Remove stale enum names and compiler warnings encountered in touched code.
5. Do not claim a test passed unless it was actually run.

### P1 — JIT memory lowering
Implement generated x86-64 lowering for ASIR `LOAD` / `STORE` through safe helpers.
- Generated code receives an `ASJitContext *`.
- Compute ARM effective address according to ASIR/SP semantics.
- Call `as_jit_load` / `as_jit_store` using the host ABI.
- Propagate helper failure to `ctx->fault` and exit the TB.
- Cover W and X accesses.
- Add interpreter/JIT differential tests for successful access and out-of-range fault.

### P2 — stack frames
Lower 64-bit `STP/LDP`, including offset/pre-index/post-index and SP writeback.
Add a differential test for the canonical:
```asm
stp x29, x30, [sp, #-16]!
ldp x29, x30, [sp], #16
```

### P3 — integer lowering completeness
Complete/test:
- MOVZ / MOVK, W and X
- ADD/SUB immediate, W and X, including correct SP forms
- ADD/SUB register
- AND/ORR/EOR
- MUL
- SUBS/CMP and NZCV
- CBZ/CBNZ
- B.cond
- B/BL/BR/RET
- ADR/ADRP as needed by compiled payloads

Do not silently approximate unsupported shift forms. Return unsupported until semantics are implemented.

### P4 — execute compiled C under JIT
Add a JIT runner path that:
1. loads the existing AArch64 ELF with the same ELF loader;
2. initializes CPU/SP identically to interpreter execution;
3. builds/looks up TBs by guest PC;
4. executes JIT-supported TBs;
5. initially permits a clearly marked interpreter fallback for unsupported ASIR;
6. verifies existing C guests return the same X0 and architectural state.

Target payloads:
- simple add: expected X0 = 300 / 0x12c;
- 32-bit integer payload: expected X0 = 48 / 0x30;
- then a loop/factorial payload to force conditional control flow and MUL.

### P5 — code cache
After P1-P4 are correct:
- add a simple guest-PC keyed TB/code cache;
- track emitted code size;
- invalidate explicitly rather than assuming guest code is immutable;
- no advanced chaining/register caching yet.

## Required validation
For ordinary changes:
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

For guest/compiler changes, reproduce the commands in `.github/workflows/ci.yml`.

When changing the decoder or JIT:
- add focused unit tests;
- add interpreter/JIT differential tests;
- test W zero-extension where applicable;
- test XZR vs SP behavior where register 31 is involved;
- test bounds/fault behavior for memory operations.

## Coding constraints
- C17.
- LLVM/Clang primary; GCC compatibility desirable.
- Keep warnings visible; do not suppress a warning instead of fixing its cause.
- Avoid undefined behavior in shifts, signed overflow, pointer arithmetic, and integer narrowing.
- Check arithmetic before address/range addition to avoid overflow.
- Guest endianness is currently little-endian; do not accidentally make correctness depend on an undocumented host-endian assumption.
- Keep changes small enough that a regression can be bisected.

## Known technical debt to fix when touched
- Harden `src/elf.c`: validate ELF versions, avoid `vaddr + memsz` overflow, use safe guest-range checks, add valid/BSS/out-of-range synthetic ELF tests.
- Harden interpreter PC bounds against arithmetic overflow.
- Clean misleading-indentation warnings in the ELF loader.
- Remove unused JIT helpers after replacement.
- Keep README status/version synchronized with the actual milestone.
- CI's freestanding ELF link layout must stay inside configured guest RAM; do not solve this by disabling loader bounds checks.

## Commit / PR discipline
- Work on `dev/v0.0.4` for this milestone.
- Keep PR #3 focused on TB/JIT work.
- Prefer descriptive commits such as `feat(jit): lower ASIR load/store via safe helpers`.
- Never commit generated ELF/object/build output.
- Before proposing merge, require green unit tests and guest payload tests, and summarize any interpreter fallback still present.

## Definition of done for v0.0.4
v0.0.4 is ready to merge only when:
- TB construction is stable;
- baseline x86-64 machine code executes real ASIR operations;
- W/X integer semantics are differential-tested;
- safe guest memory accesses work from JIT code;
- basic stack frame operations work;
- at least the existing simple compiled C payload executes through the JIT path and matches the interpreter;
- CI is green on supported runners;
- no Apple-specific code or proprietary material has entered ASCore.

Optimization belongs after correctness. Do not introduce a register allocator, direct guest-memory pointer fast path, TB chaining, or platform personality work merely to make benchmarks faster during this milestone.
