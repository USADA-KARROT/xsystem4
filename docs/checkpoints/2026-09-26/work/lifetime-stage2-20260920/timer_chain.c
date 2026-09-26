/* Bounded read-only CFG decoder for five technical functions. No VM execution. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
static int cmp(const void*a,const void*b){unsigned x=*(const unsigned*)a,y=*(const unsigned*)b;return(x>y)-(x<y);}
int main(int argc,char**argv){if(argc!=2)return 1;int err=0;struct ain*a=ain_open(argv[1],&err);if(!a)return 2;initialize_instructions(a->version);
 int ids[]={420,421,422,430,431,432,435,438,439,440,441,442,22325,6346,6366,6463};printf("{\"scope\":\"Static CFG only; CALL targets not followed; selected functions contain no SWITCH.\",\"functions\":[");
 for(unsigned f=0;f<sizeof(ids)/sizeof(*ids);f++){
  unsigned todo[4096],sites[4096],nt=1,ns=0;todo[0]=a->functions[ids[f]].address;
  while(nt){unsigned ip=todo[--nt];int seen=0;for(unsigned j=0;j<ns;j++)if(sites[j]==ip)seen=1;if(seen)continue;if(ns>=4000||nt>=4000||ip+2>a->code_size)return 3;sites[ns++]=ip;unsigned op=LittleEndian_getW(a->code,ip),width=instruction_width(op);
   if(op==RETURN||op==ENDFUNC||op==FUNC||op==EOF)continue;
   if(op==SWITCH||op==STRSWITCH)return 4;
   if(op==JUMP){todo[nt++]=LittleEndian_getDW(a->code,ip+2);continue;}
   if(op==IFZ||op==IFNZ)todo[nt++]=LittleEndian_getDW(a->code,ip+2);
   if(op==DG_CALL)todo[nt++]=LittleEndian_getDW(a->code,ip+6);
   todo[nt++]=ip+width;
  }
  qsort(sites,ns,sizeof(*sites),cmp);if(f)putchar(',');printf("{\"id\":%d,\"address\":%u,\"name\":\"%s\",\"sites\":[",ids[f],a->functions[ids[f]].address,a->functions[ids[f]].name);
  for(unsigned j=0;j<ns;j++){unsigned ip=sites[j],op=LittleEndian_getW(a->code,ip),w=instruction_width(op);if(j)putchar(',');printf("{\"address\":%u,\"opcode\":\"%s\",\"args\":[",ip,instructions[op].name);for(unsigned k=2;k<w;k+=4){if(k>2)putchar(',');printf("%d",LittleEndian_getDW(a->code,ip+k));}printf("]}");}
  printf("]}");
 }printf("],\"struct_lifecycle\":[");int types[]={7,8,233,236,508,612};for(unsigned i=0;i<6;i++){struct ain_struct*t=&a->structures[types[i]];if(i)putchar(',');printf("{\"id\":%d,\"name\":\"%s\",\"constructor\":%d,\"destructor\":%d,\"members\":%d}",types[i],t->name,t->constructor,t->destructor,t->nr_members);}printf("]}\n");ain_free(a);return 0;}
