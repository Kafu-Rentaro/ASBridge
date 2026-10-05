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

The first emitter intentionally produces only a RET stub. This establishes the TB/emitter/code-buffer interfaces before register allocation and executable code-cache policy are introduced.
