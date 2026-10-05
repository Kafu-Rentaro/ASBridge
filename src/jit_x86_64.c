// SPDX-License-Identifier: BSD-3-Clause
#include <string.h>
#include "asbridge/jit.h"
void as_jit_init(ASJitCode*j,uint8_t*b,size_t c){j->data=b;j->size=0;j->capacity=c;}
static int emit(ASJitCode*j,const void*p,size_t n){if(n>j->capacity-j->size)return-1;memcpy(j->data+j->size,p,n);j->size+=n;return 0;}
int as_jit_emit_tb_x86_64(ASJitCode*j,const ASTranslationBlock*tb){if(!j||!tb)return-1;/* v0.0.4 bootstrap: emit a valid host RET stub first. Real ASIR lowering follows. */const uint8_t ret=0xC3;return emit(j,&ret,1);}
