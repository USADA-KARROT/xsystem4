from pathlib import Path
import hashlib, json, os, subprocess

HERE = Path(__file__).resolve().parent
STAGE2 = HERE.parent
SOURCE = STAGE2 / 'source'

def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

array = (SOURCE / 'src/hll/Array.c').read_text()
vm = (SOURCE / 'src/vm.c').read_text()
page = (SOURCE / 'src/page.c').read_text()
array_parts = '\n'.join(function(array, signature) for signature in [
    'static inline bool array_elem_is_ref(',
    'static inline bool array_elem_is_2slot(',
    'static int array_erase_stride(',
    'static bool array_erase_value_equal(',
    'static bool Array_IsExistValue(',
    'static bool Array_IsExist(',
    'void *array_isexist_function(',
])
old_array = subprocess.run(['git', 'show', 'HEAD:src/hll/Array.c'], cwd=SOURCE,
                           capture_output=True, text=True, check=True).stdout
old_isexist = function(old_array, 'static bool Array_IsExist(').replace(
    'Array_IsExist(', 'Array_IsExist_before(', 1).replace('vm_call_nopop(', 'record_bad_callback(')
vm_call = function(vm, 'void vm_call_nopop(')
vm_call_before = vm_call.replace('void vm_call_nopop(', 'static void vm_call_nopop_before(', 1)
for case in ['AIN_IFACE', 'AIN_IFACE_WRAP']:
    vm_call_before = vm_call_before.replace('\n\t\tcase ' + case + ':', '')
