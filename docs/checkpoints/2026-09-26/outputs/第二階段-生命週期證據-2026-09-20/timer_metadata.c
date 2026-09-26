#include <stdio.h>
#include <string.h>
#include "system4/ain.h"
int main(int argc,char**argv){int e=0;struct ain*a=ain_open(argv[1],&e);if(!a)return 2;
for(int i=0;i<a->nr_globals;i++)if(strstr(a->globals[i].name,"g_ASTimerManager"))printf("GLOBAL\t%d\t%s\ttype=%d\n",i,a->globals[i].name,a->globals[i].type.data);
int ids[]={7,8,9,233};for(unsigned j=0;j<4;j++){int i=ids[j];struct ain_struct*s=&a->structures[i];printf("STRUCT\t%d\t%s\tctor=%d\tdtor=%d\tmembers=%d\n",i,s->name,s->constructor,s->destructor,s->nr_members);for(int k=0;k<s->nr_members;k++)printf("MEMBER\t%d\t%d\t%s\ttype=%d\n",i,k,s->members[k].name,s->members[k].type.data);}
int fs[]={420,421,422,423,424,425,430,431,432,435,438,439,440,441,442,22325,6346,6362,6364,6365,6366,6367,6368,6369,6370,6374,6463};for(unsigned j=0;j<sizeof(fs)/sizeof(*fs);j++){int i=fs[j];struct ain_function*f=&a->functions[i];printf("FUNCTION\t%d\t%s\taddress=0x%x\targs=%d\tlocals=%d\treturn=%d\n",i,f->name,f->address,f->nr_args,f->nr_vars,f->return_type.data);}
for(int i=0;i<a->nr_libraries;i++){struct ain_library*l=&a->libraries[i];if(strcmp(l->name,"Array")&&strcmp(l->name,"system")&&strcmp(l->name,"IbisInputEngine"))continue;for(int j=0;j<l->nr_functions;j++){char*n=l->functions[j].name;if(!strcmp(n,"GetTime")||!strcmp(n,"Error")||!strcmp(n,"Key_IsDown")||(!strcmp(l->name,"Array")&&(!strcmp(n,"At")||!strcmp(n,"Last")||!strcmp(n,"Empty")||!strcmp(n,"PopBack")||!strcmp(n,"EmplaceBack")||!strcmp(n,"Find")||!strcmp(n,"Numof"))))printf("HLL\t%d\t%d\t%s.%s\targs=%d\treturn=%d\n",i,j,l->name,n,l->functions[j].nr_arguments,l->functions[j].return_type.data);}}
ain_free(a);return 0;}
