/* Synthetic resume marker guards. Include after resume_frames_fixture.inc
 * and resume_c_entry_fixture.inc; both provide the reference program and
 * isolated image helpers. These cases inspect reconstructed frames only;
 * they do not claim that corrupted images can be executed safely or that a
 * C caller can be reconstructed. */

/* Rewrite one seven-field frame comment in a synthetic RSM image. The test
 * preserves the other records and replaces only the requested fields. */
static bool rm_rewrite_rsm(const char *file, int target, bool change_fno,
		int new_fno, bool change_page, int new_page)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", sp_dir, file);
	enum savefile_error error;
	struct rsave *save = rsave_read(path, RSAVE_READ_ALL, &error);
	if (error != SAVEFILE_SUCCESS)
		return false;
	bool ok = false;
	if (save->nr_comments != 1 || !save->comments[0])
		goto done;
	const char *s = save->comments[0];
	const char *prefix = "xsystem4-frames 1 ";
	if (strncmp(s, prefix, strlen(prefix)))
		goto done;
	s += strlen(prefix);
	char *end;
	long n = strtol(s, &end, 10);
	if (end == s || n <= 0 || n > 4 || target < 0 || target >= n)
		goto done;
	s = end;
	int v[4][7];
	for (int i = 0; i < n; i++) {
		if (*s++ != '|')
			goto done;
		for (int k = 0; k < 7; k++) {
			long x = strtol(s, &end, 10);
			if (end == s || x < INT32_MIN || x > INT32_MAX)
				goto done;
			v[i][k] = (int)x;
			s = end;
		}
	}
	if (*s)
		goto done;
	if (change_fno) v[target][0] = new_fno;
	if (change_page) v[target][4] = new_page;
	char comment[512];
	int used = snprintf(comment, sizeof comment, "%s%ld", prefix, n);
	if (used < 0 || (size_t)used >= sizeof comment)
		goto done;
	for (int i = 0; i < n; i++) {
		int m = snprintf(comment + used, sizeof comment - used,
				"|%d %d %d %d %d %d %d", v[i][0], v[i][1],
				v[i][2], v[i][3], v[i][4], v[i][5], v[i][6]);
		if (m < 0 || (size_t)m >= sizeof comment - used)
			goto done;
		used += m;
	}
	free(save->comments[0]);
	save->comments[0] = xstrdup(comment);
	FILE *fp = fopen(path, "wb");
	if (fp) {
		ok = rsave_write(save, fp, true, 1) == SAVEFILE_SUCCESS;
		if (fclose(fp)) ok = false;
	}
done:
	rsave_free(save);
	return ok;
}

/* The writer uses a negative marker only for a v14 non-delegate C method.
 * Check the field directly before also checking a real RSM record. */
static int rm_marker_writer(void)
{
	int saved_version = ain->version;
	int saved_count = call_stack_ptr;
	struct function_call saved = call_stack[0];
	struct resume_frame_extra x;
	call_stack_ptr = 1;
	call_stack[0] = (struct function_call) {
		.fno = ain->main, .return_address = VM_RETURN, .is_method = true,
	};
	ain->version = 14;
	vm_resume_frame_extra(0, &x);
	bool ok = x.dg_page == INT32_MIN;
	call_stack[0].is_method = false;
	vm_resume_frame_extra(0, &x);
	ok &= x.dg_page == 0;
	call_stack[0].is_method = true;
	call_stack[0].return_address = 1234;
	vm_resume_frame_extra(0, &x);
	ok &= x.dg_page == 0;
	call_stack[0].return_address = VM_RETURN;
	call_stack[0].is_delegate_call = true;
	vm_resume_frame_extra(0, &x);
	ok &= x.dg_page == 0;
	call_stack[0].is_delegate_call = false;
	ain->version = 13;
	vm_resume_frame_extra(0, &x);
	ok &= x.dg_page == 0;
	ain->version = saved_version;
	call_stack[0] = saved;
	call_stack_ptr = saved_count;
	if (!ok) printf("marker writer: version, call kind, or return-address guard failed\n");
	return !ok;
}

static int rm_rsm_c_entry(void)
{
	config.save_format = SAVE_FORMAT_RSM;
	if (rce_case(true)) return 1;
	int page = 0, cursor = -1;
	if (!rf_image_frame(rf.file1, 0, &page, &cursor) || page != INT32_MIN) {
		printf("RSM C method did not write the marker\n");
		return 1;
	}
	/* Mismatch only the comment's function, not the actual saved frame. */
	if (!rm_rewrite_rsm(rf.file1, 0, true, rf.fM, false, 0)) return 1;
	vm_load_image(RF_KEY, rf.file1);
	if (call_stack_ptr != 1 || call_stack[0].is_method) {
		printf("RSM C method accepted metadata for a different function\n");
		return 1;
	}
	/* Absence of metadata cannot distinguish a C method from nopop. */
	if (!rf_strip(rf.file1)) return 1;
	vm_load_image(RF_KEY, rf.file1);
	if (call_stack_ptr != 1 || call_stack[0].is_method) {
		printf("RSM C entry guessed a method in an image without metadata\n");
		return 1;
	}
	return 0;
}

