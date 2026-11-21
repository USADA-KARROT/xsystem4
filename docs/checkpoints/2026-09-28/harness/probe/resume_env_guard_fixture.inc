/* resume-env-guard: check saved closure environment slots after a load.
 * Test vm_resume_frames_loaded directly with one bottom frame, so no
 * delegate or return opcode inference can affect the environment result.
 * The invalid values are constructed, not taken from a game image.
 */

static int resume_env_guard_case(void)
{
	int failed = 0;
	int local = heap_alloc_page(alloc_page(LOCAL_PAGE, 0, 0));
	int structure = heap_alloc_page(alloc_page(STRUCT_PAGE, -1, 0));
	int array = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0));
	int string = heap_alloc_string(make_string("test", 4));
	int empty_page = heap_alloc_page(NULL);
	int dead = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0));
	heap_unref(dead);
	struct { const char *name; int env; int expected; } cases[] = {
		{ "local", local, local },
		{ "struct", structure, structure },
		{ "no environment", 0, 0 },
		{ "negative sentinel", -1, 0 },
		{ "out of range", INT32_MAX, 0 },
		{ "released slot", dead, 0 },
		{ "array page", array, 0 },
		{ "string slot", string, 0 },
		{ "null page", empty_page, 0 },
	};
	struct resume_frame_extra extra = { .present = true, .fno = 0 };
	call_stack_ptr = 1;
	call_stack[0] = (struct function_call){ .fno = 0, .return_address = VM_RETURN };
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		extra.env_page = cases[i].env;
		vm_resume_frames_loaded(&extra);
		if (call_stack[0].env_page != cases[i].expected) {
			printf("  FAIL %s: environment %d, expected %d\n", cases[i].name,
				call_stack[0].env_page, cases[i].expected);
			failed = 1;
		}
	}
	int saved_version = ain->version;
	ain->version = 13;
	extra.env_page = local;
	vm_resume_frames_loaded(&extra);
	if (call_stack[0].env_page != 0) {
		printf("  FAIL v13: environment %d, expected 0\n", call_stack[0].env_page);
		failed = 1;
	}
	ain->version = saved_version;
	call_stack_ptr = 0;
	heap_unref(local);
	heap_unref(structure);
	heap_unref(array);
	heap_unref(string);
	heap_unref(empty_page);
	return failed;
}

static int test_resume_env_guard(void)
{
	sp_setup_dir();
	sp_run("resume environment validation", resume_env_guard_case);
	sp_remove_dir();
	printf("RESUME-ENV-GUARD summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}
