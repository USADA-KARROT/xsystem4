/* Execute production queue functions in the order used by the CN AIN.
 * No SDL/VM execution. String adapters are invoked through actual libffi.
 */
#include <stdio.h>
#include <string.h>
#include <ffi.h>
#include MESSAGE_SOURCE

struct ain *ain;
struct static_library lib_PartsEngine = {.name = "PartsEngine"};
static int failures, checks;
static void *string_binding;

void static_library_replace(struct static_library *lib, const char *name, void *fun)
{
	(void)lib;
	if (!strcmp(name, "GetMessageVariableString")) string_binding = fun;
}

static void check(bool ok, const char *label)
{
	checks++;
	if (!ok) { fprintf(stderr, "FAIL: %s\n", label); failures++; }
}

static void push(int part, int x, int y, int key)
{
	int vars[] = {x, y, key};
	parts_enqueue_message_vars(4, part, part + 100, part + 200, 3, vars);
}

static void head(int part, int x, int y, int key)
{
	check(PE_v14_GetMessageType() == 4 && PE_v14_GetMessagePartsNumber() == part,
		"head metadata belongs to the expected message");
	check(PE_v14_GetMessageDelegateIndex() == part + 100
		&& PE_v14_GetMessageUniqueID() == part + 200, "event identity matches head");
	check(PE_v14_GetMessageVariableCount() == 3
		&& PE_v14_GetMessageVariableInt(0) == x
		&& PE_v14_GetMessageVariableInt(1) == y
		&& PE_v14_GetMessageVariableInt(2) == key, "variables belong to the same head");
}

static void empty(void)
{
	check(PE_v14_GetMessageType() == -1 && PE_v14_GetMessagePartsNumber() == 0
		&& PE_v14_GetMessageVariableCount() == 0
		&& PE_v14_GetMessageVariableInt(0) == 0, "empty queue has no stale payload");
}

static void reset_fixture(void)
{
	/* Release is tested separately; force an empty start even before the fix. */
	for (int i = 0; i < 64; i++) PE_v14_PopMessage();
	PE_v14_ReleaseMessage();
}

static void strings(void)
{
	struct ain_hll_argument args[] = {
		{.name="Index", .type={.data=AIN_INT}},
		{.name="Out", .type={.data=AIN_REF_STRING}},
	};
	struct ain_hll_function decl = {.name="GetMessageVariableString",
		.return_type={.data=AIN_STRING}, .nr_arguments=1, .arguments=args};
	struct ain_library lib = {.name="PartsEngine", .nr_functions=1, .functions=&decl};
	struct ain program = {.version=14, .nr_libraries=1, .libraries=&lib};
	ain = &program;
	int index = 0;
	unsigned baseline_ref = EMPTY_STRING.ref;
	ffi_cif cif;
	ffi_type *new_types[] = {&ffi_type_sint32};
	void *new_args[] = {&index};
	pe_v14_message_replace();
	check(string_binding && string_binding != PE_v14_GetMessageVariableString,
		"string(int) declaration selects a return-string adapter");
	/* Do not invoke the known wrong baseline ABI just to crash the fixture. */
	if (string_binding && string_binding != PE_v14_GetMessageVariableString) {
		struct string *result = NULL;
		check(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 1, &ffi_type_pointer, new_types) == FFI_OK,
			"new getter libffi signature prepares");
		ffi_call(&cif, string_binding, &result, new_args);
		check(result && result->size == 0 && result->ref == baseline_ref + 1,
			"new getter returns a valid owned empty-string reference");
		free_string(result);
	}
	decl.return_type.data = AIN_VOID;
	decl.nr_arguments = 2;
	pe_v14_message_replace();
	check(string_binding == PE_v14_GetMessageVariableString, "legacy out-string ABI is retained");
	struct string *out = cstr_to_string("old");
	struct string **out_ptr = &out;
	ffi_type *old_types[] = {&ffi_type_sint32, &ffi_type_pointer};
	void *old_args[] = {&index, &out_ptr};
	check(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_void, old_types) == FFI_OK,
		"legacy getter libffi signature prepares");
	ffi_call(&cif, string_binding, NULL, old_args);
	check(out && out->size == 0, "legacy getter replaces previous output with empty reference");
	free_string(out);
	check(EMPTY_STRING.ref == baseline_ref, "both string getter references are balanced");
	/* Unknown declarations must not be forced onto a guessed signature. */
	decl.return_type.data = AIN_INT;
	string_binding = NULL;
	pe_v14_message_replace();
	check(!string_binding, "unknown getter ABI leaves the existing binding untouched");
	decl.return_type.data = AIN_STRING;
	decl.nr_arguments = 1;
	args[0].type.data = AIN_FLOAT;
	string_binding = NULL;
	pe_v14_message_replace();
	check(!string_binding, "wrong index type is not adapted");
}

int main(void)
{
	reset_fixture(); empty();
	push(10,85,82,1); push(20,169,130,2);
	head(10,85,82,1);
	int copied_x = PE_v14_GetMessageVariableInt(0);
	PE_v14_PopMessage(); /* CallEvent3 copies variables before popping. */
	head(20,169,130,2); /* A nested dispatch now reads the next head. */
	check(copied_x == 85, "outer callback argument survives nested message handling");
	PE_v14_PopMessage(); empty();
	push(30,9,8,4); head(30,9,8,4); /* Popped data must not poison a later enqueue. */
	PE_v14_PopMessage(); empty();
	reset_fixture();
	push(1,1,2,1); push(2,3,4,2); push(3,5,6,4); push(4,7,8,1);
	PE_v14_SeekMessage(2);
	check(PE_v14_GetMessagePartsNumber() == 2, "Seek finds target");
	head(2,3,4,2);
	PE_v14_SeekMessage(2);
	check(PE_v14_GetMessagePartsNumber() == 2, "repeated Seek leaves target unconsumed");
	head(2,3,4,2);
	PE_v14_PopMessage(); head(3,5,6,4); /* Exactly one removal, no double pop. */
	PE_v14_ReleaseMessage(); empty(); /* Clear both remaining messages. */
	PE_v14_ReleaseMessage(); PE_v14_PopMessage(); empty();
	push(1,1,2,1);
	PE_v14_SeekMessage(99);
	check(PE_v14_GetMessageType() == -1, "missing Seek leaves no target"); empty();
	reset_fixture();
	for (int i=0;i<63;i++) push(i,i*2,i*3,1);
	for (int i=0;i<40;i++) { head(i,i*2,i*3,1); PE_v14_PopMessage(); }
	for (int i=63;i<103;i++) push(i,i*2,i*3,1);
	for (int i=40;i<103;i++) { head(i,i*2,i*3,1); PE_v14_PopMessage(); }
	empty();
	strings();
	printf("%d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
