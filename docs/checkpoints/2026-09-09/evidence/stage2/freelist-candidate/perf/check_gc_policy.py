from pathlib import Path
import subprocess,hashlib,json,os
P=Path(__file__).resolve().parent;S=P.parent/'source';source=(S/'src/heap.c').read_text()
def func(t,s):
 a=t.index(s);b=t.index('{',a);d=1;e=b+1
 while d:
  if t[e]=='{':d+=1
  elif t[e]=='}':d-=1
  e+=1
 return t[a:e]
old=subprocess.run(['git','show','HEAD:src/heap.c'],cwd=S,capture_output=True,text=True,check=True).stdout
prefix=r'''
#define VM_PRIVATE
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#endif
#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/page.h"
#include "vm/heap.h"
struct ain *ain;
static struct vm_pointer pointers[2048];
struct vm_pointer *heap=pointers;
size_t heap_size,heap_free_count;
int32_t heap_free_head;
uint32_t heap_next_seq;
int global_page_slot=1;
struct function_call call_stack[4096];
int32_t call_stack_ptr,stack_ptr;
static union vm_value stack_storage[64];
union vm_value *stack=stack_storage;
static size_t heap_scan_limit;
static bool heap_is_mmap;
static int gc_inhibit;
static uint8_t *gc_mark;
static size_t gc_mark_size;
static uint8_t gc_gen;
#define GC_IS_MARKED(slot) (gc_mark[slot]==gc_gen)
#define GC_SET_MARK(slot) (gc_mark[slot]=gc_gen)
#define GC_WORK_STACK_SIZE (1<<20)
#define HEAP_GROW_LINEAR_STEP (4*1024*1024)
static int *gc_work_stack,gc_work_stack_size;
static uint8_t **gc_struct_refmap;
static int gc_struct_refmap_size;
static unsigned long long gc_alloc_counter,gc_last_alloc;
static uint32_t gc_last_completed_ms;
static bool gc_has_run;
static const bool gc_collect_cycles=false;
static int scan_calls,gc_calls,grow_calls;
static uint32_t now_ms,gc_duration;
uint32_t SDL_GetTicks(void){return now_ms;}
enum ain_data_type variable_type(struct page*p,int n,int*s,int*r){return AIN_INT;}
enum ain_data_type array_type(enum ain_data_type type){return AIN_INT;}
void free_page(struct page*p){free(p);}
static void fixture_timed_gc(void){gc_calls++;now_ms+=gc_duration;}
void heap_grow(size_t size){grow_calls++;heap_size=size;heap_free_head=1500;heap_free_count=1;heap[1500].ref=0;heap[1500].seq=(uint32_t)-1;}
'''
helpers='\n'.join(func(source,s) for s in ['static bool gc_type_is_ref(', 'static const uint8_t *gc_get_struct_refmap(', 'static void gc_init_struct_cache('])
scan=func(source,'static void gc_scan_page(').replace('gc_scan_page(', 'gc_scan_page_impl(',1)
helpers+='\n'+scan+'\nstatic void gc_scan_page(struct page*p,int*w,int*n){scan_calls++;gc_scan_page_impl(p,w,n);}\n'
helpers+='\n'.join(func(source,s) for s in ['static void gc_ensure_work_stack(', 'static void gc_mark_slot(int slot)\n{'])
collectors=func(source,'static void heap_gc(void)\n{')+'\n'+func(old,'static void heap_gc(void)\n{').replace('heap_gc(', 'heap_gc_before(',1)
allocators='\n#define heap_gc fixture_timed_gc\n'+func(source,'int32_t heap_alloc_slot(')+'\n'+func(old,'int32_t heap_alloc_slot(').replace('heap_alloc_slot(', 'heap_alloc_slot_before(',1)+'\n#undef heap_gc\n'
tests=r'''
static struct page*page(int slot,enum page_type type,int n){
 struct page*p=calloc(1,sizeof(*p)+n*sizeof(union vm_value));assert(p);p->type=type;p->nr_vars=n;heap[slot]=(struct vm_pointer){.ref=1,.type=VM_PAGE,.page=p};return p;
}
static void graph(void){
 memset(heap,0,sizeof(pointers));heap_size=64;heap_scan_limit=16;heap_is_mmap=false;call_stack_ptr=stack_ptr=0;scan_calls=0;
 page(1,GLOBAL_PAGE,2)->values[0].i=2;heap[1].page->values[1].i=7;
 page(2,LOCAL_PAGE,1)->values[0].i=3;page(7,LOCAL_PAGE,1)->values[0].i=3;
 page(6,LOCAL_PAGE,1)->values[0].i=6; /* Existing collector leaves unreachable cycles. */
 for(int i=3;i<=4;i++)heap[i]=(struct vm_pointer){.ref=i==3?1:0,.type=VM_STRING,.s=make_string("data",4)};
 heap[8]=(struct vm_pointer){.ref=-1,.type=VM_STRING,.s=make_string("orphan",6)};
 heap[9]=(struct vm_pointer){.ref=1,.type=VM_STRING,.s=make_string("unreachable",11)};
}
struct snapshot {int ref[64],type[64],seq[64],has_resource[64],free_head;size_t free_count,scan_limit;};
static struct snapshot snapshot(void){struct snapshot s={0};for(int i=0;i<64;i++){s.ref[i]=heap[i].ref;s.type[i]=heap[i].ref==0 && heap[i].type==VM_FREE ? VM_PAGE : heap[i].type;s.seq[i]=heap[i].seq;s.has_resource[i]=heap[i].page!=NULL;}s.free_head=heap_free_head;s.free_count=heap_free_count;s.scan_limit=heap_scan_limit;return s;}
static void clear_graph(void){for(int i=1;i<64;i++)if(heap[i].page){if(heap[i].type==VM_STRING)free_string(heap[i].s);else free_page(heap[i].page);heap[i].page=NULL;}}
static void schedule(size_t size,size_t available){
 memset(heap,0,sizeof(pointers));heap_size=size;heap_scan_limit=2;heap_free_head=1000;heap_free_count=available;heap_next_seq=1;
 for(int i=1000;i<2000;i++)heap[i].seq=i==1999?(uint32_t)-1:(uint32_t)(i+1);
 gc_alloc_counter=gc_last_alloc=gc_last_completed_ms=0;gc_has_run=false;gc_inhibit=0;gc_calls=grow_calls=0;now_ms=5000;gc_duration=0;
}
int main(void){
 struct ain a={.version=14};ain=&a;
 graph();heap_gc_before();struct snapshot before=snapshot();int scans_before=scan_calls;
 assert(scans_before>=3 && !heap[4].s && !heap[8].s && heap[6].ref==1 && heap[9].ref==1);clear_graph();
 graph();heap_gc();struct snapshot after=snapshot();assert(!memcmp(&before,&after,sizeof(before)) && scan_calls==0);clear_graph();
 puts("Production heap_gc comparison normalizes ONLY ref==0 free tag VM_FREE to prior VM_PAGE(0); live/dead/ref/resource/free-list/scan-limit identical; ref<=0 string orphans reclaimed; existing live cycles kept; mark traversals fall from >=3 to zero: PASS");
 schedule(4000000,1023);gc_duration=6000;for(int i=0;i<100;i++)heap_alloc_slot_before(VM_PAGE);assert(gc_calls==100);
 schedule(4000000,1023);gc_duration=6000;for(int i=0;i<100;i++)heap_alloc_slot(VM_PAGE);assert(gc_calls==1 && gc_last_completed_ms==11000 && gc_last_alloc==1);
 puts("Production allocator at >=4M/low-free, 100 allocations with 6s GC: old calls GC100 times; new calls once and records completion time: PASS");
 schedule(2000000,1023);gc_duration=6000;for(int i=0;i<3;i++)heap_alloc_slot_before(VM_PAGE);assert(gc_calls==3);
 schedule(2000000,1023);gc_duration=6000;for(int i=0;i<3;i++)heap_alloc_slot(VM_PAGE);assert(gc_calls==1);
 puts("Moderate heap: 6s GC no longer immediately satisfies its own 5s cooldown on next allocation: PASS");
 now_ms=gc_last_completed_ms+5000;heap_alloc_slot(VM_PAGE);assert(gc_calls==1); /* time alone insufficient */
 gc_alloc_counter=gc_last_alloc+heap_size/8;now_ms=gc_last_completed_ms+4999;heap_alloc_slot(VM_PAGE);assert(gc_calls==1);
 now_ms++;heap_alloc_slot(VM_PAGE);assert(gc_calls==2); /* both conditions met */
 gc_alloc_counter=gc_last_alloc+heap_size/8;gc_last_completed_ms=UINT32_MAX-2000;now_ms=2999;gc_duration=0;heap_alloc_slot(VM_PAGE);assert(gc_calls==3);
 puts("Both allocation budget and completion cooldown required; collections resume when eligible; SDL uint32 wrap handled: PASS");
 schedule(9999,1023);heap_alloc_slot(VM_PAGE);assert(!gc_calls);
 schedule(10000,1024);heap_alloc_slot(VM_PAGE);assert(!gc_calls);heap_alloc_slot(VM_PAGE);assert(gc_calls==1);
 schedule(10000,1023);gc_inhibit=1;heap_alloc_slot(VM_PAGE);assert(!gc_calls);gc_inhibit=0;heap_alloc_slot(VM_PAGE);assert(gc_calls==1);
 schedule(10000,1023);now_ms=4999;heap_alloc_slot(VM_PAGE);assert(!gc_calls);now_ms=5000;heap_alloc_slot(VM_PAGE);assert(gc_calls==1);
 schedule(10000,0);gc_has_run=true;gc_last_completed_ms=now_ms;heap_free_head=-1;assert(heap_alloc_slot(VM_PAGE)==1500 && grow_calls==1 && !gc_calls);
 puts("Heap9999/10000, free1023/1024, inhibit/release, first eligible collection and exhausted free-list fallback: PASS");
 free(gc_mark);free(gc_work_stack);free(gc_struct_refmap);puts("GC production policy fixture: ALL PASS");return 0;
}
'''
f=P/'gc_policy_fixture.c';f.write_text(prefix+helpers+'\n'+collectors+allocators+tests);b=P/'gc_policy_fixture'
cmd=['clang','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk','-std=c11','-O0','-g','-fsanitize=address,undefined','-I'+str(S/'include'),'-I'+str(S/'subprojects/libsys4/include'),str(f),str(P.parent.parent/'stage1/wip-asan-build/subprojects/libsys4/libsys4.a'),'-lz','-lm','-framework','CoreFoundation','-o',str(b)]
r=subprocess.run(cmd,capture_output=True,text=True);result={'compile_exit':r.returncode,'compile_stderr':r.stderr,'source_sha256':hashlib.sha256((S/'src/heap.c').read_bytes()).hexdigest(),'fixture_sha256':hashlib.sha256(f.read_bytes()).hexdigest()};print(r.stderr)
if not r.returncode:
 e=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1');q=subprocess.run([str(b)],capture_output=True,text=True,env=e);result.update(test_exit=q.returncode,stdout=q.stdout,stderr=q.stderr);print(q.stdout);print(q.stderr)
(P/'gc-policy-result.json').write_text(json.dumps(result,indent=2)+'\n');raise SystemExit(result.get('test_exit',r.returncode))
