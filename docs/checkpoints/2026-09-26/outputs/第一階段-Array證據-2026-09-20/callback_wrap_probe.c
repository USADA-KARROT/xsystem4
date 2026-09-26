#include <stdio.h>
#include "system4/ain.h"
void pt(struct ain_type*t){if(!t){printf("null");return;}printf("{data:%d,struct:%d,rank:%d,sub:",t->data,t->struc,t->rank);pt(t->array_type);printf("}");}
int main(int c,char**v){int e;struct ain*a=ain_open(v[1],&e);int ids[]={23361,25454,23987,24191};for(int i=0;i<4;i++){struct ain_function*f=&a->functions[ids[i]];printf("%d addr=%u args=%d ",ids[i],f->address,f->nr_args);for(int j=0;j<f->nr_args;j++)pt(&f->vars[j].type);puts("");}ain_free(a);}
