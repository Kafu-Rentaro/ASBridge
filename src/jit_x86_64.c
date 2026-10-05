// SPDX-License-Identifier: BSD-3-Clause
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "asbridge/cpu.h"
#include "asbridge/jit.h"
void as_jit_init(ASJitCode*j,uint8_t*b,size_t c){j->data=b;j->size=0;j->capacity=c;}
static int e(ASJitCode*j,const void*p,size_t n){if(n>j->capacity-j->size)return-1;memcpy(j->data+j->size,p,n);j->size+=n;return 0;}
static int u8(ASJitCode*j,uint8_t v){return e(j,&v,1);}static int u32(ASJitCode*j,uint32_t v){return e(j,&v,4);}static int u64(ASJitCode*j,uint64_t v){return e(j,&v,8);}
static int loadw(ASJitCode*j,unsigned r,unsigned w){if(r==31u){uint8_t z[]={0x31,0xC0};return e(j,z,2);}if(w==32){uint8_t p[]={0x8B,0x87};return e(j,p,2)||u32(j,(uint32_t)(offsetof(ASCPU,x)+r*8));}uint8_t p[]={0x48,0x8B,0x87};return e(j,p,3)||u32(j,(uint32_t)(offsetof(ASCPU,x)+r*8));}
static int loadc(ASJitCode*j,unsigned r,unsigned w){if(r==31u){uint8_t z[]={0x31,0xC9};return e(j,z,2);}if(w==32){uint8_t p[]={0x8B,0x8F};return e(j,p,2)||u32(j,(uint32_t)(offsetof(ASCPU,x)+r*8));}uint8_t p[]={0x48,0x8B,0x8F};return e(j,p,3)||u32(j,(uint32_t)(offsetof(ASCPU,x)+r*8));}
static int storew(ASJitCode*j,unsigned r,unsigned w){if(r==31u)return 0;if(w==32){uint8_t p[]={0x89,0x87};return e(j,p,2)||u32(j,(uint32_t)(offsetof(ASCPU,x)+r*8));}uint8_t p[]={0x48,0x89,0x87};return e(j,p,3)||u32(j,(uint32_t)(offsetof(ASCPU,x)+r*8));}
static int loadx(ASJitCode*j,unsigned r){if(r==31u){uint8_t z[]={0x31,0xC0};return e(j,z,2);}uint8_t p[]={0x48,0x8B,0x87};return e(j,p,3)||u32(j,(uint32_t)(offsetof(ASCPU,x)+r*8));}
static int storex(ASJitCode*j,unsigned r){if(r==31u)return 0;uint8_t p[]={0x48,0x89,0x87};return e(j,p,3)||u32(j,(uint32_t)(offsetof(ASCPU,x)+r*8));}
static int movabs(ASJitCode*j,uint64_t v){uint8_t p[]={0x48,0xB8};return e(j,p,2)||u64(j,v);}
static int addimm(ASJitCode*j,uint64_t v,int sub){if(v<=UINT32_MAX){uint8_t p[]={0x48,0x81,(uint8_t)(sub?0xE8:0xC0)};return e(j,p,3)||u32(j,(uint32_t)v);}return-2;}
static int setpc(ASJitCode*j,uint64_t pc){if(movabs(j,pc))return-1;uint8_t p[]={0x48,0x89,0x87};return e(j,p,3)||u32(j,(uint32_t)offsetof(ASCPU,pc));}
int as_jit_emit_tb_x86_64(ASJitCode*j,const ASTranslationBlock*tb){if(!j||!tb)return-1;for(size_t k=0;k<tb->count;k++){const ASIR*q=&tb->insn[k].ir;switch(q->op){
case ASIR_MOV_IMM:if(movabs(j,(q->imm<<q->shift)&(q->width==32?UINT32_MAX:UINT64_MAX))||storew(j,q->rd,q->width))return-1;break;
case ASIR_ADD_IMM:case ASIR_SUB_IMM:if(q->rn==31u||q->rd==31u)return-2;if(loadw(j,q->rn,q->width)||addimm(j,q->imm,q->op==ASIR_SUB_IMM)||storew(j,q->rd,q->width))return-1;break;
case ASIR_AND_REG:case ASIR_ORR_REG:case ASIR_EOR_REG:case ASIR_MUL:{if(q->rd==31u||q->shift)return-2;if(loadw(j,q->rn,q->width)||loadc(j,q->rm,q->width))return-1;if(q->width==64&&u8(j,0x48))return-1;if(q->op==ASIR_MUL){if(u8(j,0x0F)||u8(j,0xAF)||u8(j,0xC1))return-1;}else{uint8_t op=q->op==ASIR_AND_REG?0x21:(q->op==ASIR_ORR_REG?0x09:0x31);if(u8(j,op)||u8(j,0xC8))return-1;}if(storew(j,q->rd,q->width))return-1;break;}
case ASIR_HALT:{if(setpc(j,tb->insn[k].pc+4))return-1;uint8_t h[]={0xC6,0x87};if(e(j,h,2)||u32(j,(uint32_t)offsetof(ASCPU,halted))||u8(j,1))return-1;uint8_t hi[]={0x66,0xC7,0x87};if(e(j,hi,3)||u32(j,(uint32_t)offsetof(ASCPU,halt_imm))||e(j,&q->imm,2))return-1;break;}
default:return-2;}}
return u8(j,0xC3);}
