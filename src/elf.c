// SPDX-License-Identifier: BSD-3-Clause
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "asbridge/elf.h"
#define EI_NIDENT 16
#define PT_LOAD 1u
#define EM_AARCH64 183u
typedef struct { unsigned char ident[EI_NIDENT]; uint16_t type,machine; uint32_t version; uint64_t entry,phoff,shoff; uint32_t flags; uint16_t ehsize,phentsize,phnum,shentsize,shnum,shstrndx; } E64H;
typedef struct { uint32_t type,flags; uint64_t offset,vaddr,paddr,filesz,memsz,align; } E64P;
static int range(size_t size,uint64_t off,uint64_t len){return off<=size&&len<=size-(size_t)off;}
int as_elf_load(const void *image,size_t size,ASMemory *mem,ASELFImage *out){
 if(!image||!mem||!out||size<sizeof(E64H))return-1;const uint8_t*b=image;E64H h;memcpy(&h,b,sizeof(h));
 if(h.ident[0]!=0x7f||h.ident[1]!='E'||h.ident[2]!='L'||h.ident[3]!='F'||h.ident[4]!=2||h.ident[5]!=1||h.machine!=EM_AARCH64)return-2;
 if(h.phentsize<sizeof(E64P)||!range(size,h.phoff,(uint64_t)h.phentsize*h.phnum))return-3;
 out->entry=h.entry;out->low=UINT64_MAX;out->high=0;
 for(uint16_t n=0;n<h.phnum;n++){E64P p;memcpy(&p,b+h.phoff+(uint64_t)n*h.phentsize,sizeof(p));if(p.type!=PT_LOAD)continue;if(p.filesz>p.memsz||!range(size,p.offset,p.filesz))return-4;if(p.vaddr<mem->base||p.memsz>mem->size-(size_t)(p.vaddr-mem->base))return-5;size_t o=(size_t)(p.vaddr-mem->base);memcpy(mem->data+o,b+p.offset,(size_t)p.filesz);memset(mem->data+o+p.filesz,0,(size_t)(p.memsz-p.filesz));if(p.vaddr<out->low)out->low=p.vaddr;if(p.vaddr+p.memsz>out->high)out->high=p.vaddr+p.memsz;}
 if(out->low==UINT64_MAX||out->entry<out->low||out->entry>=out->high)return-6;return 0;
}
