#include <assert.h>
#include <stdint.h>
static void probe_entry(int fno) { (void)fno; }
static void probe_return(int fno, int page) { (void)fno; (void)page; }
static void probe_step(unsigned op) { (void)op; }
#include "instrumented-vm.inc"
#include FFI_SOURCE

static int find_exact(const char *name, int ret, int nargs, int argtype)
{
    int found = -1;
    for (int i = 0; i < ain->nr_functions; i++) {
        if (strcmp(ain->functions[i].name, name)) continue;
        const struct ain_function *candidate = &ain->functions[i];
        fprintf(stderr,"CANDIDATE name=%s fno=%d ret=%d args=%d arg0=%d address=%x\n",name,i,candidate->return_type.data,candidate->nr_args,candidate->nr_args?candidate->vars[0].type.data:-1,candidate->address);
        if((ret>=0 && candidate->return_type.data!=ret) || candidate->nr_args!=nargs || (nargs && candidate->vars[0].type.data!=argtype))continue;
        if(found == -1) found = i; /* first exact-signature match; all candidates printed */
    }
    assert(found >= 0);
    const struct ain_function *f = &ain->functions[found];
    assert((ret < 0 || f->return_type.data == ret) && f->nr_args == nargs);
    if (nargs) assert(f->vars[0].type.data == argtype);
    assert(func_flags[found] & FUNC_FLAG_CASTIMER_MGR);
    printf("SIGNATURE name=%s fno=%d return=%d args=%d arg0=%d flags=%u\n",
           name, found, f->return_type.data, f->nr_args,
           nargs ? f->vars[0].type.data : -1, func_flags[found]);
    return found;
}

/* Supply the real function's typed local page, as consumed by the helper.
 * This deliberately calls the native helper directly, not method_call. */
static union vm_value native_call(int fno, union vm_value arg)
{
    const struct ain_function *f = &ain->functions[fno];
    struct page *local = alloc_page(LOCAL_PAGE, fno, f->nr_vars);
    for (int i = 0; i < f->nr_vars; i++)
        local->values[i] = variable_initval(f->vars[i].type.data);
    if (f->nr_args) local->values[0] = arg;
    const int local_slot = heap_alloc_page(local);
    assert(call_stack_ptr == 0);
    call_stack[0] = (struct function_call){.fno=fno, .page_slot=local_slot, .struct_page=-1};
    call_stack_ptr = 1;
    union vm_value ret = {.i=0x55555555};
    assert(native_cas_timer_intercept(fno, -1, &ret));
    call_stack_ptr = 0;
    heap_unref(local_slot);
    return ret;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    setvbuf(stdout, NULL, _IOLBF, 0);
    int error = 0;
    ain = ain_open(argv[1], &error);
    assert(ain && ain->version == 14);
    initialize_instructions(ain->version);
    heap_init();
    init_func_flags(); /* Production flags kept intact. */
    instr_ptr = VM_RETURN;
    const int get_rate = find_exact("CASTimerManager@Rate::get", AIN_FLOAT, 0, -1);
    const int set_rate = find_exact("CASTimerManager@Rate::set", -1, 1, AIN_FLOAT);
    const int create = find_exact("CASTimerManager@CreateHandle", AIN_INT, 0, -1);
    const int release = find_exact("CASTimerManager@ReleaseHandle", AIN_VOID, 1, AIN_INT);
    const int check = find_exact("CASTimerManager@CheckHandle", AIN_BOOL, 1, AIN_INT);

    union vm_value value = native_call(get_rate, (union vm_value){.i=0});
    const int startup_rate_mismatch = value.f != 1.0f;
    printf("RATE startup bits=0x%08x float=%.9g expected_float=1 mismatch=%d\n",
           (unsigned)value.i, value.f, startup_rate_mismatch);
    native_call(set_rate, (union vm_value){.f=2.5f});
    value = native_call(get_rate, (union vm_value){.i=0});
    printf("RATE after_float_set bits=0x%08x float=%.9g expected_float=2.5 match=%d\n",
           (unsigned)value.i, value.f, value.f == 2.5f);

    int handle = native_call(create, (union vm_value){.i=0}).i;
    assert(handle > 0 && handle < CAS_TIMER_MAX);
    int active_before = cas_timers[handle].active;
    int check_before = native_call(check, (union vm_value){.i=handle}).i;
    native_call(release, (union vm_value){.i=handle});
    int active_after = cas_timers[handle].active;
    int check_after = native_call(check, (union vm_value){.i=handle}).i;
    int next = native_call(create, (union vm_value){.i=0}).i;
    printf("HANDLE created=%d active_before=%d check_before=%d expected_valid_check=1 active_after_release=%d check_after_release=%d next_created=%d\n",
           handle, active_before, check_before, active_after, check_after, next);
    printf("AUDIT startup_float_mismatch=%d valid_check_false=%d released_handle_still_active=%d\n",
           startup_rate_mismatch, active_before && !check_before, active_after);
    puts("SCOPE direct production native helper + real AIN signatures; no GUI or bytecode/method_call integration; exit0 means diagnostic completed, not correctness passed");
    return 0;
}
