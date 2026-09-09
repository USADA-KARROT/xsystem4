from pathlib import Path
import json,hashlib,subprocess,os
HERE=Path(__file__).resolve().parent
STAGE2=HERE.parent
SOURCE=STAGE2/'source'
source=(SOURCE/'src/vm.c').read_text();page_source=(SOURCE/'src/page.c').read_text()
def func(text,sig):
 start=text.index(sig);brace=text.index('{',start);depth=1;end=brace+1
 while depth:
  if text[end]=='{':depth+=1
  elif text[end]=='}':depth-=1
  end+=1
 return text[start:end]
snippets='\n'.join(func(source,s) for s in ['static int ain_return_slots_type(', 'static bool delegate_arg_is_2slot(', 'static int delegate_param_slots(', 'static int delegate_return_slots(', 'static union vm_value delegate_copy_argument(', 'static void delegate_call('])
old=subprocess.run(['git','show','HEAD:src/vm.c'],cwd=SOURCE,capture_output=True,text=True,check=True).stdout
before=func(old,'static void delegate_call(').replace('delegate_call(', 'delegate_call_before(',1)
fini=func(page_source,'void variable_fini(')
prefix=r'''
#define VM_PRIVATE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "vm.h"
#include "vm/page.h"
#include "vm/heap.h"
struct ain *ain;
static union vm_value stack_storage[64];
union vm_value *stack=stack_storage;
int32_t stack_ptr,call_stack_ptr;
struct function_call call_stack[4096];
size_t instr_ptr;
static unsigned char *func_flags;
#define FUNC_FLAG_LAMBDA 1
static struct vm_pointer heap_storage[128];
struct vm_pointer *heap=heap_storage;
size_t heap_size=128;
static int refs[128],unrefs[128],frees[128],next_slot;
_Noreturn void _vm_error(const char *fmt,...){abort();}
union vm_value stack_pop(void){assert(stack_ptr>0);return stack[--stack_ptr];}
union vm_value stack_peek(int n){assert(n>=0 && n<stack_ptr);return stack[stack_ptr-1-n];}
bool heap_index_valid(int n){return n>0 && n<128 && heap[n].ref>0;}
void heap_ref(int n){assert(heap_index_valid(n));heap[n].ref++;refs[n]++;}
void variable_fini(union vm_value v,enum ain_data_type type,bool call_dtor);
void heap_unref(int n){
 if(n<=1)return;
 assert(heap_index_valid(n));unrefs[n]++;if(--heap[n].ref)return;
 struct page*p=heap[n].page;heap[n].page=NULL;frees[n]++;
 if(p){
  if(p->type==LOCAL_PAGE)for(int i=p->nr_vars-1;i>=0;i--)variable_fini(p->values[i],ain->functions[p->index].vars[i].type.data,true);
  free(p);
 }
}
void exit_unref(int n){heap_unref(n);}
static struct page *make_page(int slot,enum page_type t,int fno,int count){
 assert(!heap[slot].ref);struct page*p=calloc(1,sizeof(*p)+count*sizeof(union vm_value));assert(p);
 p->type=t;p->index=fno;p->nr_vars=count;heap[slot]=(struct vm_pointer){.ref=1,.type=VM_PAGE,.page=p};return p;
}
struct page *heap_get_delegate_page(int n){assert(heap_index_valid(n));return heap[n].page;}
bool delegate_get(struct page*p,int i,int*obj,int*fn){
 if(i*3>=p->nr_vars)return false;*obj=p->values[i*3].i;*fn=p->values[i*3+1].i;return true;
}
static int _function_call(int fno,int ret){
 int slot=next_slot++;make_page(slot,LOCAL_PAGE,fno,ain->functions[fno].nr_vars);
 call_stack[call_stack_ptr++]=(struct function_call){.fno=fno,.page_slot=slot,.return_address=ret,.struct_page=-1};return slot;
}
static void set_struct_page(int n){call_stack[call_stack_ptr-1].struct_page=n;}
'''
tests=r'''
static void setup(void){
 memset(heap,0,sizeof(heap_storage));memset(refs,0,sizeof(refs));memset(unrefs,0,sizeof(unrefs));memset(frees,0,sizeof(frees));
 stack_ptr=call_stack_ptr=0;next_slot=20;instr_ptr=0x4a59ea;
 make_page(10,LOCAL_PAGE,20752,1);struct page*d=make_page(11,DELEGATE_PAGE,0,6);
 for(int i=0;i<2;i++){d->values[i*3].i=12;d->values[i*3+1].i=36081;d->values[i*3+2].i=13;}
 make_page(12,STRUCT_PAGE,0,0);make_page(13,LOCAL_PAGE,27031,4);
 call_stack[call_stack_ptr++]=(struct function_call){.fno=20752,.page_slot=10,.struct_page=12};
 stack_push(123456);stack_push(10);stack_push(0);stack_push(11);stack_push(0);
}
static void callback_and_cleanup(void){
 int slot=call_stack[call_stack_ptr-1].page_slot;struct page*p=heap[slot].page;
 assert(p->index==36081 && p->nr_vars==2 && p->values[0].i==10 && p->values[1].i==0);
 /* The real trace verifies X_ASSIGN performs this alias write correctly. */
 heap[p->values[0].i].page->values[p->values[1].i].i=1;
 call_stack_ptr--;heap_unref(slot);
}
static void cleanup(void){for(int i=10;i<=13;i++)if(heap[i].ref)heap_unref(i);}
int main(int argc,char**argv){
 assert(argc==2);int err=0;ain=ain_open(argv[1],&err);assert(ain && ain->version>=14);
 initialize_instructions(ain->version);func_flags=calloc(ain->nr_functions,1);assert(func_flags);
 assert(ain->functions[20752].nr_vars==1 && ain->functions[20752].vars[0].type.data==AIN_BOOL);
 assert(ain->functions[36081].nr_args==2 && ain->functions[36081].vars[0].type.data==AIN_REF_BOOL && ain->functions[36081].vars[1].type.data==AIN_VOID);
 assert(ain->delegates[248].nr_arguments==2 && ain->delegates[248].variables[0].type.data==AIN_REF_BOOL && ain->delegates[248].variables[1].type.data==AIN_VOID);
 printf("Actual AIN f36081/ref-bool + void, delegate248/ref-bool + void, caller20752/bool verified\n");
 setup();delegate_call_before(248,0x4a59fa);assert(heap[10].ref==1);callback_and_cleanup();assert(!heap[10].ref && !heap[10].page && frees[10]==1);cleanup();
 puts("Before production delegate_call: End alias writes 1, callee variable_fini releases active caller page: reproduced");
 setup();
 for(int i=0;i<2;i++){
  delegate_call(248,0x4a59fa);assert(heap[10].ref==2 && refs[10]==i+1 && stack_ptr==5);
  assert(call_stack[call_stack_ptr-1].env_page==13);
  callback_and_cleanup();assert(heap[10].ref==1 && heap[10].page->values[0].i==1 && frees[10]==0);
 }
 instr_ptr=0x4a59ea;delegate_call(248,0x4a59fa);assert(stack_ptr==1 && stack[0].i==123456 && instr_ptr==0x4a59fa);
 assert(heap[10].ref==1 && refs[10]==2 && unrefs[10]==2 && refs[13]==0 && unrefs[13]==0);
 cleanup();assert(frees[10]==1);puts("After production delegate_call: both handlers retain same ref target; End survives cleanup; counts balance; delegate final stack cleanup preserves caller: PASS");
 setup();int types[]={AIN_REF_BOOL,AIN_REF_INT,AIN_REF_FLOAT,AIN_REF_LONG_INT,AIN_REF_STRING,AIN_REF_STRUCT,AIN_REF_ARRAY};
 for(unsigned i=0;i<sizeof(types)/sizeof(types[0]);i++){
  union vm_value v=delegate_copy_argument((union vm_value){.i=10},types[i]);assert(v.i==10 && heap[10].ref==2);
  variable_fini(v,types[i],true);assert(heap[10].ref==1);
 }
 int nrefs=refs[10];assert(delegate_copy_argument((union vm_value){.i=10},AIN_VOID).i==10);assert(delegate_copy_argument((union vm_value){.i=10},AIN_INT).i==10);assert(refs[10]==nrefs);
 assert(delegate_copy_argument((union vm_value){.i=-1},AIN_REF_BOOL).i==-1);assert(delegate_copy_argument((union vm_value){.i=0},AIN_REF_BOOL).i==0);
 int version=ain->version;ain->version=13;assert(delegate_copy_argument((union vm_value){.i=10},AIN_REF_BOOL).i==10 && refs[10]==nrefs);ain->version=version;
 cleanup();puts("Reference family aliases/refcounts, primitive and companion values, null references, unchanged pre-v14 path: PASS");
 free(func_flags);ain_free(ain);puts("Delegate reference production fixture: ALL PASS");return 0;
}
'''
fixture=HERE/'delegate_reference_fixture.c';fixture.write_text(prefix+fini+'\n'+snippets+'\n'+before+'\n'+tests)
binary=HERE/'delegate_reference_fixture'
cmd=['clang','-std=c11','-O0','-g','-fsanitize=address,undefined','-I'+str(SOURCE/'include'),'-I'+str(SOURCE/'subprojects/libsys4/include'),str(fixture),str(STAGE2.parent/'stage1/wip-asan-build/subprojects/libsys4/libsys4.a'),'-lz','-lm','-framework','CoreFoundation','-o',str(binary)]
p=subprocess.run(cmd,capture_output=True,text=True);print(p.stderr)
r={'compile_command':cmd,'compile_exit':p.returncode,'compile_stderr':p.stderr,'production_vm_sha256':hashlib.sha256((SOURCE/'src/vm.c').read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256(fixture.read_bytes()).hexdigest()}
if not p.returncode:
 env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 q=subprocess.run([str(binary),str(STAGE2/'game/dohnadohna.ain')],env=env,capture_output=True,text=True,errors='replace');r.update(test_exit=q.returncode,stdout=q.stdout,stderr=q.stderr);print(q.stdout);print(q.stderr)
(HERE/'delegate-reference-result.json').write_text(json.dumps(r,indent=2)+'\n');raise SystemExit(r.get('test_exit',p.returncode))
