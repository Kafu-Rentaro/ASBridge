// SPDX-License-Identifier: BSD-3-Clause
volatile unsigned long loop_input = 7;

__attribute__((noinline)) unsigned long factorial(unsigned long n) {
    unsigned long result = 1;
    while (n) {
        result *= n;
        --n;
    }
    return result;
}

unsigned long entry_loop(void) { return factorial(loop_input); }