/* O -> CALLMETHOD M -> DG_CALL L -> CALLFUNC G. Injecting the marker
 * into each non-C frame must not change its real call kind. A delegate with
 * a negative page has no saved delegate page and uses its existing fallback. */
static int rm_rsm_opcodes(void)
{
	config.save_format = SAVE_FORMAT_RSM;
	rf_bad = 0;
	rf_build(false);
	rf.file1 = "rm-opcodes.dat";
	rf_run_reference();
	rf_expect_reference(false);
	if (rf_bad || rf.res.nflags != 4 || !(rf.res.flags[1] & 1)
	    || !(rf.res.flags[2] & 2) || (rf.res.flags[3] & 3)) {
		printf("marker opcode reference did not have the expected call kinds\n");
		return 1;
	}
	for (int target = 1; target <= 3; target++) {
		/* Replacing a prior marker with another is harmless: each frame is
		 * independently constrained by its actual return instruction. */
		if (!rm_rewrite_rsm(rf.file1, target, false, 0, true, INT32_MIN)) return 1;
		vm_load_image(RF_KEY, rf.file1);
		if (call_stack_ptr != 4 || !call_stack[1].is_method
		    || !call_stack[2].is_delegate_call || call_stack[2].is_method
		    || call_stack[3].is_method || call_stack[3].is_delegate_call
		    || (target >= 2 && !call_stack[2].dg_resume_pending)) {
			printf("RSM marker changed opcode-derived frame kinds at %d\n", target);
			return 1;
		}
	}
	return 0;
}

/* Conflicting JSON fields are synthetic inputs, not claims about the native
 * format. Explicit 0/1 wins; missing or invalid values use the marker. */
static int rm_json_fields(void)
{
	config.save_format = SAVE_FORMAT_JSON;
	if (rce_case(true)) return 1;
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", sp_dir, rf.file1);
	char *original = file_read(path, NULL);
	if (!original) return 1;
	struct { int kind, page; bool expected; } cases[] = {
		{ 0, INT32_MIN, false }, { 1, 0, true },
		{ 2, INT32_MIN, true }, { 2, 0, false },
		{ 3, INT32_MIN, true }, { 3, 0, false },
		{ 4, INT32_MIN, true }, { 5, INT32_MIN, true },
	};
	bool ok = true;
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0] && ok; i++) {
		cJSON *image = cJSON_Parse(original);
		cJSON *frames = image ? cJSON_GetObjectItem(image, "call-stack") : NULL;
		cJSON *frame = frames ? cJSON_GetArrayItem(frames, 0) : NULL;
		if (!frame || cJSON_GetArraySize(frames) != 1) { cJSON_Delete(image); ok = false; break; }
		cJSON_DeleteItemFromObject(frame, "c-method");
		cJSON_DeleteItemFromObject(frame, "delegate-page");
		cJSON_AddNumberToObject(frame, "delegate-page", cases[i].page);
		if (cases[i].kind == 0 || cases[i].kind == 1)
			cJSON_AddNumberToObject(frame, "c-method", cases[i].kind);
		else if (cases[i].kind == 3)
			cJSON_AddNumberToObject(frame, "c-method", 2);
		else if (cases[i].kind == 4)
			cJSON_AddNumberToObject(frame, "c-method", 0.5);
		else if (cases[i].kind == 5)
			cJSON_AddItemToObject(frame, "c-method", cJSON_CreateString("1"));
		char *encoded = cJSON_PrintUnformatted(image);
		cJSON_Delete(image);
		FILE *fp = encoded ? fopen(path, "wb") : NULL;
		ok = fp && fputs(encoded, fp) >= 0;
		if (fp && fclose(fp)) ok = false;
		free(encoded);
		if (!ok) break;
		vm_load_image(RF_KEY, rf.file1);
		ok = call_stack_ptr == 1 && !call_stack[0].is_delegate_call
			&& call_stack[0].is_method == cases[i].expected;
		if (!ok) printf("JSON marker case %u: method=%d, expected=%d\n", i,
				call_stack_ptr == 1 ? call_stack[0].is_method : -1, cases[i].expected);
	}
	free(original);
	return !ok;
}

static int test_resume_marker(void)
{
	sp_setup_dir();
	sp_run("marker writer gates", rm_marker_writer);
	sp_run("RSM C entry marker and fallback", rm_rsm_c_entry);
	sp_run("RSM marker on non-C call kinds", rm_rsm_opcodes);
	sp_run("JSON marker and method flag", rm_json_fields);
	sp_remove_dir();
	printf("RESUME-MARKER summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}
