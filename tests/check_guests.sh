#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
set -euo pipefail
build_dir=${1:-build}
clang_bin=${ASBRIDGE_CLANG:-clang}
mkdir -p "$build_dir"

compile_asm() {
    "$clang_bin" --target=aarch64-none-elf -c "tests/$1.S" -o "$build_dir/$1.o"
}
link_guest() {
    local name=$1
    shift
    "$clang_bin" --target=aarch64-none-elf -fuse-ld=lld -nostdlib \
        -Wl,-Ttext=0x401000 -Wl,-Tdata=0x402000 -Wl,-e,_start \
        -Wl,--image-base=0x400000 "$@" -o "$build_dir/$name.elf"
}
check_guest() {
    local name=$1 expected=$2
    "$build_dir/asrun" "$build_dir/$name.elf" | tee "$build_dir/$name.interpreter.out"
    grep -q "x0=$expected " "$build_dir/$name.interpreter.out"
    "$build_dir/asrun" --compare "$build_dir/$name.elf" | tee "$build_dir/$name.jit.out"
    grep -q "x0=$expected " "$build_dir/$name.jit.out"
    grep -q 'fallback=0 .*state=match' "$build_dir/$name.jit.out"
}

compile_asm guest_minimal
link_guest guest_minimal "$build_dir/guest_minimal.o"
check_guest guest_minimal 0x12c
for name in guest_c guest_int guest_loop; do
    "$clang_bin" --target=aarch64-none-elf -O1 -ffreestanding -fno-stack-protector \
        -fno-unwind-tables -fno-asynchronous-unwind-tables \
        -c "tests/$name.c" -o "$build_dir/$name.o"
    compile_asm "${name}_start"
    link_guest "$name" "$build_dir/${name}_start.o" "$build_dir/$name.o"
    case "$name" in
        guest_c) check_guest "$name" 0x12c ;;
        guest_int) check_guest "$name" 0x30 ;;
        guest_loop) check_guest "$name" 0x13b0 ;;
    esac
done
