// SPDX-License-Identifier: BSD-3-Clause
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "asbridge/cpu.h"
#include "asbridge/decode.h"
#include "asbridge/interpreter.h"
static void decode_tests(void){ASIR q;assert(as_decode_ir(0xF9000022u,&q)&&q.op==ASIR_STORE64);assert(as_decode_ir(0xF9400023u,&q)&&q.op==ASIR_LOAD64);assert(as_decode_ir(0x54000040u,&q)&&q.op==ASIR_BRANCH_COND&&q.cond==0&&q.offset==8);assert(as_decode_ir(0x10000040u,&q)&&q.op==ASIR_ADR&&q.offset==8);assert(as_decode_ir(0xD65F03C0u,&q)&&q.op==ASIR_BRANCH_REG&&q.rn==30);}
static void base_tests(void){ASCPU c;uint8_t ram[4096]={0};ASMemory m={ram,sizeof(ram),0x80000000u};uint32_t p[]={0xD2824680u,0x91004000u,0xD1001000u,0xD4200000u};memcpy(ram,p,sizeof(p));as_cpu_reset(&c,m.base);assert(as_run(&c,&m,16)==0&&c.x[0]==0x1240u);}
static void mem_tests(void){ASCPU c;uint8_t ram[4096]={0};ASMemory m={ram,sizeof(ram),0x80000000u};as_cpu_reset(&c,m.base);c.x[1]=m.base+256;c.x[2]=UINT64_C(0x1122334455667788);assert(as_step(&c,&m,0xF9000022u)==0);assert(as_step(&c,&m,0xF9400023u)==0&&c.x[3]==c.x[2]);}
static void branch_tests(void){ASCPU c;uint8_t ram[4096]={0};ASMemory m={ram,sizeof(ram),0x80000000u};as_cpu_reset(&c,m.base);assert(as_step(&c,&m,0x94000002u)==0&&c.x[30]==m.base+4&&c.pc==m.base+8);assert(as_step(&c,&m,0xD65F03C0u)==0&&c.pc==m.base+4);c.x[0]=5;assert(as_step(&c,&m,0xF100141Fu)==0);assert((c.nzcv&(1u<<30))!=0);uint64_t pc=c.pc;assert(as_step(&c,&m,0x54000040u)==0&&c.pc==pc+8);}
static void pc_tests(void){ASCPU c;uint8_t ram[4096]={0};ASMemory m={ram,sizeof(ram),0x80000000u};as_cpu_reset(&c,m.base);assert(as_step(&c,&m,0x10000040u)==0);assert(c.x[0]==m.base+8);}
int main(void){decode_tests();base_tests();mem_tests();branch_tests();pc_tests();puts("ASCore v0.0.2: PASS");return 0;}
