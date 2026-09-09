from pathlib import Path
import hashlib, json, os, subprocess
P=Path(__file__).resolve().parent
S=P.parent/'source'
def func(t,s):
 a=t.index(s);b=t.index('{',a);d=1;e=b+1
 while d:
  if t[e]=='{':d+=1
  elif t[e]=='}':d-=1
  e+=1
 return t[a:e]
heap=(S/'src/heap.c').read_text();page=(S/'src/page.c').read_text();resume=(S/'src/resume.c').read_text()
pre=r'''
#include <stdarg.h>
#include <stdio.h>
#include "../source/src/heap.c"
struct ain *ain;
union vm_value stack_storage[64];
union vm_value *stack=stack_storage;
int32_t stack_ptr,call_stack_ptr;
struct function_call call_stack[4096];
size_t instr_ptr;
static unsigned clock_ms=10000;
uint32_t SDL_GetTicks(void){return clock_ms;}
_Noreturn void _vm_error(const char *fmt,...){va_list ap;va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);abort();}
void vm_stage2_trace_page_event(const char*a,int b,int c,int d,int e){}
static int deleted_pages, dtor_slot=-1, dtor_child=-1, dtor_allocated=-1, dtor_runs;
void free_page(struct page*p){deleted_pages++;free(p);}
enum ain_data_type variable_type(struct page*p,int i,int*s,int*r){return AIN_STRUCT;}
enum ain_data_type array_type(enum ain_data_type t){return AIN_INT;}
static void (*dtor_hook)(int slot);
void delete_struct(int no,int slot){if(dtor_hook)dtor_hook(slot);}
'''
code='\n'.join(func(page,s) for s in ['void variable_fini(', 'void delete_page_vars(', 'void delete_page('])
code+='\n'+func(resume,'static void delete_heap(')+'\n'+func(resume,'static void heap_rebuild_free_list(')
code+='\n'+func((P/'heap-before-freelist.c').read_text(),'static void heap_free_slot(int32_t slot)\n{').replace('heap_free_slot(', 'heap_free_slot_before(',1)
post=r'''
static struct page*new_page(enum page_type type,int vars){struct page*p=calloc(1,sizeof(*p)+vars*sizeof(union vm_value));assert(p);p->type=type;p->nr_vars=vars;p->index=0;for(int i=0;i<vars;i++)p->values[i].i=-1;return p;}
static int page_slot(enum page_type type,int vars){int s=heap_alloc_slot(VM_PAGE);heap_set_page(s,new_page(type,vars));return s;}
static void reset(void){
 if(heap){for(size_t i=2;i<heap_size;i++)if(heap[i].ref>0){if(heap[i].type==VM_PAGE)free_page(heap[i].page);else if(heap[i].type==VM_STRING)free_string(heap[i].s);}if(heap_is_mmap)munmap(heap,HEAP_MMAP_RESERVE);else free(heap);heap=NULL;}
 heap_scan_limit=2;gc_inhibit=0;gc_alloc_counter=gc_last_alloc=gc_last_completed_ms=0;gc_has_run=false;dtor_hook=NULL;heap_init();
}
static void check_list(void){
 unsigned char*seen=calloc(heap_size,1);size_t n=0;
 for(int s=heap_free_head;s>=0;s=(int32_t)heap[s].seq){assert(s>1 && (size_t)s<heap_size && !seen[s]);seen[s]=1;assert(heap[s].ref==0 && heap[s].type==VM_FREE);n++;assert(n<=heap_size);}
 assert(n==heap_free_count);
 for(size_t i=2;i<heap_size;i++)if(heap[i].ref>0)assert(!seen[i]);
 free(seen);
}
static void destructor_allocate(int slot){
 if(slot!=dtor_slot)return;
 dtor_runs++;assert(gc_inhibit>0);heap_unref(dtor_child);assert(heap[dtor_child].ref==0);
 size_t old_size=heap_size;
 dtor_allocated=heap_alloc_slot(VM_PAGE);
 assert(dtor_allocated!=dtor_slot && dtor_allocated!=dtor_child);
 assert(heap_size>old_size && !gc_has_run); /* GC deferred, growth still available. */
 assert(heap[dtor_child].page && heap[dtor_child].type==VM_PAGE);
}
int main(void){
 struct ain a={.version=14,.nr_structures=1};ain=&a;
 reset();check_list();int s=page_slot(LOCAL_PAGE,0);size_t n=heap_free_count;
 /* Exact old production guard fails to return a normal VM_PAGE slot. */
 free_page(heap[s].page);heap[s].page=NULL;heap[s].ref=0;heap_free_slot_before(s);assert(heap_free_count==n && heap_free_head!=s);
 heap_free_slot(s);assert(heap_free_count==n+1 && heap_free_head==s);check_list();
 puts("Old production page-free guard loses slot; new production membership returns it exactly once: PASS");
 reset();s=page_slot(LOCAL_PAGE,0);n=heap_free_count;int before=deleted_pages;heap_unref(s);assert(deleted_pages==before+1 && heap_free_count==n+1 && heap_free_head==s && !heap[s].page);check_list();
 heap_unref(s);heap_free_slot(s);assert(heap_free_count==n+1);check_list();assert(heap_alloc_slot(VM_PAGE)==s && heap[s].type==VM_PAGE && heap[s].ref==1);heap_unref(s);
 for(int i=0;i<100000;i++){int t=page_slot(LOCAL_PAGE,0);assert(t==s);heap_unref(t);}assert(heap_size==INITIAL_HEAP_SIZE && !gc_has_run && heap_free_count==INITIAL_HEAP_SIZE-2);check_list();
 puts("100000 actual heap_unref/delete_page/allocate cycles reuse page slot without GC or heap growth; double-free never forms a list cycle: PASS");
 s=heap_alloc_string(make_string("hello",5));n=heap_free_count;heap_unref(s);assert(heap_free_count==n+1 && heap_free_head==s && heap[s].type==VM_FREE);heap_unref(s);heap_free_slot(s);check_list();assert(heap_alloc_slot(VM_STRING)==s);heap[s].s=make_string("bye",3);exit_unref(s);heap_free_slot(s);check_list();
 puts("Strings, exit_unref, page/string replacement and duplicate free preserve exact membership: PASS");
 reset();int live=page_slot(LOCAL_PAGE,0);s=page_slot(LOCAL_PAGE,0);heap_unref(s);heap_gc();assert(heap[live].ref==1);for(size_t i=2;i<heap_size;i++)if(heap[i].ref==0)assert(heap[i].type==VM_FREE);n=heap_free_count;heap_free_slot(s);assert(heap_free_count==n);check_list();
 puts("Production GC rebuild tags allocated and unallocated tail slots; live objects kept, repeated free suppressed: PASS");
 delete_heap();assert(!heap[live].ref);/* Pointer was freed by production delete_heap; rebuild must clear it. */
 heap[live].ref=1;heap[live].type=VM_PAGE;heap[live].page=new_page(LOCAL_PAGE,0);heap_rebuild_free_list();check_list();for(size_t i=2;i<heap_size;i++)if(!heap[i].ref)assert(heap[i].type==VM_FREE && !heap[i].page);
 s=heap_free_head;n=heap_free_count;heap_free_slot(s);assert(heap_free_count==n);assert(heap_alloc_slot(VM_PAGE)==s);heap_unref(s);check_list();
 puts("Production resume delete/rebuild tags loaded free slots, clears freed payload pointers, keeps loaded live slots, and allows safe reuse: PASS");
 reset();heap_grow(16384);check_list();
 dtor_slot=page_slot(STRUCT_PAGE,1);dtor_child=page_slot(LOCAL_PAGE,0);heap[dtor_slot].page->values[0].i=dtor_child;
 while(heap_free_count)heap_alloc_slot(VM_PAGE);
 dtor_runs=0;dtor_hook=destructor_allocate;gc_has_run=false;heap_unref(dtor_slot);assert(dtor_runs==1 && gc_inhibit==0 && !gc_has_run);assert(gc_inhibit==0 && heap[dtor_child].type==VM_FREE && heap[dtor_slot].type==VM_FREE && heap[dtor_allocated].ref==1);check_list();
 s=page_slot(LOCAL_PAGE,0);heap_gc_inhibit();heap_unref(s);assert(gc_inhibit==1);heap_gc_allow();assert(gc_inhibit==0);
 puts("Production deferred destructor path preserves pending child/current slot, balanced outer inhibit and exhaustion growth: PASS");
 reset();heap_grow(16384);while(heap_free_count>=1024)heap_alloc_slot(VM_PAGE);clock_ms=4999;heap_alloc_slot(VM_PAGE);assert(!gc_has_run);clock_ms=5000;heap_alloc_slot(VM_PAGE);assert(gc_has_run);check_list();
 puts("Moderate heap first GC retains original >=5000ms gate: PASS");
 reset();if(heap_is_mmap)munmap(heap,HEAP_MMAP_RESERVE);else free(heap);heap=NULL;puts("Free-list production regression fixture: ALL PASS");return 0;
}
'''
f=P/'freelist_fixture.c';f.write_text(pre+code+post)
cmd=['clang','-std=c11','-O0','-g','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk','-fsanitize=address,undefined','-Wl,-dead_strip','-I'+str(S/'include'),'-I'+str(S/'subprojects/libsys4/include'),'-I/opt/homebrew/include/SDL2',str(f),str(P.parent.parent/'stage1/wip-asan-build/subprojects/libsys4/libsys4.a'),'-lz','-lm','-framework','CoreFoundation','-o',str(P/'freelist_fixture')]
r=subprocess.run(cmd,capture_output=True,text=True);result={'compile_exit':r.returncode,'compile_stderr':r.stderr,'sources_sha256':{str(x.relative_to(S)):hashlib.sha256(x.read_bytes()).hexdigest() for x in [S/'src/heap.c',S/'src/resume.c',S/'include/vm/heap.h']},'fixture_sha256':hashlib.sha256(f.read_bytes()).hexdigest()};print(r.stderr)
if not r.returncode:
 q=subprocess.run([str(P/'freelist_fixture')],capture_output=True,text=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1'));result.update(test_exit=q.returncode,stdout=q.stdout,stderr=q.stderr);print(q.stdout);print(q.stderr)
(P/'freelist-result.json').write_text(json.dumps(result,indent=2)+'\n');raise SystemExit(result.get('test_exit',r.returncode))
