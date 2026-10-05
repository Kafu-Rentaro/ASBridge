// SPDX-License-Identifier: BSD-3-Clause
#include <string.h>
#include "asbridge/decode.h"
static int64_t sx(uint64_t v,unsigned b){uint64_t s=UINT64_C(1)<<(b-1);return(int64_t)((v^s)-s);}
bool as_decode_ir(uint32_t i,ASIR*d){memset(d,0,sizeof(*d));d->width=64;
 if(i==0xD503201Fu){d->op=ASIR_NOP;return true;}if((i&0xFFE0001Fu)==0xD4200000u){d->op=ASIR_HALT;d->imm=(i>>5)&0xffffu;return true;}
 if((i&0x7F800000u)==0x52800000u||(i&0x7F800000u)==0x72800000u){d->op=((i&0x7F800000u)==0x52800000u)?ASIR_MOV_IMM:ASIR_MOV_KEEP;d->width=(i>>31)?64:32;d->rd=i&31u;d->shift=((i>>21)&3u)*16u;if(d->width==32&&d->shift>=32)return false;d->imm=(i>>5)&0xffffu;return true;}
 if((i&0x7F000000u)==0x11000000u||(i&0x7F000000u)==0x51000000u){d->op=(i&0x40000000u)?ASIR_SUB_IMM:ASIR_ADD_IMM;d->width=(i>>31)?64:32;d->rd=i&31u;d->rn=(i>>5)&31u;d->use_sp=1;d->imm=((uint64_t)((i>>10)&0xfffu))<<(((i>>22)&1u)?12u:0u);return true;}
 if((i&0x7F000000u)==0x71000000u){d->op=ASIR_SUBS_IMM;d->width=(i>>31)?64:32;d->rd=i&31u;d->rn=(i>>5)&31u;d->imm=((uint64_t)((i>>10)&0xfffu))<<(((i>>22)&1u)?12u:0u);return true;}
 if((i&0x7F200000u)==0x0B000000u||(i&0x7F200000u)==0x4B000000u){d->op=(i&0x40000000u)?ASIR_SUB_REG:ASIR_ADD_REG;d->width=(i>>31)?64:32;d->rd=i&31u;d->rn=(i>>5)&31u;d->rm=(i>>16)&31u;d->shift=(i>>10)&0x3fu;if(d->width==32&&d->shift>=32)return false;return true;}
 if((i&0x7F200000u)==0x0A000000u||(i&0x7F200000u)==0x2A000000u||(i&0x7F200000u)==0x4A000000u){unsigned opc=(i>>29)&3u;d->op=opc==0?ASIR_AND_REG:(opc==1?ASIR_ORR_REG:ASIR_EOR_REG);d->width=(i>>31)?64:32;d->rd=i&31u;d->rn=(i>>5)&31u;d->rm=(i>>16)&31u;d->shift=(i>>10)&0x3fu;if(d->width==32&&d->shift>=32)return false;return true;}
 if((i&0x7FE0FC00u)==0x1B007C00u){d->op=ASIR_MUL;d->width=(i>>31)?64:32;d->rd=i&31u;d->rn=(i>>5)&31u;d->rm=(i>>16)&31u;return true;}
 if((i&0xBFC00000u)==0xB9000000u||(i&0xBFC00000u)==0xB9400000u){d->op=(i&0x00400000u)?ASIR_LOAD:ASIR_STORE;d->width=(i>>30)&1u?64:32;d->rd=i&31u;d->rn=(i>>5)&31u;d->use_sp=1;d->imm=((i>>10)&0xfffu)*(d->width/8u);return true;}
 if((i&0x3B200C00u)==0x38000400u||(i&0x3B200C00u)==0x38000C00u){d->op=(i&(1u<<22))?ASIR_LOAD:ASIR_STORE;d->width=((i>>30)&3u)==3u?64:32;d->rd=i&31u;d->rn=(i>>5)&31u;d->use_sp=1;d->offset=sx((i>>12)&0x1ffu,9);d->addr_mode=((i>>10)&3u)==1u?AS_ADDR_POST:AS_ADDR_PRE;return true;}
 if((i&0x3A000000u)==0x28000000u&&(i&0xC0000000u)==0x80000000u){unsigned mode=(i>>23)&3u;if(mode!=0u){d->op=(i&(1u<<22))?ASIR_LOAD_PAIR64:ASIR_STORE_PAIR64;d->rd=i&31u;d->rt2=(i>>10)&31u;d->rn=(i>>5)&31u;d->use_sp=1;d->offset=sx((i>>15)&0x7fu,7)*8;d->addr_mode=(mode==1u)?AS_ADDR_POST:(mode==3u?AS_ADDR_PRE:AS_ADDR_OFFSET);return true;}}
 if((i&0xFF000010u)==0x54000000u){d->op=ASIR_BRANCH_COND;d->cond=i&15u;d->offset=sx((i>>5)&0x7ffffu,19)*4;return true;}
 if((i&0x9F000000u)==0x10000000u){uint64_t x=((uint64_t)((i>>5)&0x7ffffu)<<2)|((i>>29)&3u);d->op=(i&0x80000000u)?ASIR_ADRP:ASIR_ADR;d->rd=i&31u;d->offset=sx(x,21)*(d->op==ASIR_ADRP?4096:1);return true;}
 if((i&0xFC000000u)==0x14000000u||(i&0xFC000000u)==0x94000000u){d->op=ASIR_BRANCH;d->rd=(i&0x80000000u)?30u:31u;d->offset=sx(i&0x03ffffffu,26)*4;return true;}
 if((i&0x7E000000u)==0x34000000u){d->op=ASIR_BRANCH_ZERO;d->width=(i>>31)?64:32;d->rn=i&31u;d->imm=(i>>24)&1u;d->offset=sx((i>>5)&0x7ffffu,19)*4;return true;}
 if((i&0xFFFFFC1Fu)==0xD61F0000u||(i&0xFFFFFC1Fu)==0xD65F0000u){d->op=ASIR_BRANCH_REG;d->rn=(i>>5)&31u;return true;}
 return false;}
