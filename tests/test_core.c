// SPDX-License-Identifier: BSD-3-Clause
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#if defined(__x86_64__) && (defined(__unix__) || defined(__APPLE__))
#include <sys/mman.h>
#include <unistd.h>
#endif
#include "asbridge/cpu.h"
#include "asbridge/decode.h"
#include "asbridge/elf.h"
#include "asbridge/interpreter.h"
#include "asbridge/jit.h"
#include "asbridge/tb.h"
static void decode_tests(void){ASIR q;assert(as_decode_ir(0xF9000022u,&q)&&q.op==ASIR_STORE);assert(as_decode_ir(0xF9400023u,&q)&&q.op==ASIR_LOAD);assert(as_decode_ir(0xA9BF7BFDu,&q)&&q.op==ASIR_STORE_PAIR64&&q.addr_mode==AS_ADDR_PRE&&q.offset==-16);assert(as_decode_ir(0xA8C17BFDu,&q)&&q.op==ASIR_LOAD_PAIR64&&q.addr_mode==AS_ADDR_POST&&q.offset==16);}
static void stack_frame_test(void){ASCPU c;uint8_t ram[4096]={0};ASMemory m={ram,sizeof(ram),0x80000000u};as_cpu_reset(&c,m.base);c.sp=m.base+0x800;c.x[29]=0x1111;c.x[30]=0x2222;uint64_t initial=c.sp;assert(as_step(&c,&m,0xA9BF7BFDu)==0);assert(c.sp==initial-16);c.x[29]=c.x[30]=0;assert(as_step(&c,&m,0xA8C17BFDu)==0);assert(c.sp==initial&&c.x[29]==0x1111&&c.x[30]==0x2222);}
static void program_test(void){ASCPU c;uint8_t ram[4096]={0};ASMemory m={ram,sizeof(ram),0x80000000u};uint32_t p[]={0xD2824680u,0x91004000u,0xD1001000u,0xD4200000u};memcpy(ram,p,sizeof(p));as_cpu_reset(&c,m.base);assert(as_run(&c,&m,16)==0&&c.x[0]==0x1240u);}
static void wreg_test(void){ASCPU c;uint8_t ram[4096]={0};ASMemory m={ram,sizeof(ram),0x80000000u};as_cpu_reset(&c,m.base);c.x[0]=UINT64_MAX;assert(as_step(&c,&m,0x52800020u)==0);assert(c.x[0]==1);c.x[1]=m.base+0x100;c.x[2]=UINT64_C(0xdeadbeef12345678);assert(as_step(&c,&m,0xB9000022u)==0);c.x[3]=UINT64_MAX;assert(as_step(&c,&m,0xB9400023u)==0);assert(c.x[3]==UINT64_C(0x12345678));}
static void elf_reject_test(void){uint8_t bad[64]={0},ram[4096]={0};ASMemory m={ram,sizeof(ram),0x400000};ASELFImage e;assert(as_elf_load(bad,sizeof(bad),&m,&e)!=0);bad[0]=0x7f;bad[1]='E';bad[2]='L';bad[3]='F';bad[4]=2;bad[5]=1;assert(as_elf_load(bad,sizeof(bad),&m,&e)!=0);}
static void tb_jit_test(void){uint32_t code[]={0xD2800020u,0x91000800u,0xD4200000u};ASTranslationBlock tb;assert(as_tb_build(&tb,(const uint8_t*)code,sizeof(code),0x1000,0x1000)==0);assert(tb.count==3&&tb.insn[2].ir.op==ASIR_HALT);uint8_t out[128]={0};ASJitCode j;as_jit_init(&j,out,sizeof(out));assert(as_jit_emit_tb_x86_64(&j,&tb)==0);assert(j.size>1&&out[j.size-1]==0xC3);
#if defined(__x86_64__) && (defined(__unix__) || defined(__APPLE__))
 size_t ps=(size_t)sysconf(_SC_PAGESIZE);void*mem=mmap(NULL,ps,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(mem!=MAP_FAILED);memcpy(mem,out,j.size);assert(mprotect(mem,ps,PROT_READ|PROT_EXEC)==0);ASCPU jit,ref;ASMemory dummy={0};as_cpu_reset(&jit,0x1000);as_cpu_reset(&ref,0x1000);assert(as_step(&ref,&dummy,code[0])==0);assert(as_step(&ref,&dummy,code[1])==0);assert(as_step(&ref,&dummy,code[2])==0);void(*fn)(ASCPU*)=(void(*)(ASCPU*))mem;fn(&jit);assert(jit.x[0]==ref.x[0]&&jit.pc==ref.pc&&jit.halted==ref.halted&&jit.halt_imm==ref.halt_imm);munmap(mem,ps);
#endif
}
int main(void){tb_jit_test();decode_tests();stack_frame_test();program_test();wreg_test();elf_reject_test();puts("ASCore v0.0.4: PASS");return 0;}
