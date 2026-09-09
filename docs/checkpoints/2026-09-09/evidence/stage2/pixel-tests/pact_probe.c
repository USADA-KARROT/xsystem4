#include <stdio.h>
#include <string.h>
#include "system4/afa.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "system4/utfsjis.h"
static void walk(struct ex_tree *n,const char *path) {
 char next[2048]; char *name=strdup(n->name ? n->name->text : "");
 snprintf(next,sizeof(next),"%s/%s",path,name); free(name);
 if(n->is_leaf) {
  if(1) {
   printf("%s type=%d",next,n->leaf.value.type);
   if(n->leaf.value.type==EX_INT)printf(" value=%d",n->leaf.value.i);
   if(n->leaf.value.type==EX_FLOAT)printf(" value=%g",n->leaf.value.f);
   if(n->leaf.value.type==EX_STRING){char *s=strdup(n->leaf.value.s->text);printf(" value=");for(char *q=s;*q;q++){if(*q=='\n')printf("\\n");else if(*q=='\r')printf("\\r");else putchar(*q);}free(s);}
   if(n->leaf.value.type==EX_LIST) {for(unsigned i=0;i<n->leaf.value.list->nr_items;i++){struct ex_value *v=&n->leaf.value.list->items[i].value;if(v->type==EX_INT)printf(" %d",v->i);if(v->type==EX_FLOAT)printf(" %g",v->f);}}
   puts("");
  }
 } else {for(unsigned i=0;i<n->nr_children;i++)walk(&n->children[i],next);}
}
int main(int argc,char **argv) {
 int error=0;struct afa_archive *a=afa_open(argv[1],0,&error);if(!a)return 2;
 struct archive_data *d=archive_get_by_name(&a->ar, argc > 2 ? argv[2] : "Scene\\20_Title\\Title\\SceneTitle.pactex");if(!d)return 3;
 printf("%s size=%zu\n",d->name,d->size);
 struct ex *e=ex_read(d->data,d->size);if(!e)return 4;
 for(unsigned i=0;i<e->nr_blocks;i++)if(e->blocks[i].val.type==EX_TREE)walk(e->blocks[i].val.tree,"");
 ex_free(e);archive_free_data(d);archive_free(&a->ar);return 0;
}
