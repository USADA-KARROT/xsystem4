#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/savefile.h"
static int compare(const void*a,const void*b){return strcmp(*(const char*const*)a,*(const char*const*)b);}
struct count {const char*name;int n;};
static int descending(const void*a,const void*b){return ((const struct count*)b)->n-((const struct count*)a)->n;}
static void top(const char*label,const char**names,int n){
 qsort(names,n,sizeof(char*),compare);struct count*v=calloc(n+1,sizeof(*v));int m=0;
 for(int i=0;i<n;i++){if(!m||strcmp(v[m-1].name,names[i]))v[m++]=(struct count){names[i],1};else v[m-1].n++;}
 qsort(v,m,sizeof(*v),descending);for(int i=0;i<m&&i<12;i++)printf("TOP %s %d %s\n",label,v[i].n,v[i].name);free(v);
}
int main(int argc,char**argv){
 if(argc!=2)return 1;enum savefile_error error;struct rsave*s=rsave_read(argv[1],RSAVE_READ_ALL,&error);if(!s){printf("error %d\n",error);return 2;}
 long count[7]={0},slots[6]={0},textbytes=0;int max_array=0,max_array_id=-1;
 const char**structures=calloc(s->nr_heap_objs,sizeof(char*)),**locals=calloc(s->nr_heap_objs,sizeof(char*));int ns=0,nl=0;
 for(int i=0;i<s->nr_heap_objs;i++){
  enum rsave_heap_tag tag=*(enum rsave_heap_tag*)s->heap[i];if(tag==-1){count[6]++;continue;}if(tag<0||tag>5)return 3;count[tag]++;
  switch(tag){
   case RSAVE_GLOBALS:case RSAVE_LOCALS:{struct rsave_heap_frame*f=s->heap[i];slots[tag]+=f->nr_slots;if(tag==RSAVE_LOCALS)locals[nl++]=f->func.name?f->func.name:"";break;}
   case RSAVE_STRING:{struct rsave_heap_string*t=s->heap[i];textbytes+=t->len;break;}
   case RSAVE_ARRAY:{struct rsave_heap_array*a=s->heap[i];slots[tag]+=a->nr_slots;if(a->nr_slots>max_array){max_array=a->nr_slots;max_array_id=i;}break;}
   case RSAVE_STRUCT:{struct rsave_heap_struct*t=s->heap[i];slots[tag]+=t->nr_slots;structures[ns++]=t->struct_type.name?t->struct_type.name:"";if(t->struct_type.name&&!strcmp(t->struct_type.name,"CASConfigData")&&t->nr_slots>31)printf("CONFIG slot=%d enabled=%d storage_ms=%d memory_frames=%d\n",i,t->slots[29],t->slots[30],t->slots[31]);break;}
   case RSAVE_DELEGATE:slots[tag]+=((struct rsave_heap_delegate*)s->heap[i])->nr_slots;break;default:break;
  }
 }
 printf("SAVE version=%d heap_slots=%d next_seq=%d call_frames=%d stack=%d func_names=%d\n",s->version,s->nr_heap_objs,s->next_seq,s->nr_call_frames,s->stack_size,s->nr_func_names);
 for(int i=0;i<7;i++)printf("TAG %d count=%ld value_slots=%ld\n",i==6?-1:i,count[i],i<6?slots[i]:0);
 printf("STRING_BYTES %ld MAX_ARRAY id=%d slots=%d\n",textbytes,max_array_id,max_array);
 for(int i=0;i<s->nr_call_frames;i++){int j=s->call_frames[i].local_ptr;if(j>=0&&j<s->nr_heap_objs){struct rsave_heap_frame*f=s->heap[j];if(f->tag==RSAVE_LOCALS)printf("FRAME %d heap=%d %s\n",i,j,f->func.name?f->func.name:"");}}
 top("struct",structures,ns);top("local",locals,nl);free(structures);free(locals);rsave_free(s);return 0;
}