fini = function(page, 'void variable_fini(')
prefix = r'''
#define VM_PRIVATE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ffi.h>
#include "system4/ain.h"
#include "system4/string.h"
#include "system4/instructions.h"
#include "system4/little_endian.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
struct ain *ain;
static union vm_value fixture_stack[64];
union vm_value *stack = fixture_stack;
int32_t stack_ptr, call_stack_ptr;
struct function_call call_stack[4096];
size_t instr_ptr;
static unsigned char *func_flags;
#define FUNC_FLAG_LAMBDA 1
#define VM_RETURN -1
static struct vm_pointer fixture_heap[128];
struct vm_pointer *heap = fixture_heap;
size_t heap_size = 128;
int hll_current_arg3, hll_param_slot2, hll_func_obj = -1;
static int retain[128], release[128], freed[128];
static int callbacks, wrong_callbacks, wrong_fno, wrong_nargs;
static int match_value, expected_metadata, expected_args;
static bool verify_ref_during_callback;
void variable_fini(union vm_value v, enum ain_data_type type, bool dtor);
_Noreturn void _vm_error(const char *fmt, ...) { abort(); }
union vm_value stack_pop(void) { assert(stack_ptr > 0); return stack[--stack_ptr]; }
bool heap_index_valid(int n) { return n > 0 && n < 128 && heap[n].ref > 0; }
void heap_ref(int n) { assert(heap_index_valid(n)); heap[n].ref++; retain[n]++; }
void heap_unref(int n) {
    assert(heap_index_valid(n)); release[n]++;
    if (--heap[n].ref) return;
    freed[n]++;
    if (heap[n].type == VM_PAGE) {
        struct page *p = heap[n].page;
        if (p && p->type == LOCAL_PAGE)
            for (int i = p->nr_vars - 1; i >= 0; i--)
                variable_fini(p->values[i], ain->functions[p->index].vars[i].type.data, true);
        free(p); heap[n].page = NULL;
    } else { free_string(heap[n].s); heap[n].s = NULL; }
}
void exit_unref(int n) { heap_unref(n); }
static struct page *numbers(const int *v, int count, int type) {
    struct page *p = calloc(1, sizeof(*p) + count * sizeof(union vm_value)); assert(p);
    p->type = ARRAY_PAGE; p->a_type = type; p->nr_vars = count; p->array.rank = 1;
    for (int i = 0; i < count; i++) p->values[i].i = v[i]; return p;
}
static void object(int slot) {
    assert(!heap[slot].ref);
    struct page *p = calloc(1, sizeof(*p)); assert(p); p->type = STRUCT_PAGE;
    heap[slot] = (struct vm_pointer){ .ref = 1, .type = VM_PAGE, .page = p };
}
static int _function_call(int fno, int ret) {
    int slot = 120; assert(!heap[slot].ref); int n = ain->functions[fno].nr_vars;
    struct page *p = calloc(1, sizeof(*p) + n * sizeof(union vm_value)); assert(p);
    p->type = LOCAL_PAGE; p->index = fno; p->nr_vars = n;
    heap[slot] = (struct vm_pointer){ .ref = 1, .type = VM_PAGE, .page = p };
    call_stack[call_stack_ptr++] = (struct function_call){ .fno = fno, .page_slot = slot, .return_address = ret };
    return slot;
}
static void set_struct_page(int n) { call_stack[call_stack_ptr - 1].struct_page = n; }
static void vm_execute(void) {
    struct function_call *frame = &call_stack[call_stack_ptr - 1];
    struct page *p = heap[frame->page_slot].page;
    assert(ain->functions[frame->fno].nr_args == expected_args);
    int first = p->values[0].i;
    if (expected_args == 2) assert(p->values[1].i == expected_metadata);
    if (verify_ref_during_callback) assert(heap[first].ref == 2);
    callbacks++; bool result = first == match_value;
    int local = frame->page_slot; call_stack_ptr--; heap_unref(local);
    stack_push(result);
}
static void record_bad_callback(int fno, int nargs) {
    wrong_callbacks++; wrong_fno = fno; wrong_nargs = nargs; stack_push(0);
}
'''
tests = r'''
static bool call2(void *fn, struct page **p, int value) {
    struct page **pp = p; ffi_type *types[] = { &ffi_type_pointer, &ffi_type_sint32 };
    void *args[] = { &pp, &value }; ffi_cif cif; ffi_arg ret = 0;
    assert(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_sint32, types) == FFI_OK);
    ffi_call(&cif, FFI_FN(fn), &ret, args); return *(unsigned char *)&ret != 0;
}
int main(int argc, char **argv) {
    assert(argc == 2); int error = 0; ain = ain_open(argv[1], &error); assert(ain);
    initialize_instructions(ain->version); func_flags = calloc(ain->nr_functions, 1); assert(func_flags);
    int libno = ain_get_library(ain, "Array"); assert(libno >= 0);
    void *values = NULL, *predicate = NULL; int value_id = -1, declarations = 0;
    for (int i = 0; i < ain->libraries[libno].nr_functions; i++) {
        struct ain_hll_function *f = &ain->libraries[libno].functions[i];
        if (strcmp(f->name, "IsExist")) continue;
        void *selected = array_isexist_function(f); assert(selected);
        printf("Actual AIN Array:%d IsExist ret=%d argc=%d arg0=%d arg1=%d\n", i,
               f->return_type.data, f->nr_arguments, f->arguments[0].type.data, f->arguments[1].type.data);
        if (f->arguments[1].type.data == AIN_HLL_PARAM) { values = selected; value_id = i; }
        else predicate = selected;
        declarations++;
    }
    assert(declarations == 2 && values == (void *)Array_IsExistValue && predicate == (void *)Array_IsExist);
    bool actual_call = false;
    unsigned start = ain->functions[31722].address;
    for (unsigned p = start; p < start + 4096; p += instruction_width(LittleEndian_getW(ain->code, p))) {
        if (LittleEndian_getW(ain->code, p) == CALLHLL && LittleEndian_getDW(ain->code, p+2) == libno
            && LittleEndian_getDW(ain->code, p+6) == value_id) {
            assert(LittleEndian_getDW(ain->code, p+10) == 1); actual_call = true;
            printf("Actual SetShortcut f31722 at 0x%x uses value overload with integer/enum arg3=1\n", p); break;
        }
    }
    assert(actual_call && ain->functions[4].nr_args == 3);
    const int nums[] = { 1, 3, 4, 17 }; struct page *p = numbers(nums, 4, AIN_ARRAY_INT); hll_current_arg3 = 1;
    assert(!Array_IsExist_before(&p, 4)); assert(wrong_callbacks == 4 && wrong_fno == 4 && wrong_nargs == 3);
    assert(stack_ptr == 0); puts("Before production Array.IsExist: enum4 invokes unrelated 3-argument f4 for each value: reproduced");
    int cases[] = { -1, 0, 1, 3, 4, 5, 17, 37741, 40000 };
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        int n = cases[i]; assert(call2(values, &p, n) == (n == 1 || n == 3 || n == 4 || n == 17));
    }
    assert(callbacks == 0 && wrong_callbacks == 4 && stack_ptr == 0);
    for (int i = 0; i < 4; i++) assert(p->values[i].i == nums[i]); free(p); p = NULL;
    assert(!call2(values, &p, 4)); assert(!call2(predicate, &p, 4));
    puts("Value overload: all integer/enum searches, absence/empty, no callbacks or array mutation: PASS");

    hll_current_arg3 = 2;
    heap[20] = (struct vm_pointer){ .ref=1, .type=VM_STRING, .s=make_string("alpha", 5) };
    heap[21] = (struct vm_pointer){ .ref=1, .type=VM_STRING, .s=make_string("beta", 4) };
    heap[22] = (struct vm_pointer){ .ref=1, .type=VM_STRING, .s=make_string("alpha", 5) };
    const int strings[] = { 20, 21 }; p = numbers(strings, 2, AIN_ARRAY_STRING);
    assert(call2(values, &p, 22)); assert(!call2(values, &p, 23));
    assert(heap[20].ref == 1 && heap[21].ref == 1 && heap[22].ref == 1); free(p);
    heap_unref(20); heap_unref(21); heap_unref(22);
    puts("Value overload: distinct string heap slots compare text, search owns no temporary references: PASS");

    object(30); object(32); object(50); hll_current_arg3 = 65539;
    const int pairs[] = { 30, 50, 32, 50 }; p = numbers(pairs, 4, AIN_ARRAY); p->array.struct_type = 2;
    hll_param_slot2 = 50; assert(call2(values, &p, 30)); hll_param_slot2 = 51; assert(!call2(values, &p, 30));
    hll_param_slot2 = 30; assert(!call2(values, &p, 50));
    assert(!retain[30] && !retain[32] && !retain[50]);
    assert(ain->functions[36095].nr_args == 2 && ain->functions[36095].vars[0].type.data == AIN_IFACE
           && ain->functions[36095].vars[1].type.data == AIN_VOID);
    expected_args = 2; expected_metadata = 50; match_value = 32;
    stack_push(30); stack_push(50); vm_call_nopop_before(36095, 2); stack_pop(); stack_ptr = 0;
    assert(!heap[30].ref && freed[30] == 1 && heap[50].ref == 1 && !release[50]);
    puts("Before production vm_call_nopop: actual f36095 IFACE argument loses caller reference on return; metadata untouched: reproduced");
    object(30); verify_ref_during_callback = true; int before_callbacks = callbacks;
    stack_push(123456);
    for (int round = 0; round < 3; round++) {
        assert(call2(predicate, &p, 36095)); assert(stack_ptr == 1 && stack[0].i == 123456);
        assert(heap[30].ref == 1 && heap[32].ref == 1 && heap[50].ref == 1);
    }
    assert(callbacks - before_callbacks == 6 && retain[30] == 3 && retain[32] == 3
           && release[30] == 4 && release[32] == 3 && !retain[50] && !release[50]);
    match_value = 99; assert(!call2(predicate, &p, 36095)); assert(stack_ptr == 1); stack_pop();
    free(p); heap_unref(30); heap_unref(32); heap_unref(50);
    assert(freed[30] == 2 && freed[32] == 1 && freed[50] == 1);
    puts("Predicate actual IFACE f36095: logical pairs, correct metadata, early true/false, callback retain/release balances and final owner release once: PASS");

    assert(ain->functions[22399].nr_args == 1 && ain->functions[22399].vars[0].type.data == AIN_REF_STRUCT);
    object(40); object(42); const int refs[] = { 40, 42 }; p = numbers(refs, 2, AIN_ARRAY_STRUCT);
    expected_args = 1; match_value = 42; hll_current_arg3 = 65538;
    assert(call2(predicate, &p, 22399)); assert(heap[40].ref == 1 && heap[42].ref == 1 && stack_ptr == 0);
    free(p); heap_unref(40); heap_unref(42);
    puts("Actual one-slot REF_STRUCT predicate f22399: both callbacks preserve references and stack: PASS");

    struct ain_type saved_type = ain->functions[36095].vars[0].type;
    {
        object(60); object(61); ain->functions[36095].vars[0].type.data = AIN_IFACE_WRAP;
        expected_args = 2; expected_metadata = 61; match_value = 60;
        stack_push(60); stack_push(61); vm_call_nopop(36095, 2); assert(stack_pop().i == 1); stack_ptr = 0;
        assert(heap[60].ref == 1 && heap[61].ref == 1 && !retain[61] && !release[61]);
        heap_unref(60); heap_unref(61);
        retain[61] = release[61] = 0;
    }
    ain->functions[36095].vars[0].type = saved_type;
    puts("Heap-backed IFACE_WRAP argument: first slot retained symmetrically with variable_fini, metadata untouched: PASS");
    puts("OPTION ownership remains unresolved and unchanged; primitive and heap-backed OPTION are outside this fix");

    struct ain_hll_argument args[2] = {0}; args[0].type.data = AIN_REF_ARRAY_INT; args[1].type.data = AIN_HLL_FUNC_71;
    struct ain_hll_function legacy = { .nr_arguments = 2, .arguments = args }; legacy.return_type.data = AIN_BOOL;
    assert(array_isexist_function(&legacy) == predicate); args[1].type.data = AIN_HLL_PARAM; assert(array_isexist_function(&legacy) == values);
    args[1].type.data = AIN_INT; assert(!array_isexist_function(&legacy)); legacy.nr_arguments = 1; assert(!array_isexist_function(&legacy));
    assert(!array_isexist_function(NULL));
    puts("Typed legacy ref-array and HLL_FUNC_71 declarations dispatch correctly; unknown signatures are unlinked: PASS");
    free(func_flags); ain_free(ain); puts("Array.IsExist production fixture: ALL PASS"); return 0;
}
'''
fixture = HERE / 'array_isexist_fixture.c'
fixture.write_text(prefix + '\n' + fini + '\n' + vm_call + '\n' + vm_call_before + '\n'
                   + array_parts + '\n' + old_isexist + '\n' + tests)
