/* Standalone integration fixture: production hll_call + real libffi.
 * VM stack/heap services are bounded substitutes; no game bytecode or SDL.
 * Include the actual implementation to initialize its private dispatch table.
 */
#include <stdarg.h>
#include <assert.h>
#include FFI_SOURCE

static union vm_value values[128];
union vm_value *stack = values;
int32_t stack_ptr;
static struct vm_pointer slots[32];
struct vm_pointer *heap = slots;
size_t heap_size = 32;
struct ain *ain;
int hll_current_arg3 = -1;
int hll_self_slot = -1;
int hll_param_slot2;
static int failed, checks, cleanup_count;
static int outer_tag, inner_tag, outer_second, inner_second;

static void check(bool ok, const char *what)
{
	checks++;
	if (!ok) {
		fprintf(stderr, "FAIL: %s\n", what);
		failed++;
	}
}

void sys_warning(const char *fmt, ...)
{
	(void)fmt;
}

bool heap_index_valid(int slot)
{
	return slot > 0 && (size_t)slot < heap_size;
}

int32_t heap_alloc_slot(enum vm_pointer_type type)
{
	static int next = 8;
	assert(next < 32);
	heap[next].type = type;
	return next++;
}

void variable_fini(union vm_value value, enum ain_data_type type, bool call_dtor)
{
	(void)call_dtor;
	check(type == AIN_INT, "cleanup receives the declared integer argument");
	check(hll_current_arg3 == outer_tag, "outer metadata remains active during cleanup");
	check(value.i == 9191, "cleanup still reaches trailing argument after generic element");
	cleanup_count++;
}

static void check_context(int tag, int self, int obj, int second, const char *where)
{
	check(hll_current_arg3 == tag && hll_self_slot == self
		&& hll_func_obj == obj && hll_param_slot2 == second, where);
}

static int leaf(void)
{
	check_context(1, -1, -1, 0, "leaf starts with its own empty callback/array context");
	return 7;
}

static int inner(struct page **self, int callback, int element)
{
	check(*self == heap[2].page && element == 333 && callback == 201,
		"inner receives its marshalled arguments");
	check_context(inner_tag, 2, 41, inner_second, "inner receives its own context");
	hll_call(0, 2, 1);
	check(stack[--stack_ptr].i == 7, "leaf return reaches inner caller");
	check_context(inner_tag, 2, 41, inner_second, "leaf return restores inner context");
	hll_call(0, 3, 0); /* unimplemented branch returns before context setup */
	check(stack[--stack_ptr].i == 0, "unimplemented HLL retains default return");
	check_context(inner_tag, 2, 41, inner_second, "unimplemented call preserves inner context");
	return 17;
}

static int outer(struct page **self, int callback, int element, int trailing)
{
	check(*self == heap[1].page && element == 111 && callback == 101 && trailing == 9191,
		"outer receives its marshalled arguments");
	check_context(outer_tag, 1, 31, outer_second, "outer starts with its own context");
	/* Model repeated predicate invocations, each invoking a different HLL.
	 * Re-enter the real marshaller and C dispatch; do not emulate save/restore.
	 */
	for (int i = 0; i < 2; i++) {
		check(hll_func_obj == 31, "each outer predicate keeps its receiver");
		stack_push(2);
		stack_push(41);
		stack_push(201);
		stack_push(333);
		if (inner_tag == 5)
			stack_push(inner_second);
		hll_call(0, 1, inner_tag);
		check(stack[--stack_ptr].i == 17, "inner return reaches outer caller");
		check_context(outer_tag, 1, 31, outer_second, "inner return restores all outer fields");
	}
	return element + trailing;
}

static int allocate_array(struct page **self)
{
	check(*self == NULL, "uninitialized array is marshalled as NULL");
	return 1;
}

static struct ain_hll_argument args[] = {
	{ .name = "self", .type = { .data = AIN_REF_ARRAY } },
	{ .name = "predicate", .type = { .data = AIN_HLL_FUNC } },
	{ .name = "element", .type = { .data = AIN_HLL_PARAM } },
	{ .name = "trailing", .type = { .data = AIN_INT } },
};

int main(void)
{
	static struct page arrays[2];
	arrays[0].type = arrays[1].type = ARRAY_PAGE;
	heap[1].type = heap[2].type = VM_PAGE;
	heap[1].page = &arrays[0];
	heap[2].page = &arrays[1];
	struct ain_hll_function declarations[5] = {
		{ .name="Outer", .return_type={.data=AIN_INT}, .nr_arguments=4, .arguments=args },
		{ .name="Inner", .return_type={.data=AIN_INT}, .nr_arguments=3, .arguments=args },
		{ .name="Leaf", .return_type={.data=AIN_INT} },
		{ .name="Unimplemented", .return_type={.data=AIN_INT} },
		{ .name="Allocate", .return_type={.data=AIN_INT}, .nr_arguments=1, .arguments=args },
	};
	struct ain_library declaration_library = {.name="Fixture", .nr_functions=5, .functions=declarations};
	struct ain program = {.version=14, .nr_libraries=1, .libraries=&declaration_library};
	ain = &program;
	struct hll_function dispatch[5] = {0};
	struct hll_function *dispatch_libraries[] = {dispatch};
	libraries = dispatch_libraries;
	ffi_type *types[] = {&ffi_type_pointer, &ffi_type_sint32, &ffi_type_sint32, &ffi_type_sint32};
	dispatch[0].fun = outer;
	dispatch[1].fun = inner;
	dispatch[2].fun = leaf;
	dispatch[4].fun = allocate_array;
	for (int i = 0; i < 5; i++) {
		if (i == 3) continue;
		assert(ffi_prep_cif(&dispatch[i].cif, FFI_DEFAULT_ABI,
			declarations[i].nr_arguments, &ffi_type_sint32, types) == FFI_OK);
	}
	for (int test = 0; test < 2; test++) {
		outer_tag = test ? 2 : 3;
		inner_tag = test ? 65538 : 5;
		outer_second = test ? 0 : 222;
		inner_second = test ? 0 : 444;
		stack_ptr = 0;
		stack_push(1);
		stack_push(31);
		stack_push(101);
		stack_push(111);
		if (!test) stack_push(outer_second);
		stack_push(9191);
		hll_call(0, 0, outer_tag);
		check(stack_ptr == 1 && stack[0].i == 9302, "outer call returns one correct value");
		check_context(-1, -1, -1, 0, "top-level return restores initial context");
	}
	check(cleanup_count == 2, "both trailing arguments are finalized once");
	/* Null-array source is a consumed marshalling token, not caller context.
	 * A context fix must not resurrect this already-consumed reference.
	 */
	heap[3].type = VM_PAGE;
	heap[3].page = calloc(1, sizeof(struct page) + sizeof(union vm_value));
	heap[3].page->type = STRUCT_PAGE;
	heap[3].page->nr_vars = 1;
	heap[3].page->values[0].i = -1;
	xref_null_src_page = 3;
	xref_null_src_var = 0;
	stack_ptr = 0;
	stack_push(-1);
	hll_call(0, 4, 2);
	check(heap[3].page->values[0].i >= 8, "null array write-back allocates member slot");
	check(xref_null_src_page == -1 && xref_null_src_var == -1,
		"null-array source remains consumed after return");
	free(heap[3].page);
	printf("%d checks, %d failures\n", checks, failed);
	return failed ? 1 : 0;
}
