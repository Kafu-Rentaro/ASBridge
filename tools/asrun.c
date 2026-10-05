// SPDX-License-Identifier: BSD-3-Clause
#include <stdio.h>
#include <stdlib.h>
#include "asbridge/cpu.h"
#include "asbridge/elf.h"
#include "asbridge/interpreter.h"
int main(int argc,char**argv){if(argc!=2){fprintf(stderr,"usage: asrun guest.elf\n");return 2;}FILE*f=fopen(argv[1],"rb");if(!f)return 2;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);if(n<=0){fclose(f);return 2;}unsigned char*img=malloc((size_t)n),*ram=calloc(1,64u*1024u*1024u);if(!img||!ram){fclose(f);free(img);free(ram);return 2;}if(fread(img,1,(size_t)n,f)!=(size_t)n){fclose(f);free(img);free(ram);return 2;}fclose(f);ASMemory m={ram,64u*1024u*1024u,0x400000u};ASELFImage e;int r=as_elf_load(img,(size_t)n,&m,&e);if(r){fprintf(stderr,"ELF load failed: %d\n",r);free(img);free(ram);return 1;}ASCPU c;as_cpu_reset(&c,e.entry);r=as_run(&c,&m,1000000);if(r)fprintf(stderr,"execution failed: %d pc=0x%llx\n",r,(unsigned long long)c.pc);else printf("halt imm=%u x0=0x%llx steps<=1000000\n",c.halt_imm,(unsigned long long)c.x[0]);free(img);free(ram);return r?1:0;}
