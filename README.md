# ASBridge

ASBridge is an experimental clean-room ARM64 execution and dynamic binary translation environment for x86-64 systems.

## Status

Early development — ASCore v0.0.1 bootstrap.

## Principles

- Independent implementation based on public architecture specifications.
- Portable C17 reference interpreter first, x86-64 JIT second.
- Generic ARM64 platform before optional platform personalities.
- No proprietary Apple binaries, firmware, keys, or copyrighted device-tree dumps in this repository.
- QEMU, m1n1, and XNU may be used for interoperability research and behavioral comparison; incompatible source code is not copied into ASBridge.

## Initial roadmap

1. ARM64 decoder and reference interpreter.
2. ASIR intermediate representation.
3. x86-64 JIT backend.
4. Exceptions and system registers.
5. MMU/TLB.
6. Generic ARM64 Linux boot.
7. SMP and atomics.
8. UEFI standalone runtime.
9. Pluggable platform-personality framework.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

BSD-3-Clause.
