/* Synthetic v13 compatibility guards against 372c83e.
 * Include after resume_image_fixture.inc. The harness loads a v14 AIN and
 * initializes its instruction table before these cases. Setting its version
 * to 13 tests version dispatch and image data only: this is NOT a real v13
 * AIN, a bytecode execution test, or evidence of full old-game compatibility.
 * Each case runs in sp_run's isolated child; no game's saves are used.
 */

static int rv13_direct_frame(void)
{
	ain->version = 13;
	call_stack_ptr = 1;
	call_stack[0] = (struct function_call) {
		.fno = ain->main, .return_address = VM_RETURN, .page_slot = 12,
		.struct_page = 13, .base_sp = 14, .env_page = 15,
		.delegate_obj_ref = 16, .delegate_env_ref = 17,
		.is_method = true, .is_delegate_call = true, .dg_return_slots = 2,
		.resumed = true, .dg_resume_pending = true, .resume_dg_page = 18,
	};
	struct function_call before = call_stack[0];
	struct resume_frame_extra extra = { .present = true, .fno = ain->main,
		.env_page = 99, .dg_page = 88 };
	vm_resume_frames_loaded(&extra);
	bool same = !memcmp(&before, &call_stack[0], sizeof before) && call_stack_ptr == 1;
	if (!same) printf("v13 direct frame guard changed an existing frame\n");
	return !same;
}

/* Base load_json_call_stack uses cJSON valueint: 4294967295 saturates
 * to INT_MAX. Inspect the loaded frame, never execute that return address. */
static int rv13_json_case(double address, uint32_t expected)
{
	ain->version = 13;
	config.save_format = SAVE_FORMAT_JSON;
	if (!ri_save("v13-json.dat")) return 1;
	char path[PATH_MAX]; ri_path(path, sizeof path, "v13-json.dat");
	char *text = file_read(path, NULL);
	cJSON *image = text ? cJSON_Parse(text) : NULL;
	free(text);
	cJSON *frames = image ? cJSON_GetObjectItem(image, "call-stack") : NULL;
	cJSON *frame = frames ? cJSON_GetArrayItem(frames, 0) : NULL;
	if (!frame || cJSON_GetArraySize(frames) != 1) { cJSON_Delete(image); return 1; }
	cJSON_ReplaceItemInObject(frame, "return-address", cJSON_CreateNumber(address));
	static const char *keys[] = { "env-page", "delegate-obj-ref", "delegate-env-ref",
		"delegate-page", "delegate-cursor", "delegate-limit" };
	for (int i = 0; i < 6; i++) cJSON_AddNumberToObject(frame, keys[i], 9);
	char *encoded = cJSON_PrintUnformatted(image);
	cJSON_Delete(image);
	FILE *fp = encoded ? fopen(path, "wb") : NULL;
	bool wrote = fp && fputs(encoded, fp) >= 0;
	if (fp && fclose(fp)) wrote = false;
	free(encoded);
	if (!wrote) return 1;
	vm_load_image(RI_KEY, "v13-json.dat");
	struct function_call *f = &call_stack[0];
	bool ok = call_stack_ptr == 1 && f->return_address == expected && !f->base_sp
		&& !f->resumed && !f->is_method && !f->is_delegate_call
		&& !f->env_page && !f->delegate_obj_ref && !f->delegate_env_ref
		&& !f->dg_resume_pending && !f->resume_dg_page;
	if (!ok) printf("v13 JSON: ret=%u expected=%u resumed=%d env=%d\n",
		f->return_address, expected, f->resumed, f->env_page);
	return !ok;
}
static int rv13_json_unsigned(void) { return rv13_json_case(4294967295.0, INT_MAX); }
static int rv13_json_negative(void) { return rv13_json_case(-1.0, UINT32_MAX); }
static int rv13_json_regular(void) { return rv13_json_case(1234.0, 1234); }

static struct page *rv13_comments(const char *const *strings, int n)
{
	union vm_value dim = { .i = n };
	struct page *a = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < n; i++) {
		int s = heap_alloc_slot(VM_STRING);
		heap[s].s = make_string(strings[i], strlen(strings[i]));
		a->values[i].i = s;
	}
	return a;
}

static bool rv13_comments_equal(struct page *a, const char *const *want, int n)
{
	if (!a || a->nr_vars != n) return false;
	for (int i = 0; i < n; i++)
		if (strcmp(heap_get_string(a->values[i].i)->text, want[i])) return false;
	return true;
}

