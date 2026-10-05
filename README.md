# ASBridge

ASBridge is an experimental clean-room ARM64 execution and dynamic binary translation environment for x86-64 systems.

## Status

ASCore v0.0.4 implements translation blocks and a baseline native x86-64 JIT for a bounded AArch64 integer subset. The interpreter remains the architectural reference. Both paths share the decoder, ASIR, CPU state, bounded RAM, and ELF loader.

The JIT supports W/X arithmetic and logical operations, subtraction flags, branches, safe loads/stores, and 64-bit stack pairs. A guest-PC keyed cache tracks emitted code and invalidates conservatively when memory changes. See [JIT design and supported forms](docs/JIT.md).

This milestone runs freestanding AArch64 payloads. It does not boot an OS or provide privileged instructions, an MMU, SIMD, or GPU acceleration. Running an Arm64 OS on x86-64 with accelerated graphics is the longer-term goal; [the roadmap](docs/ROADMAP.md) describes the remaining platform and GPU work.

## Principles

- Independent implementation based on public architecture specifications.
- Portable C17 reference interpreter first, x86-64 JIT second.
- Generic ARM64 platform before optional platform personalities.
- No proprietary Apple binaries, firmware, keys, or copyrighted device-tree dumps in this repository.
- QEMU, m1n1, and XNU may be used for interoperability research and behavioral comparison; incompatible source code is not copied into ASBridge.

## Codex / agent development

Repository development instructions, clean-room boundaries, validation commands, and the v0.0.4 completion criteria are maintained in [AGENTS.md](AGENTS.md). Read it before modifying ASCore.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake 3.20 or newer and a C17 compiler are required. Clang is primary; GCC compatibility is maintained. Native execution uses the System V x86-64 ABI on Unix-like hosts, including the x86-64 macOS code path. Windows and non-x86-64 hosts have no native JIT runtime; `as_jit_run` returns `-4`. The reference interpreter is separate from that native runtime.

On an x86-64 Linux host with Clang and LLD, compile and compare all AArch64 guest payloads:

```sh
bash tests/check_guests.sh build
```

The script uses `--target=aarch64-none-elf` and checks the assembly and C add guests (`X0 = 300`), the 32-bit integer guest (`X0 = 48`), and the factorial loop (`X0 = 5040`). Each guest must match the interpreter's CPU and RAM state with zero fallback steps. Set `ASBRIDGE_CLANG` to select a Clang executable. These commands describe the validation gates; they do not establish CI or macOS test results.

## Run

```sh
build/asrun build/guest_c.elf                 # reference interpreter
build/asrun --jit build/guest_c.elf           # strict native JIT
build/asrun --jit-fallback build/guest_c.elf  # explicit interpreter fallback
build/asrun --compare build/guest_c.elf       # strict JIT versus interpreter
```

`--jit` fails on an unsupported instruction. `--jit-fallback` permits interpreter steps when a block cannot be compiled and reports the fallback count; it cannot execute instructions unsupported by the interpreter. `--compare` checks the result, all modeled CPU state, and the entire guest RAM image. All modes use the same ELF loading and initial stack pointer. The runner allocates 64 MiB of guest RAM at `0x400000` and has a one-million-instruction execution budget.

## License

BSD-3-Clause.
