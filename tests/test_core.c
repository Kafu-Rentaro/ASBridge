// SPDX-License-Identifier: BSD-3-Clause
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "asbridge/cpu.h"
#include "asbridge/interpreter.h"
int main(void){ASCPU c;uint8_t ram[0x1000]={0};ASMemory m={ram,sizeof(ram),0x80000000u};as_cpu_reset(&c,m.base);uint32_t p[]={0xD2824680u,0x91004000u,0xD1001000u,0xD4200000u};memcpy(ram,p,sizeof(p));assert(as_run(&c,&m,16)==0);assert(c.x[0]==0x1240u);assert(c.halted);as_cpu_reset(&c,m.base);c.x[1]=m.base+0x100;c.x[2]=0x1122334455667788ULL;assert(as_step(&c,&m,0xF9000022u)==0);assert(as_step(&c,&m,0xF9400023u)==0);assert(c.x[3]==c.x[2]);puts("ASCore v0.0.2: PASS");return 0;}
