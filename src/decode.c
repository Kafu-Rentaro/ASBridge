// SPDX-License-Identifier: BSD-3-Clause
#include <string.h>
#include "asbridge/decode.h"
static int64_t sx(uint64_t v,unsigned b){uint64_t s=UINT64_C(1)<<(b-1);return(int64_t)((v^s)-s);}
bool as_decode_ir(uint32_t i,ASIR*d){memset(d,0,sizeof(*d));
 if(i==0xD503201Fu){d->op=ASIR_NOP;return true;}
 if((i&0xFFE0001Fu)==0xD4200000u){d->op=ASIR_HALT;d->imm=(i>>5)&0xffffu;return true;}
 if((i&0xFF800000u)==0xD2800000u||(i&0xFF800000u)==0xF2800000u){d->op=((i&0xFF800000u)==0xD2800000u)?ASIR_MOV_IMM:ASIR_MOV_KEEP;d->rd=i&31u;d->shift=((i>>21)&3u)*16u;d->imm=(i>>5)&0xffffu;return true;}
 if((i&0xFF000000u)==0x91000000u||(i&0xFF000000u)==0xD1000000u){d->op=(i&0x40000000u)?ASIR_SUB_IMM:ASIR_ADD_IMM;d->rd=i&31u;d->rn=(i>>5)&31u;d->imm=((uint64_t)((i>>10)&0xfffu))<<(((i>>22)&1u)?12u:0u);return true;}
 if((i&0xFF000000u)==0xF1000000u){d->op=ASIR_SUBS_IMM;d->rd=i&31u;d->rn=(i>>5)&31u;d->imm=((uint64_t)((i>>10)&0xfffu))<<(((i>>22)&1u)?12u:0u);return true;}
 if((i&0xFF000010u)==0x54000000u){d->op=ASIR_BRANCH_COND;d->cond=i&15u;d->offset=sx((i>>5)&0x7ffffu,19)*4;return true;}
 if((i&0x9F000000u)==0x10000000u){uint64_t x=((uint64_t)((i>>5)&0x7ffffu)<<2)|((i>>29)&3u);d->op=(i&0x80000000u)?ASIR_ADRP:ASIR_ADR;d->rd=i&31u;d->offset=sx(x,21)*(d->op==ASIR_ADRP?4096:1);return true;}
 if((i&0xFC000000u)==0x14000000u||(i&0xFC000000u)==0x94000000u){d->op=ASIR_BRANCH;d->rd=(i&0x80000000u)?30u:31u;d->offset=sx(i&0x03ffffffu,26)*4;return true;}
 if((i&0x7E000000u)==0x34000000u){d->op=ASIR_BRANCH_ZERO;d->rn=i&31u;d->imm=(i>>24)&1u;d->offset=sx((i>>5)&0x7ffffu,19)*4;return true;}
 if((i&0xFFFFFC1Fu)==0xD61F0000u||(i&0xFFFFFC1Fu)==0xD65F0000u){d->op=ASIR_BRANCH_REG;d->rn=(i>>5)&31u;return true;}
 if((i&0xFFC00000u)==0xF9000000u||(i&0xFFC00000u)==0xF9400000u){d->op=(i&0x00400000u)?ASIR_LOAD64:ASIR_STORE64;d->rd=i&31u;d->rn=(i>>5)&31u;d->imm=((i>>10)&0xfffu)*8u;return true;}
 return false;}
