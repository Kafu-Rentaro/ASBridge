# ASCore v0.0.4 JIT design

The interpreter remains the architectural reference backend.

Pipeline:

AArch64 guest bytes -> decoder -> ASIR -> translation block -> x86-64 emitter -> code cache

Initial rules:
- Translation blocks end at control-flow or HALT operations.
- Guest PC is retained per decoded instruction for tracing and differential testing.
- The x86-64 emitter must never bypass ASIR decoding.
- Interpreter/JIT differential tests are required before an operation is considered JIT-supported.
- Executable-memory allocation is deliberately separate from byte emission so emitter tests can run without RWX memory.

The emitter has progressed beyond the initial RET stub. The current baseline directly updates canonical ASCPU state and intentionally avoids a register allocator. ASJitContext provides CPU, bounded guest memory, and fault state for the next memory-lowering stage.

For the authoritative implementation queue and completion criteria, see AGENTS.md.
