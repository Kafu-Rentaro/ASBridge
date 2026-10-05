# ASBridge platform roadmap

The long-term goal is to run Arm64 operating systems on x86-64 with host-accelerated graphics, including eventual investigation of macOS variants. The v0.0.4 baseline provides a bounded integer execution environment and native CPU translation. It delivers neither macOS boot nor GPU acceleration.

Progress should remain incremental, with the interpreter as the architectural reference and differential tests for each JIT addition. Public specifications and independently built guest payloads define the implementation boundary.

## 1. Stabilize the execution baseline

Complete the v0.0.4 validation gates: native W/X semantics, safe memory helpers, stack frames, flags and branches, strict compiled-C execution, and explicit cache invalidation. Preserve reproducible ELF guest tests and report unsupported instructions and fallback counts. Extend ordinary instruction coverage, atomics, and SIMD only with reference semantics and focused tests.

## 2. Generic privileged AArch64 execution

Introduce a documented ARMv8-A system model: exception levels, architectural system registers, synchronous exceptions, exception entry/return, and translation controls. Add page-table walks, address translation and permissions, MMU/TLB invalidation, and precise instruction/data faults. ASCore should expose generic architectural behavior; board-specific addresses and devices belong outside it.

Validation should use small freestanding guests before an OS: exception handlers, privileged register accesses, page-table tests, and faults with known architectural state.

## 3. A reproducible virtual machine platform

Define guest physical memory and MMIO boundaries, timers, interrupt delivery and an interrupt controller, virtual CPUs and multicore synchronization, and the ordering/atomic behavior required by the selected guest. Add a minimal virtual board with serial output, storage, and other boot-critical virtual devices.

Choose public boot protocols and redistributable firmware with documented licenses. Keep machine configuration and firmware loading outside the generic execution core. Device behavior needs independent tests and deterministic traces so boot failures can be separated from CPU translation failures.

## 4. First open AArch64 OS baseline

Select a reproducible open AArch64 OS, initially Linux on the documented virtual board. Record toolchain versions, firmware provenance, build commands, boot configuration, and expected serial output. Advance from early boot to userspace and repeated boot tests before adding platform-specific personalities.

This provides an observable system baseline for CPU, MMU, interrupts, storage, and multicore work. A successful freestanding C guest is not an OS boot milestone.

## 5. Host-accelerated graphics

First provide a virtual display/framebuffer, then define a virtual GPU device and an explicit guest-driver/host-renderer API boundary. Select a documented graphics protocol and suitable host graphics API; specify command submission, resource ownership, synchronization, memory sharing, and recovery before accelerating workloads.

Validation must use a compatible guest driver and check rendered output and synchronization behavior against a software path. CPU JIT translation alone cannot provide GPU acceleration: the guest needs a graphics device and driver interface that the host implements. Performance work follows a working graphics path and measured workloads.

## 6. Separate optional platform personalities

Platform personalities, including any future Apple-specific research, remain separate from ASCore. They may describe independently implemented machine interfaces while reusing the generic CPU, MMU, and device infrastructure. Compatibility with a particular macOS variant must be demonstrated for that variant; generic AArch64 support does not establish it.

Keep the BSD-3-Clause clean-room boundary throughout. Use public architecture and interface documentation, never copied QEMU, m1n1, XNU, or proprietary implementation code. Do not commit Apple binaries, firmware, keys, kernelcaches, boot images, or proprietary DeviceTrees. Record incorporated third-party material and licenses in `THIRD_PARTY.md`.

Each stage requires its own reproducible correctness evidence. No OS boot, macOS compatibility, or GPU performance claim belongs to the current v0.0.4 milestone.