static int rv13_rsm_comments(void)
{
	ain->version = 13;
	config.save_format = SAVE_FORMAT_RSM;
	static const char *initial[] = { "", "ordinary", "xsystem4-frames 1 1|0 0 0 0 0 0 0",
		"xsystem4-frames 1 broken", "xsystem4-frames 1 1|0 0 0 0 0 0 0",
		"xsystem4-c-methods 1 1" };
	static const char *replacement[] = { "replacement", "xsystem4-frames 1 new" };
	/* Comment-only file: reading must return every string, in order. */
	struct page *a = rv13_comments(initial, 6);
	if (!vm_write_image_comments(RI_KEY, "comments.dat", a)) return 1;
	int success = 0;
	struct page *got = vm_load_image_comments(RI_KEY, "comments.dat", &success);
	if (!success || !rv13_comments_equal(got, initial, 6)) {
		printf("v13 comment-only read omitted or reordered a user comment\n"); return 1;
	}
	/* Full image: writing comments must replace ALL existing comments. */
	if (!ri_save("full.dat") || !vm_write_image_comments(RI_KEY, "full.dat", a)) return 1;
	got = vm_load_image_comments(RI_KEY, "full.dat", &success);
	if (!success || !rv13_comments_equal(got, initial, 6)) return 1;
	vm_load_image(RI_KEY, "full.dat");
	struct function_call *f = &call_stack[0];
	if (call_stack_ptr != 1 || f->base_sp || f->resumed || f->is_method
	    || f->is_delegate_call || f->env_page || f->delegate_obj_ref
	    || f->delegate_env_ref || f->dg_resume_pending || f->resume_dg_page) {
		printf("v13 RSM full image incorrectly reconstructed frame metadata\n");
		return 1;
	}
	a = rv13_comments(replacement, 2);
	if (!vm_write_image_comments(RI_KEY, "full.dat", a)) return 1;
	got = vm_load_image_comments(RI_KEY, "full.dat", &success);
	if (!success || !rv13_comments_equal(got, replacement, 2)) return 1;
	char path[PATH_MAX]; ri_path(path, sizeof path, "full.dat");
	enum savefile_error error;
	struct rsave *rs = rsave_read(path, RSAVE_READ_ALL, &error);
	bool ok = error == SAVEFILE_SUCCESS && rs->nr_comments == 2
		&& !strcmp(rs->comments[0], replacement[0]) && !strcmp(rs->comments[1], replacement[1]);
	if (error == SAVEFILE_SUCCESS) rsave_free(rs);
	if (!ok) printf("v13 full image retained or lost a comment\n");
	return !ok;
}

static int rv13_trace(void)
{
	ain->version = 13;
	setenv("XSYS4_RESUME_TRACE", "1", 1);
	setenv("XSYS4_PARTS_CTRL_TRACE", "1", 1);
	char path[PATH_MAX]; ri_path(path, sizeof path, "trace.txt");
	fflush(stdout); fflush(stderr);
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) return 1;
	int out = dup(1), err = dup(2);
	if (out < 0 || err < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0) return 1;
	close(fd);
	bool vm_on = resume_trace_on(), parts_on = parts_ctrl_trace_on();
	vm_resume_trace("synthetic", "trace.dat");
	parts_ctrl_trace("synthetic", 0);
	fflush(stdout); fflush(stderr);
	dup2(out, 1); dup2(err, 2); close(out); close(err);
	FILE *fp = fopen(path, "rb");
	int ch = fp ? fgetc(fp) : 0;
	if (fp) fclose(fp);
	bool ok = !vm_on && !parts_on && ch == EOF;
	if (!ok) printf("v13 trace emitted data or was enabled: vm=%d parts=%d first=%d\n", vm_on, parts_on, ch);
	return !ok;
}

static int test_resume_v13_guard(void)
{
	puts("SYNTHETIC guard only: v14 AIN version field set to 13; no v13 bytecode executed");
	sp_setup_dir();
	sp_run("v13 frame reconstruction is a no-op", rv13_direct_frame);
	sp_run("v13 JSON unsigned return sentinel", rv13_json_unsigned);
	sp_run("v13 JSON negative return sentinel", rv13_json_negative);
	sp_run("v13 JSON ordinary return address", rv13_json_regular);
	sp_run("v13 RSM user comments retained/replaced", rv13_rsm_comments);
	sp_run("v13 trace disabled despite environment", rv13_trace);
	sp_remove_dir();
	printf("RESUME-V13-GUARD summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}
