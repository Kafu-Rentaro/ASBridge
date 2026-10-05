// SPDX-License-Identifier: BSD-3-Clause
#include <string.h>
#include "asbridge/memory.h"
static int off(const ASMemory*m,uint64_t a,size_t n,size_t*o){if(a<m->base)return -1;uint64_t x=a-m->base;if(x>m->size||n>m->size-(size_t)x)return -1;*o=(size_t)x;return 0;}
int as_mem_read64(const ASMemory*m,uint64_t a,uint64_t*v){size_t o;if(off(m,a,8,&o))return -1;memcpy(v,m->data+o,8);return 0;}
int as_mem_write64(ASMemory*m,uint64_t a,uint64_t v){size_t o;if(off(m,a,8,&o))return -1;memcpy(m->data+o,&v,8);return 0;}
