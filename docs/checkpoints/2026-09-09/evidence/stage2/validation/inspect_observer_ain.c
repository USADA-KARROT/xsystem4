
#include <stdio.h>
#include <string.h>
#include "system4/ain.h"
static void type(const struct ain_type *t){printf("%d",t->data);if(t->array_type){printf("<");type(t->array_type);printf(">");}}
int main(int argc,char**argv){int err=0;struct ain*a=ain_open(argv[1],&err);if(!a)return 1;int fns[]={20748,20752,27031,36081};for(int i=0;i<4;i++){struct ain_function*f=&a->functions[fns[i]];printf("f=%d args=%d vars=%d\n",fns[i],f->nr_args,f->nr_vars);for(int j=0;j<f->nr_vars;j++){printf(" var%d type=",j);type(&f->vars[j].type);puts("");}}for(int i=0;i<a->nr_delegates;i++){struct ain_function_type*d=&a->delegates[i];if(!strstr(d->name,"Observer"))continue;printf("delegate=%d name=%s argc=%d vars=%d\n",i,d->name,d->nr_arguments,d->nr_variables);for(int j=0;j<d->nr_variables;j++){printf(" var%d type=",j);type(&d->variables[j].type);puts("");}}ain_free(a);}
