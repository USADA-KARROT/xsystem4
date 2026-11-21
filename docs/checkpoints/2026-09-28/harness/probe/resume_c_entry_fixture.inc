/* C-entry image round trip. Include after resume_frames_fixture.inc.
 * A synthetic no-argument function writes the image at RF's marker and
 * returns 4242. The two C entry points both carry a struct_page, but only
 * vm_call(method) puts the object on the VM stack for method_return.
 * This compares the stack and object after the C-entry frame has returned;
 * it does not claim to restore a C caller or resume execution below it.
 */

struct rce_result {
	int sp, csp, values[5];
	int object_ref, object_member;
	int saved_method, saved_object, saved_return;
};

static struct rce_result rce_result_after(int object)
{
	struct rce_result result = { .sp = stack_ptr, .csp = call_stack_ptr };
	for (int i = 0; i < 5; i++)
		result.values[i] = i < stack_ptr ? stack[i].i : -77777;
	result.object_ref = heap_index_valid(object) ? heap[object].ref : -1;
	result.object_member = rf_member(object, 0);
	return result;
}

static int rce_case(bool method)
{
	rf_build(false);
	rf.file1 = "rce.dat";
	rf.mode = RF_SAVE_FIRST;
	rf.saved = rf.recording = rf.exit_at_end = false;
	rf.ntrace = 0;
	memset(&rf.res, 0, sizeof rf.res);
	stack_ptr = 0;
	stack_push((union vm_value){ .i = 1001 });
	stack_push((union vm_value){ .i = 1002 });
	stack_push((union vm_value){ .i = 1003 });
	int object = rf.a;
	int initial_ref = heap[object].ref;
	if (method) {
		vm_call(rf.fG, object);
	} else {
		/* vm_call_nopop gives a non-lambda function its object without
		 * pushing a method object slot. This guards against classifying
		 * every VM_RETURN frame with a struct_page as a method call. */
		extern int hll_func_obj;
		int old_object = hll_func_obj;
		hll_func_obj = object;
		vm_call_nopop(rf.fG, 0);
		hll_func_obj = old_object;
	}
	struct rce_result reference = rce_result_after(object);
	bool reference_ok = rf.saved && rf.res.nflags == 1
		&& (rf.res.flags[0] & 1) == (int)method
		&& reference.csp == 0 && reference.sp == 4
		&& reference.values[0] == 1001 && reference.values[1] == 1002
		&& reference.values[2] == 1003 && reference.values[3] == 4242
		&& reference.object_ref == initial_ref && reference.object_member == 0;
	if (!reference_ok) {
		printf("RCE %s: continuous reference invalid: saved=%d flags=%d csp=%d sp=%d "
		       "values=%d,%d,%d,%d ref=%d/%d member=%d\n", rf_fmt(), rf.saved,
		       rf.res.flags[0], reference.csp, reference.sp, reference.values[0],
		       reference.values[1], reference.values[2], reference.values[3],
		       reference.object_ref, initial_ref, reference.object_member);
		return 1;
	}
	int channel[2];
	if (pipe(channel) != 0)
		return 1;
	fflush(stdout);
	fflush(stderr);
	pid_t pid = fork();
	if (pid == 0) {
		close(channel[0]);
		alarm(60);
		rf.mode = RF_NONE;
		rf.recording = false;
		vm_load_image(RF_KEY, rf.file1);
		int saved_method = call_stack_ptr == 1 ? call_stack[0].is_method : -1;
		int saved_object = call_stack_ptr == 1 ? call_stack[0].struct_page : -1;
		int saved_return = call_stack_ptr == 1 && call_stack[0].return_address == VM_RETURN;
		vm_execute();
		struct rce_result loaded = rce_result_after(object);
		loaded.saved_method = saved_method;
		loaded.saved_object = saved_object;
		loaded.saved_return = saved_return;
		ssize_t written = write(channel[1], &loaded, sizeof loaded);
		close(channel[1]);
		_exit(written == sizeof loaded ? 0 : 2);
	}
	close(channel[1]);
	if (pid < 0) {
		close(channel[0]);
		return 1;
	}
	struct rce_result loaded = { 0 };
	ssize_t n = read(channel[0], &loaded, sizeof loaded);
	close(channel[0]);
	int status = 0;
	waitpid(pid, &status, 0);
	if (n != sizeof loaded || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		printf("RCE %s: loaded run failed: bytes=%zd status=%d\n", rf_fmt(), n, status);
		return 1;
	}
	bool ok = loaded.saved_return && loaded.saved_object == object
		&& loaded.saved_method == (int)method && loaded.csp == reference.csp
		&& loaded.sp == reference.sp && !memcmp(loaded.values, reference.values, sizeof loaded.values)
		&& loaded.object_ref == reference.object_ref && loaded.object_member == reference.object_member;
	if (!ok)
		printf("RCE %s: loaded frame method=%d object=%d return=%d; "
		       "after return csp=%d sp=%d values=%d,%d,%d,%d,%d ref=%d member=%d; "
		       "continuous csp=%d sp=%d values=%d,%d,%d,%d,%d ref=%d member=%d\n",
		       rf_fmt(), loaded.saved_method, loaded.saved_object, loaded.saved_return,
		       loaded.csp, loaded.sp, loaded.values[0], loaded.values[1], loaded.values[2],
		       loaded.values[3], loaded.values[4], loaded.object_ref, loaded.object_member,
		       reference.csp, reference.sp, reference.values[0], reference.values[1],
		       reference.values[2], reference.values[3], reference.values[4],
		       reference.object_ref, reference.object_member);
	return ok ? 0 : 1;
}

static int rce_method_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return rce_case(true); }
static int rce_method_json(void) { config.save_format = SAVE_FORMAT_JSON; return rce_case(true); }
static int rce_nopop_rsm(void) { config.save_format = SAVE_FORMAT_RSM; return rce_case(false); }
static int rce_nopop_json(void) { config.save_format = SAVE_FORMAT_JSON; return rce_case(false); }

static int test_resume_c_entry(void)
{
	sp_setup_dir();
	sp_run("RCE rsm C method entry", rce_method_rsm);
	sp_run("RCE json C method entry", rce_method_json);
	sp_run("RCE rsm C nopop entry", rce_nopop_rsm);
	sp_run("RCE json C nopop entry", rce_nopop_json);
	sp_remove_dir();
	printf("RESUME-C-ENTRY summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}
