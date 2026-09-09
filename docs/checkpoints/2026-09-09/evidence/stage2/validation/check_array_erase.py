from pathlib import Path
import hashlib,json,os,subprocess
HERE=Path(__file__).resolve().parent
STAGE2=HERE.parent
SOURCE=STAGE2/'source'

def func(text,signature):
 start=text.index(signature);brace=text.index('{',start);depth=1;end=brace+1
 while depth:
  if text[end]=='{':depth+=1
  elif text[end]=='}':depth-=1
  end+=1
 return text[start:end]
path=SOURCE/'src/hll/Array.c';source=path.read_text()
snippets='\n'.join(func(source,x) for x in [
 'static inline bool array_elem_is_ref(', 'static inline bool array_elem_is_2slot(',
 'static int array_erase_stride(', 'static bool Array_Erase(', 'static bool Array_EraseIf(',
 'static bool array_erase_value_equal(', 'static bool Array_EraseValues(',
 'void *array_erase_function('])
old=subprocess.run(['git','show','HEAD:src/hll/Array.c'],cwd=SOURCE,capture_output=True,text=True,check=True).stdout
legacy=func(old,'static void Array_Erase(').replace('Array_Erase(', 'Array_Erase_before(',1)
prefix=r'''
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ffi.h>
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
struct ain *ain;
static union vm_value fixture_stack[64];
union vm_value *stack=fixture_stack;
int32_t stack_ptr;
static struct vm_pointer slots[128];
struct vm_pointer *heap=slots;
size_t heap_size=128;
int hll_current_arg3;
static int unrefs[128], callbacks;
static bool ended[128];
struct page *alloc_page(enum page_type type,int index,int count) {
 struct page *p=calloc(1,sizeof(*p)+count*sizeof(union vm_value));assert(p);
 p->type=type;p->index=index;p->nr_vars=count;p->array.rank=1;return p;
}
void free_page(struct page *p){free(p);}
void heap_unref(int slot){assert(slot>0 && slot<128 && heap[slot].ref>0);unrefs[slot]++;heap[slot].ref--;}
union vm_value stack_pop(void){assert(stack_ptr>0);return stack[--stack_ptr];}
void vm_call_nopop(int fno,int nargs){
 assert(fno==17 && (nargs==1 || nargs==2));int obj=stack[stack_ptr-nargs].i;assert(obj>0 && obj<128);
 if(nargs==2)assert(stack[stack_ptr-1].i==obj+1);
 callbacks++;stack_push(ended[obj]);
}
'''
tests=r'''
static struct page *numbers(const int *v,int n,int type){
 struct page *p=alloc_page(ARRAY_PAGE,type,n);for(int i=0;i<n;i++)p->values[i].i=v[i];return p;
}
static void expect(struct page *p,const int *v,int n){
 assert((p?p->nr_vars:0)==n);for(int i=0;i<n;i++)assert(p->values[i].i==v[i]);
}
static bool call2(void *fn,struct page **p,int value){
 struct page **pp=p;ffi_type *types[]={&ffi_type_pointer,&ffi_type_sint32};
 void *args[]={&pp,&value};ffi_cif cif;ffi_arg ret=0;
 assert(ffi_prep_cif(&cif,FFI_DEFAULT_ABI,2,&ffi_type_sint32,types)==FFI_OK);
 ffi_call(&cif,FFI_FN(fn),&ret,args);return *(unsigned char*)&ret!=0;
}
static bool call3(void *fn,struct page **p,int index,int length){
 struct page **pp=p;ffi_type *types[]={&ffi_type_pointer,&ffi_type_sint32,&ffi_type_sint32};
 void *args[]={&pp,&index,&length};ffi_cif cif;ffi_arg ret=0;
 assert(ffi_prep_cif(&cif,FFI_DEFAULT_ABI,3,&ffi_type_sint32,types)==FFI_OK);
 ffi_call(&cif,FFI_FN(fn),&ret,args);return *(unsigned char*)&ret!=0;
}
int main(int argc,char **argv){
 assert(argc==2);int error=0;struct ain *actual=ain_open(argv[1],&error);assert(actual);
 int libno=ain_get_library(actual,"Array");assert(libno>=0);
 void *range=NULL,*predicate=NULL,*values=NULL;int declarations=0;
 for(int i=0;i<actual->libraries[libno].nr_functions;i++){
  struct ain_hll_function *f=&actual->libraries[libno].functions[i];if(strcmp(f->name,"Erase"))continue;
  printf("AIN Array:%d Erase return=%d argc=%d",i,f->return_type.data,f->nr_arguments);
  for(int j=0;j<f->nr_arguments;j++)printf(" arg%d=%d(inner=%d)",j,f->arguments[j].type.data,f->arguments[j].type.array_type?f->arguments[j].type.array_type->data:-1);
  void *fn=array_erase_function(f);assert(fn);
  if(f->nr_arguments==3){assert(fn==(void*)Array_Erase);range=fn;}
  else if(f->arguments[1].type.data==AIN_WRAP){assert(fn==(void*)Array_EraseValues);values=fn;}
  else {assert(fn==(void*)Array_EraseIf);predicate=fn;}
  declarations++;puts(" -> distinct ABI selected");
 }
 assert(declarations==3 && range && predicate && values);
 struct ain_function functions[32]={0};functions[17].nr_args=1;functions[17].return_type.data=AIN_BOOL;
 struct ain fixture={.nr_functions=32,.functions=functions};ain=&fixture;
 for(int i=1;i<128;i++){heap[i].ref=1;heap[i].type=VM_PAGE;}
 const int start[]={1,2,3,4};struct page *p=numbers(start,4,AIN_ARRAY);ended[2]=ended[3]=true;hll_current_arg3=65538;
 /* Old binding treats callback number as index and leaves these observers. */
 Array_Erase_before(&p,17,1);expect(p,start,4);assert(callbacks==0);
 assert(call2(predicate,&p,17));const int once[]={1,3,4};expect(p,once,3);assert(unrefs[2]==1 && unrefs[3]==0);
 assert(callbacks==2);assert(call2(predicate,&p,17));const int twice[]={1,4};expect(p,twice,2);
 assert(!call2(predicate,&p,17));expect(p,twice,2);assert(unrefs[1]==0 && unrefs[2]==1 && unrefs[3]==1 && unrefs[4]==0);assert(stack_ptr==0);
 free_page(p);puts("Observer regression: old binding keeps ended observers; new predicate removes first match, preserves survivors and refcounts: PASS");
 hll_current_arg3=1;const int nums[]={10,20,30,40};p=numbers(nums,4,AIN_INT);
 assert(!call3(range,&p,-1,1));assert(!call3(range,&p,0,0));expect(p,nums,4);
 assert(call3(range,&p,1,2));const int ends[]={10,40};expect(p,ends,2);
 assert(call3(range,&p,1,INT_MAX));const int first[]={10};expect(p,first,1);
 assert(call3(range,&p,0,1));assert(!p);assert(!call3(range,&p,0,1));puts("Index/length ABI, bool result, clamping and empty array: PASS");
 hll_current_arg3=0x10003;const int pairs[]={30,31,32,33};p=numbers(pairs,4,AIN_ARRAY);p->array.struct_type=2;
 Array_Erase_before(&p,0,1);assert(unrefs[30]==1 && unrefs[31]==1);free_page(p);
 heap[30].ref=heap[31].ref=1;unrefs[30]=unrefs[31]=0;
 p=numbers(pairs,4,AIN_ARRAY);p->array.struct_type=2;assert(call3(range,&p,0,1));const int pair_left[]={32,33};expect(p,pair_left,2);
 assert(unrefs[30]==1 && unrefs[31]==0 && unrefs[32]==0 && unrefs[33]==0);
 functions[17].nr_args=2;ended[32]=true;assert(call2(predicate,&p,17));assert(!p && stack_ptr==0);functions[17].nr_args=1;
 assert(unrefs[32]==1 && unrefs[33]==0);puts("Two-slot range/predicate: old erase releases metadata; new erase releases first slot only and passes both callback arguments: PASS");
 hll_current_arg3=1;
 const int dest[]={10,20,20,30,40},src[]={20,40};p=numbers(dest,5,AIN_INT);heap[100].page=numbers(src,2,AIN_INT);
 assert(call2(values,&p,100));const int diff[]={10,30};expect(p,diff,2);expect(heap[100].page,src,2);assert(!call2(values,&p,100));free_page(heap[100].page);heap[100].page=p;
 assert(call2(values,&p,100));assert(!p);heap[100].page=NULL;puts("Wrapped-source ABI: integer set subtraction, source unchanged, alias-to-self: PASS");
 hll_current_arg3=2;
 heap[20]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("alpha",5)};
 heap[21]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("beta",4)};
 heap[22]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("alpha",5)};
 heap[23]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("alpha",5)};
 const int strings[]={20,21,22},remove_strings[]={23};p=numbers(strings,3,AIN_STRING);heap[100].page=numbers(remove_strings,1,AIN_STRING);
 assert(call2(values,&p,100));const int remaining[]={21};expect(p,remaining,1);assert(unrefs[20]==1 && unrefs[22]==1 && unrefs[21]==0 && unrefs[23]==0);
 free_page(p);free_page(heap[100].page);heap[100].page=NULL;for(int i=20;i<=23;i++)free_string(heap[i].s);
 puts("Wrapped-source string values compare contents across distinct heap slots; removed references released once: PASS");
 hll_current_arg3=0x10003;heap[40].ref=2;
 heap[50]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("same",4)};
 heap[51]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("same",4)};
 const int iface[]={40,50},other_iface[]={40,51};p=numbers(iface,2,AIN_ARRAY);p->array.struct_type=2;
 heap[100].page=numbers(other_iface,2,AIN_ARRAY);heap[100].page->array.struct_type=2;
 assert(!call2(values,&p,100));expect(p,iface,2);assert(heap[40].ref==2 && unrefs[50]==0 && unrefs[51]==0);
 heap[100].page->values[1].i=50;assert(call2(values,&p,100));assert(!p && heap[40].ref==1 && unrefs[40]==1 && unrefs[50]==0 && unrefs[51]==0);
 free_page(heap[100].page);heap[100].page=NULL;free_string(heap[50].s);free_string(heap[51].s);
 puts("Two-slot source: metadata compares raw bits despite matching live string contents, only object reference is released: PASS");
 struct ain_hll_argument arg[3]={0};arg[0].type.data=AIN_REF_ARRAY;arg[1].type.data=arg[2].type.data=AIN_INT;
 struct ain_hll_function legacy={.nr_arguments=3,.arguments=arg};legacy.return_type.data=AIN_VOID;assert(array_erase_function(&legacy)==range);
 legacy.nr_arguments=2;assert(!array_erase_function(&legacy));legacy.return_type.data=AIN_BOOL;assert(!array_erase_function(&legacy));
 puts("Legacy void range preserved; unknown signatures rejected rather than called with wrong ABI: PASS");
 ain_free(actual);puts("Array.Erase production fixture: ALL PASS");return 0;
}
'''
fixture=HERE/'array_erase_fixture.c';fixture.write_text(prefix+snippets+'\n'+legacy+'\n'+tests)
binary=HERE/'array_erase_fixture'
command=['/Library/Developer/CommandLineTools/usr/bin/clang','-std=c11','-O0','-g','-fsanitize=address,undefined','-I'+str(SOURCE/'include'),'-I'+str(SOURCE/'subprojects/libsys4/include'),'-I/opt/homebrew/opt/libffi/include',str(fixture),str(STAGE2.parent/'stage1/wip-asan-build/subprojects/libsys4/libsys4.a'),'-L/opt/homebrew/opt/libffi/lib','-lffi','-lz','-lm','-framework','CoreFoundation','-o',str(binary)]
compiled=subprocess.run(command,capture_output=True,text=True)
if compiled.returncode:print(compiled.stderr);raise SystemExit(compiled.returncode)
env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
ran=subprocess.run([str(binary),str(STAGE2/'game/dohnadohna.ain')],env=env,capture_output=True,text=True,errors="replace")
result={'production_source':str(path),'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'compile_command':command,'compile_exit':compiled.returncode,'compile_stderr':compiled.stderr,'test_exit':ran.returncode,'stdout':ran.stdout,'stderr':ran.stderr}
(HERE/'array_erase_result.json').write_text(json.dumps(result,indent=2)+'\n');print(ran.stdout);print(ran.stderr);raise SystemExit(ran.returncode)
