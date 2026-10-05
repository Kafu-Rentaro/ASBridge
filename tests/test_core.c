// SPDX-License-Identifier: BSD-3-Clause
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "asbridge/cpu.h"
#include "asbridge/decode.h"
#include "asbridge/elf.h"
#include "asbridge/interpreter.h"
static void decode_tests(void){ASIR q;assert(as_decode_ir(0xF9000022u,&q)&&q.op==ASIR_STORE64);assert(as_decode_ir(0xF9400023u,&q)&&q.op==ASIR_LOAD64);assert(as_decode_ir(0xA9BF7BFDu,&q)&&q.op==ASIR_STORE_PAIR64&&q.addr_mode==AS_ADDR_PRE&&q.offset==-16);assert(as_decode_ir(0xA8C17BFDu,&q)&&q.op==ASIR_LOAD_PAIR64&&q.addr_mode==AS_ADDR_POST&&q.offset==16);}
static void stack_frame_test(void){ASCPU c;uint8_t ram[4096]={0};ASMemory m={ram,sizeof(ram),0x80000000u};as_cpu_reset(&c,m.base);c.sp=m.base+0x800;c.x[29]=0x1111;c.x[30]=0x2222;uint64_t initial=c.sp;assert(as_step(&c,&m,0xA9BF7BFDu)==0);assert(c.sp==initial-16);c.x[29]=c.x[30]=0;assert(as_step(&c,&m,0xA8C17BFDu)==0);assert(c.sp==initial&&c.x[29]==0x1111&&c.x[30]==0x2222);}
static void program_test(void){ASCPU c;uint8_t ram[4096]={0};ASMemory m={ram,sizeof(ram),0x80000000u};uint32_t p[]={0xD2824680u,0x91004000u,0xD1001000u,0xD4200000u};memcpy(ram,p,sizeof(p));as_cpu_reset(&c,m.base);assert(as_run(&c,&m,16)==0&&c.x[0]==0x1240u);}
static void elf_reject_test(void){uint8_t bad[64]={0},ram[4096]={0};ASMemory m={ram,sizeof(ram),0x400000};ASELFImage e;assert(as_elf_load(bad,sizeof(bad),&m,&e)!=0);bad[0]=0x7f;bad[1]='E';bad[2]='L';bad[3]='F';bad[4]=2;bad[5]=1;assert(as_elf_load(bad,sizeof(bad),&m,&e)!=0);}
int main(void){decode_tests();stack_frame_test();program_test();elf_reject_test();puts("ASCore v0.0.3: PASS");return 0;}
