# Contributing to ASBridge

## Clean implementation policy

ASBridge is intended to be an independently implemented project.

1. Implement architectural behavior primarily from publicly available specifications.
2. Do not copy source from projects with incompatible licenses into BSD-3-Clause ASBridge files.
3. QEMU, m1n1, XNU and other implementations may be used for behavioral comparison and interoperability research.
4. Document incorporated third-party material in THIRD_PARTY.md and preserve its applicable license.
5. Do not commit proprietary Apple software, firmware, keys, kernelcaches, iBoot images, or copyrighted device-tree dumps.

Every source file authored for ASBridge should carry:

```c
// SPDX-License-Identifier: BSD-3-Clause
```

Please keep generic ARM64 execution code independent from platform-personality code.