binary = HERE / 'array_isexist_fixture'
cmd = ['clang', '-std=c11', '-O0', '-g', '-fsanitize=address,undefined',
       '-I' + str(SOURCE / 'include'), '-I' + str(SOURCE / 'subprojects/libsys4/include'),
       '-I/opt/homebrew/opt/libffi/include', str(fixture),
       str(STAGE2.parent / 'stage1/wip-asan-build/subprojects/libsys4/libsys4.a'),
       '-L/opt/homebrew/opt/libffi/lib', '-lffi', '-lz', '-lm', '-framework', 'CoreFoundation',
       '-o', str(binary)]
compiled = subprocess.run(cmd, capture_output=True, text=True)
result = {'compile_command': cmd, 'compile_exit': compiled.returncode, 'compile_stderr': compiled.stderr,
          'array_source_sha256': hashlib.sha256(array.encode()).hexdigest(),
          'vm_call_nopop_sha256': hashlib.sha256(vm_call.encode()).hexdigest(),
          'fixture_sha256': hashlib.sha256(fixture.read_bytes()).hexdigest()}
print(compiled.stderr)
if not compiled.returncode:
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1:abort_on_error=1',
               UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
    ran = subprocess.run([str(binary), str(STAGE2 / 'game/dohnadohna.ain')], env=env,
                         capture_output=True, text=True, errors='replace')
    result.update(test_exit=ran.returncode, stdout=ran.stdout, stderr=ran.stderr)
    print(ran.stdout); print(ran.stderr)
(HERE / 'array-isexist-result.json').write_text(json.dumps(result, indent=2) + '\n')
raise SystemExit(result.get('test_exit', compiled.returncode))
