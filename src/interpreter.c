// SPDX-License-Identifier: BSD-3-Clause
#include <string.h>
#include "asbridge/decode.h"
#include "asbridge/interpreter.h"
static uint64_t rg(const ASCPU*c,unsigned r){return r==31u?0:c->x[r];}static void wg(ASCPU*c,unsigned r,uint64_t v){if(r!=31u)c->x[r]=v;}
static void subflags(ASCPU*c,uint64_t a,uint64_t b,uint64_t r){uint32_t n=(uint32_t)(r>>63),z=(r==0),carry=(a>=b),v=((a^b)&(a^r))>>63;c->nzcv=(n<<31)|(z<<30)|(carry<<29)|((uint32_t)v<<28);}
static int cond(const ASCPU*c,unsigned q){unsigned n=(c->nzcv>>31)&1,z=(c->nzcv>>30)&1,cc=(c->nzcv>>29)&1,v=(c->nzcv>>28)&1;switch(q){case 0:return z;case 1:return!z;case 2:return cc;case 3:return!cc;case 10:return n==v;case 11:return n!=v;case 12:return!z&&(n==v);case 13:return z||(n!=v);case 14:return 1;default:return 0;}}
int as_step(ASCPU*c,ASMemory*m,uint32_t i){ASIR q;if(!as_decode_ir(i,&q))return-1;uint64_t n=c->pc+4,v=0,a,r;switch(q.op){
 case ASIR_NOP:break;case ASIR_HALT:c->halted=true;c->halt_imm=(uint16_t)q.imm;break;
 case ASIR_MOV_IMM:wg(c,q.rd,q.imm<<q.shift);break;case ASIR_MOV_KEEP:{uint64_t mask=UINT64_C(0xffff)<<q.shift;wg(c,q.rd,(rg(c,q.rd)&~mask)|((q.imm<<q.shift)&mask));break;}
 case ASIR_ADD_IMM:wg(c,q.rd,rg(c,q.rn)+q.imm);break;case ASIR_SUB_IMM:wg(c,q.rd,rg(c,q.rn)-q.imm);break;
 case ASIR_SUBS_IMM:a=rg(c,q.rn);r=a-q.imm;subflags(c,a,q.imm,r);wg(c,q.rd,r);break;
 case ASIR_LOAD64:if(as_mem_read64(m,rg(c,q.rn)+q.imm,&v))return-2;wg(c,q.rd,v);break;case ASIR_STORE64:v=rg(c,q.rd);if(as_mem_write64(m,rg(c,q.rn)+q.imm,v))return-2;break;
 case ASIR_BRANCH:if(q.rd==30u)c->x[30]=n;n=c->pc+q.offset;break;case ASIR_BRANCH_ZERO:if((rg(c,q.rn)==0u)!=(q.imm!=0u))n=c->pc+q.offset;break;case ASIR_BRANCH_REG:n=rg(c,q.rn);break;case ASIR_BRANCH_COND:if(cond(c,q.cond))n=c->pc+q.offset;break;
 case ASIR_ADR:wg(c,q.rd,c->pc+q.offset);break;case ASIR_ADRP:wg(c,q.rd,(c->pc&~UINT64_C(0xfff))+q.offset);break;default:return-1;}c->pc=n;return 0;}
int as_run(ASCPU*c,ASMemory*m,uint64_t lim){while(!c->halted&&lim--){if(c->pc<m->base||c->pc-m->base+4>m->size)return-2;uint32_t i;memcpy(&i,m->data+(size_t)(c->pc-m->base),4);int r=as_step(c,m,i);if(r)return r;}return c->halted?0:1;}
