# ASCore v0.0.4 JIT design

The interpreter is the architectural reference. Native execution follows one shared pipeline:

```text
AArch64 guest bytes -> decoder -> ASIR -> translation block -> x86-64 emitter -> executable cache
```

The emitter lowers ASIR into real x86-64 machine code and updates canonical `ASCPU` state. It does not decode guest instructions or use a register allocator. Each TB retains guest PCs and raw instruction words, holds at most 64 instructions, and ends at control flow, HALT, or a store. Unsupported decoding can leave a valid prefix block for execution before the unsupported instruction is reached.

## Supported forms

The following subset is available in the decoder, interpreter, and JIT. This is not full AArch64 instruction coverage.

| Family | Implemented forms |
| --- | --- |
| Constants | `MOVZ`, `MOVK`, W and X |
| Integer arithmetic | `ADD`/`SUB` immediate and LSL register forms, W and X; immediate SP forms |
| Flags | `SUBS`/`CMP` immediate and LSL register forms with NZCV |
| Logical operations | `AND`/`ORR`/`EOR` logical immediate and LSL register forms, W and X |
| Multiply | `MUL`, W and X |
| Single memory access | Integer `LDR`/`STR`, W and X; unsigned offset, pre-index, and post-index |
| Paired memory access | Integer 64-bit `LDP`/`STP`; signed offset, pre-index, and post-index, including SP stack frames |
| Control flow | `B`, `BL`, `BR`, `RET`, `CBZ`/`CBNZ`, `B.cond` |
| PC-relative address | `ADR`, `ADRP` |
| Runner control | `NOP`; `BRK` decoded as the current runner's HALT operation |

Register 31 is interpreted separately as SP or XZR according to the instruction form. XZR reads zero and discards writes; W writes zero-extend into the corresponding X register. Unsupported shifts, extended-register forms, constrained overlapping writeback operands, and other absent forms are rejected rather than approximated. `BRK` currently stops the runner; architectural exception delivery remains future work.

## Context ABI and safe memory

Generated functions have signature `void (ASJitContext *)` and follow the System V x86-64 ABI. The context carries pointers to `ASCPU` and `ASMemory`, plus a fault result. R12 retains the context, R13 retains the CPU, and R14 preserves a memory instruction's original base across helper calls. Preserved registers and call-stack alignment follow the host ABI.

Arithmetic and branches update CPU state directly. Memory operations compute a guest effective address and call `as_jit_load`, `as_jit_store`, or their 64-bit pair equivalents. Helpers use bounded little-endian guest-memory access; emitted code never dereferences an arbitrary guest address. The guest PC is set to the memory instruction before the helper call. A failed helper records the fault and exits the TB before base writeback or subsequent instructions.

Pair loads commit destinations after both reads succeed. Pair stores match the reference backend: a successful first store may remain visible if the second access faults, while base writeback has not occurred. Differential tests must preserve these modeled fault semantics as well as successful results.

Byte emission is independent of executable allocation. The runtime allocates writable memory, copies emitted bytes, then changes the mapping to read/execute. The baseline does not require a simultaneously writable/executable mapping. Emitter failure preserves the caller's original emitted size.

## Runtime and cache

`ASJitCache` uses guest PC as the lookup key and a fixed-capacity round-robin replacement policy. It tracks compilations, hits, executions, interpreter fallback steps, and the total emitted bytes currently cached. It does not chain TBs or cache guest registers across calls.

The cache binds to the memory object, backing pointer, base, size, and write generation. A change to any binding or generation invalidates all entries. Successful memory helper writes advance the generation, and stores end TBs so the next lookup observes the change. Every candidate hit also revalidates its guest instruction bytes. External callers that modify RAM directly must call `as_jit_cache_invalidate`; instruction revalidation is an additional safeguard. This conservative policy prioritizes correctness over cache retention across data stores.

The runtime dispatches blocks by the current guest PC and limits execution by an instruction budget, compiling a shortened block when the remaining budget is smaller than the cached block. Runtime results are `0` for HALT, `1` for budget exhaustion, `-1` for unsupported instructions, `-2` for guest memory faults, `-4` for an unavailable native runtime, and `-5` for host allocation or executable-mapping failure.

Native execution is enabled only for x86-64 Unix-like builds, including the x86-64 macOS code path. The emitted ABI has no Windows implementation. Non-native builds retain the reference interpreter but return `-4` from the JIT runner, even when fallback was requested.

## Runner and validation

`asrun --jit` is strict: unsupported instructions fail. `--jit-fallback` explicitly permits interpreter steps for blocks that cannot be compiled and reports them. `--compare` runs the interpreter and strict JIT from identical ELF, CPU, and RAM initialization, then compares execution results, all modeled registers, SP, PC, NZCV, HALT state, and the entire RAM image.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
bash tests/check_guests.sh build
```

The guest script requires an x86-64 Linux native runtime and Clang/LLD AArch64 cross-compilation. It checks assembly/C add results of 300, a 32-bit integer result of 48, and factorial 7 yielding 5040. Guest comparisons require `state=match` and `fallback=0`. Unit and differential tests cover instruction semantics, memory faults, stack frames, loader bounds, and cache behavior. Actual local and CI outcomes must be reported separately from this validation procedure.

The v0.0.4 completion criteria remain in [AGENTS.md](../AGENTS.md). Privileged execution, MMU, SIMD, OS boot, and GPU acceleration are outside this baseline; see [the platform roadmap](ROADMAP.md).
