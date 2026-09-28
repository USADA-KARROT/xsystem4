#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "system4/afa.h"
#include "system4/archive.h"
#include "system4/ex.h"
#include "system4/string.h"
static void hex(const char *s) { if(s) for (;*s;s++) printf("%02x",(unsigned char)*s); }
static void val(struct ex_value *v) {
  if(v->type==EX_STRING) hex(v->s->text);
  else if(v->type==EX_INT) printf("%d",v->i);
  else if(v->type==EX_FLOAT) printf("%.9g",v->f);
  else if(v->type==EX_LIST){printf("[");for(unsigned i=0;i<v->list->nr_items;i++){if(i)printf(","); printf("%d:",v->list->items[i].value.type);val(&v->list->items[i].value);}printf("]");}
}
static const char *style_keys[] = {
  "\xa5\xaa\xa5\xf3\xd6\xb8\xe1\x98\xa0\xee\x91\x42",
  "\xa5\xad\xa9\x60\xa5\xc0\xa5\xa6\xa5\xf3\xa0\xee\x91\x42",
  "\xa5\xb5\xa9\x60\xa5\xd5\xa5\xa7\xa5\xa4\xa5\xb9\xa5\xa8\xa5\xea\xa5\xa2",
  "\xa5\xbf\xa5\xb0\x84\x49\xc0\xed\xd3\xd0\x84\xbf",
  "\xa5\xc0\xa5\xc3\xa5\xb7\xa5\xe5\xbd\x59\xba\xcf\xd3\xd0\x84\xbf",
  "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xb5\xa5\xa4\xa5\xba",
  "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xbf\xa5\xa4\xa5\xd7",
  "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xbf\x46\xc8\xa1\xa4\xea",
  "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xbf\x46\xc8\xa1\xa4\xea\xc9\xab",
  "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xc9\xab",
  "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xcc\xab\xa4\xb5",
  "\xa5\xeb\xa5\xd3\xd1\x62\xef\x97",
  "\xb2\xbf\xbc\xfe\xa5\xbf\xa5\xa4\xa5\xd7",
  "\xb7\x4e\xee\x90\x84\x65\xc7\xe9\x88\xf3",
  "\xb8\xc4\xd0\xd0\xa4\xf2\xb1\xed\xca\xbe\xa4\xb9\xa4\xeb",
  "\xc6\xd5\xcd\xa8\xa0\xee\x91\x42",
  "\xce\xc4\xb1\xbe\xce\xbb\xd6\xc3",
  "\xce\xc4\xb1\xbe\xd1\x62\xef\x97",
  "\xd0\xd0\xe9\x67\xb8\xf4",
  "\xd2\xbb\xce\xc4\xd7\xd6\xde\x78\xa4\xea\xa4\xce\xb1\xed\xca\xbe\x95\x72\xe9\x67",
  "\xd3\xa2\xce\xc4\xd7\xd4\x84\xd3\xd5\xdb\xa4\xea\xb7\xb5\xa4\xb7\xa5\xb5\xa5\xa4\xa5\xba",
  "\xd7\xd6\xe9\x67\xb8\xf4",
};
static int keep_key(const char *name) { for(unsigned i=0;i<sizeof(style_keys)/sizeof(*style_keys);i++)if(!strcmp(name,style_keys[i]))return 1;return 0;}
static void tree(struct ex_tree *t,int depth,int active) {
  active = active || (t->name && !strcmp(t->name->text,"TextAchievement"));
  if(active && t->name && keep_key(t->name->text)){printf("%d\t",depth);hex(t->name?t->name->text:"");printf("\t%d\t",t->is_leaf?t->leaf.value.type:6);if(t->is_leaf)val(&t->leaf.value);printf("\n");}
  if(!t->is_leaf) for(unsigned i=0;i<t->nr_children;i++)tree(&t->children[i],depth+1,active);
}
int main(int argc,char**argv) {
  if(argc!=2)return 2;
  int err=0;struct afa_archive *a=afa_open(argv[1],ARCHIVE_MMAP,&err);
  if(!a){fprintf(stderr,"archive %d\n",err);return 3;}
  int found=0;
  for(unsigned i=0;i<a->nr_files;i++)if(strstr(a->files[i].name->text,"SceneAchievementNotify.pactex")){
    printf("archive_entry\t");hex(a->files[i].name->text);printf("\n");
    struct archive_data*d=archive_get((struct archive*)a,i);if(!d)return 4;
    struct ex*e=ex_read(d->data,d->size);if(!e)return 5;
    for(unsigned j=0;j<e->nr_blocks;j++)if(e->blocks[j].val.type==EX_TREE)tree(e->blocks[j].val.tree,0,0);
    ex_free(e);archive_free_data(d);found++;
  }
  archive_free((struct archive*)a);return found?0:6;
}
