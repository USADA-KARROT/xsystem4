from pathlib import Path
import hashlib,json,subprocess

HERE=Path(__file__).resolve().parent
STAGE2=HERE.parent
SOURCE=STAGE2/'source'
path=SOURCE/'src/vm.c'
source=path.read_text()
def case(text):
    start=text.index('\tcase S_PLUSA:')
    end=text.index('\tcase S_ADD:',start)
    return text[start:end]
production=case(source)
old=case(subprocess.run(['git','show','HEAD:src/vm.c'],cwd=SOURCE,capture_output=True,text=True,check=True).stdout)
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system4/ain.h"
#include "system4/instructions.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
static union vm_value values[64];
union vm_value *stack=values;
int32_t stack_ptr;
static struct vm_pointer pointers[1024];
struct vm_pointer *heap=pointers;
size_t heap_size=1024;
static int next_slot=20;
int32_t heap_alloc_slot(enum vm_pointer_type type) {
    assert(next_slot<1024);int i=next_slot++;
    heap[i].type=type;heap[i].ref=1;return i;
}
bool string_index_valid(int i) {return i>=0 && i<1024 && HEAP_REF(i)>0 && heap[i].type==VM_STRING;}
struct string *heap_get_string(int i) {return string_index_valid(i)&&heap[i].s?heap[i].s:&EMPTY_STRING;}
void heap_unref(int i) {
    assert(i>0 && i<1024 && HEAP_REF(i)>0);
    heap[i].ref--;
    if(!HEAP_REF(i)) {assert(heap[i].type==VM_STRING);free_string(heap[i].s);heap[i].s=NULL;}
}
union vm_value stack_peek(int n) {assert(stack_ptr>n);return stack[stack_ptr-1-n];}
union vm_value stack_pop(void) {assert(stack_ptr>0);return stack[--stack_ptr];}
static union vm_value *stack_pop_var(void) {
    int index=stack_pop().i,page=stack_pop().i;
    assert(page>0 && page<20 && heap[page].type==VM_PAGE && HEAP_REF(page)==1);
    assert(index>=0 && index<heap[page].page->nr_vars);
    return &heap[page].page->values[index];
}
static void stack_push_string(struct string *s) {
    int i=heap_alloc_slot(VM_STRING);heap[i].s=s;heap[i].ref|=HEAP_TEMP_FLAG;stack_push(i);
}
static int new_string(const char *s) {
    int i=heap_alloc_slot(VM_STRING);heap[i].s=make_string(s,strlen(s));return i;
}
'''
wrappers='static void execute(int opcode) { switch(opcode) {\n'+production+'default: assert(0); } }\n'
wrappers+='static void execute_before(int opcode) { switch(opcode) {\n'+old+'default: assert(0); } }\n'
tests=r'''
static void expect_string(int slot,const char *s) {
    assert(string_index_valid(slot));assert(!strcmp(heap[slot].s->text,s));
}
static void run_one(int version,int opcode,int page,int index,bool self_append) {
    initialize_instructions(version);
    bool modern=instructions[CALLMETHOD].args[0]==T_INT;
    int lhs=new_string("\xc4\xe3\xba\xc3"),rhs;
    struct string *old_value=string_ref(heap[lhs].s); /* Independent string value alias. */
    if(self_append) {rhs=heap_alloc_slot(VM_STRING);heap[rhs].s=string_ref(heap[lhs].s);}
    else rhs=new_string("!");
    heap[page].page->values[index].i=lhs;
    stack_ptr=0;stack_push(123456);
    if(modern){stack_push(page);stack_push(index);}else stack_push(lhs);
    stack_push(rhs);execute(opcode);
    assert(stack_ptr==2 && stack[0].i==123456);
    int result=stack_pop().i;
    assert(result!=lhs && !HEAP_REF(rhs) && heap[page].page->values[index].i==lhs);
    assert(HEAP_REF(lhs)==1 && HEAP_REF(result)==1);
    const char *expected=self_append?"\xc4\xe3\xba\xc3\xc4\xe3\xba\xc3":"\xc4\xe3\xba\xc3!";
    expect_string(lhs,expected);expect_string(result,expected);
    assert(!strcmp(old_value->text,"\xc4\xe3\xba\xc3"));
    expect_string(9,"canary"); /* Variable index must never be treated as heap slot. */
    heap_unref(result);expect_string(lhs,expected); /* Caller DELETE does not free lvalue. */
    heap_unref(lhs);free_string(old_value);stack_ptr=0;
}
int main(void) {
    unsigned empty_before=EMPTY_STRING.ref;
    for(int i=1;i<=2;i++) {
        heap[i].type=VM_PAGE;heap[i].ref=1;
        heap[i].page=calloc(1,sizeof(struct page)+10*sizeof(union vm_value));
        heap[i].page->nr_vars=10;heap[i].page->type=i==1?LOCAL_PAGE:STRUCT_PAGE;
    }
    heap[9].type=VM_STRING;heap[9].ref=1;heap[9].s=make_string("canary",6);
    /* Reproduce first CreateDrawChar: lvalue is local[9], not heap[9]. */
    initialize_instructions(14);
    int lhs=new_string(""),rhs=new_string("CN");heap[1].page->values[9].i=lhs;
    stack_push(123456);stack_push(1);stack_push(9);stack_push(rhs);
    execute_before(S_PLUSA2);
    assert(stack_ptr==3);expect_string(lhs,"");expect_string(9,"canaryCN");
    heap_unref(stack_pop().i);heap_unref(lhs);stack_ptr=0;
    free_string(heap[9].s);heap[9].s=make_string("canary",6);
    puts("Before: two-slot lvalue leaves extra stack item, mutates heap[index], keeps dialogue empty: reproduced");
    for(int version=6;version<=14;version+=(version==6?5:3))
      for(int opcode_index=0;opcode_index<2;opcode_index++)
        for(int page=1;page<=2;page++)
          for(int self_append=0;self_append<2;self_append++)
            run_one(version,opcode_index?S_PLUSA2:S_PLUSA,page,page==1?9:4,self_append);
    heap_unref(9);free(heap[1].page);free(heap[2].page);
    for(int i=20;i<next_slot;i++)assert(!HEAP_REF(i));
    assert(EMPTY_STRING.ref==empty_before);
    puts("After: versions 6/11/14, both opcodes, local/struct lvalues, stack balance, GB18030, self-append/COW and return DELETE ownership: PASS");
    return 0;
}
'''
fixture=HERE/'string_plusa_fixture.c';fixture.write_text(prefix+wrappers+tests)
binary=HERE/'string_plusa_fixture'
cmd=['/Library/Developer/CommandLineTools/usr/bin/clang','-std=c11','-O0','-g','-fsanitize=address,undefined',
     '-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk',
     '-I'+str(SOURCE/'include'),'-I'+str(SOURCE/'subprojects/libsys4/include'),str(fixture),
     str(STAGE2.parent/'stage1/wip-asan-build/subprojects/libsys4/libsys4.a'),'-lz','-lm','-framework','CoreFoundation','-o',str(binary)]
p=subprocess.run(cmd,capture_output=True,text=True)
r={'source_case_sha256':hashlib.sha256(production.encode()).hexdigest(),'compile_command':cmd,'compile_exit':p.returncode,'compile_stderr':p.stderr}
print(p.stdout+p.stderr,end='')
if p.returncode==0:
    q=subprocess.run([str(binary)],capture_output=True,text=True)
    r.update(test_exit=q.returncode,stdout=q.stdout,stderr=q.stderr,binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    print(q.stdout+q.stderr,end='')
(HERE/'string-plusa-result.json').write_text(json.dumps(r,indent=2)+'\n')
raise SystemExit(r.get('test_exit',p.returncode))
