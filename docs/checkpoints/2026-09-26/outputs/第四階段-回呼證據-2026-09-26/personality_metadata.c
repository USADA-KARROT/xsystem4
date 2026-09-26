#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "system4/string.h"
/* Read-only metadata/technical-bytecode probe. Does not run the VM. */
static int wanted(int n) {
    const int ids[]={28149,28150,28153,27605,28947,28980,32260,6100,6086,24980,25144,25145,14312};
    for (unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++) if(ids[i]==n)return 1;
    return 0;
}
static void hex(const char *p, size_t n){for(size_t i=0;i<n;i++)printf("%02x",(unsigned char)p[i]);}
static void typ(const struct ain_type *t){printf("{data=%d,struct=%d,rank=%d",t->data,t->struc,t->rank);if(t->array_type){printf(",sub=");typ(t->array_type);}printf("}");}
int main(int argc,char **argv){
 if(argc!=2)return 2; int err=0; struct ain *a=ain_open(argv[1],&err);if(!a)return 3;
 initialize_instructions(a->version);
 for(int n=0;n<a->nr_functions;n++)if(wanted(n)||!strcmp(a->functions[n].name,"EX_A2String")||!strcmp(a->functions[n].name,"EX_RA2String")){
  struct ain_function *f=&a->functions[n]; printf("FUNCTION\t%d\t0x%x\t%d\t%d\t%s\n",n,f->address,f->nr_args,f->nr_vars,f->name);
  printf("RETURN\t%d\t",n);typ(&f->return_type);puts("");
  for(int v=0;v<f->nr_vars;v++){printf("VAR\t%d\t%d\t",n,v);typ(&f->vars[v].type);puts("");}
 }
 for(int n=824;n<=825;n++){struct ain_function_type *d=&a->delegates[n];printf("DELEGATE\t%d\targs=%d\treturn=",n,d->nr_arguments);typ(&d->return_type);puts("");for(int v=0;v<d->nr_arguments;v++){printf("DGVAR\t%d\t%d\t",n,v);typ(&d->variables[v].type);puts("");}}
 int current=-1;
 for(size_t ip=0;ip<a->code_size;){
  unsigned op=LittleEndian_getW(a->code,ip),w=instruction_width(op);
  if(!w||ip+w>a->code_size)return 4;
  if(op==FUNC)current=LittleEndian_getDW(a->code,ip+2);
  if((op==NEW&&LittleEndian_getDW(a->code,ip+2)==706)||
     ((current==28149||current==28150||current==28153||current==24980||current==25144||current==25145)&&op!=FUNC)){
   printf("INSN\t%d\t0x%zx\t%s",current,ip,instructions[op].name);
   for(unsigned off=2;off<w;off+=4)printf("\t%d",(int)LittleEndian_getDW(a->code,ip+off));
   if(op==S_PUSH){int i=LittleEndian_getDW(a->code,ip+2);printf("\tstringhex=");hex(a->strings[i]->text,a->strings[i]->size);}
   puts("");
  }
  if(op==ENDFUNC)current=-1; /* Do not label an outer tail as its nested lambda. */
  ip+=w;
 }
 ain_free(a); return 0;
}
