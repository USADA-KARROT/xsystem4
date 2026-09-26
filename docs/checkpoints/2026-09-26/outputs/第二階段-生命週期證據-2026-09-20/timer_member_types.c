#include <stdio.h>
#include "system4/ain.h"
static void tp(struct ain_type*t){printf("{data:%d,struc:%d,rank:%d",t->data,t->struc,t->rank);if(t->array_type){printf(",subtype:");tp(t->array_type);}printf("}");}
int main(int argc,char**argv){int e=0;struct ain*a=ain_open(argv[1],&e);if(!a)return 2;for(int i=0;i<5;i++){printf("struct9.member%d %s ",i,a->structures[9].members[i].name);tp(&a->structures[9].members[i].type);puts("");}ain_free(a);return 0;}
