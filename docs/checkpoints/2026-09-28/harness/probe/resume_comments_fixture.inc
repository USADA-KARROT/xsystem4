/* Normal comment round trips. Include after resume_frames_fixture.inc. */
static struct page *rc_make(const char *const *items, int n)
{
	union vm_value dim = { .i = n };
	struct page *page = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
	for (int i = 0; i < n; i++) {
		heap_unref(page->values[i].i); /* alloc_array initialized an empty string */
		int slot = heap_alloc_slot(VM_STRING);
		heap[slot].s = make_string(items[i], strlen(items[i]));
		page->values[i].i = slot;
	}
	return page;
}

static void rc_comments_release(struct page *page)
{
	if (!page) return;
	delete_page_vars(page);
	free_page(page);
}

static void rc_check(const char *file, const char *const *items, int n)
{
	int ok = 0;
	struct page *got = vm_load_image_comments(RF_KEY, file, &ok);
	RF_EXPECT(ok && (n ? got && got->nr_vars == n : !got || !got->nr_vars),
		"%s: %d comments, want %d (success %d)", file, got ? got->nr_vars : 0, n, ok);
	if (got && got->nr_vars == n)
		for (int i = 0; i < n; i++)
			RF_EXPECT(!strcmp(heap_get_string(got->values[i].i)->text, items[i]),
				"%s: comment %d changed", file, i);
	rc_comments_release(got);
}

static void rc_write(const char *file, const char *const *items, int n)
{
	struct page *page = rc_make(items, n);
	RF_EXPECT(vm_write_image_comments(RF_KEY, file, page), "%s: write failed", file);
	rc_comments_release(page);
	rc_check(file, items, n);
}

static struct rsave *rc_read(const char *file)
{
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/%s", sp_dir, file);
	enum savefile_error error;
	struct rsave *save = rsave_read(path, RSAVE_READ_ALL, &error);
	RF_EXPECT(error == SAVEFILE_SUCCESS, "%s: full RSM read failed", file);
	return error == SAVEFILE_SUCCESS ? save : NULL;
}

/* A generated v14 image retains one final frame record while every earlier
 * user comment, even a byte-for-byte duplicate, remains visible. */
static int rc_full_image(void)
{
	config.save_format = SAVE_FORMAT_RSM;
	rf_bad = 0;
	rf_build(false);
	rf.file1 = "rc-image.dat";
	rf_run_reference();
	rf_expect_reference(false);
	struct rf_step *ref = xcalloc(rf.ntrace + 1, sizeof *ref);
	memcpy(ref, rf.trace, rf.ntrace * sizeof *ref);
	int nref = rf.ntrace;
	struct rf_result rres = rf.res;
	struct rsave *save = rc_read(rf.file1);
	RF_EXPECT(save && save->nr_comments == 1, "reference image needs exactly one frame record");
	if (!save || save->nr_comments != 1) {
		if (save) rsave_free(save);
		free(ref);
		return 1;
	}
	char *record = xstrdup(save->comments[0]);
	rsave_free(save);
	const char *comments[] = { "", "ordinary", "xsystem4-frames 1 ordinary text",
		record, record, "\x82\xa0", "\xb0\xa1" };
	rc_write(rf.file1, comments, 7);
	save = rc_read(rf.file1);
	if (save) {
		RF_EXPECT(save->nr_comments == 8, "full image lost or added comments");
		if (save->nr_comments == 8) {
			for (int i = 0; i < 7; i++)
				RF_EXPECT(!strcmp(save->comments[i], comments[i]), "stored comment %d changed", i);
			RF_EXPECT(!strcmp(save->comments[7], record), "last frame record changed");
		}
		rsave_free(save);
	}
	const char *replacement[] = { record, "replacement" };
	rc_write(rf.file1, replacement, 2);
	rc_write(rf.file1, comments, 0);
	rc_write(rf.file1, comments, 7);
	struct rf_record got;
	rf_run_loaded(rf.file1, RF_NONE, "rc-image.trace", &got);
	rf_compare("comments on a live frame image", ref, nref, &rres, &got);
	free(got.steps);
	free(ref);
	free(record);
	return rf_bad;
}

/* An older full image without frame metadata can have a prefix-like user
 * comment. It must stay visible and replacement must discard it. */
static int rc_old_full(void)
{
	config.save_format = SAVE_FORMAT_RSM;
	rf_bad = 0;
	rf_build(false);
	rf.file1 = "rc-old.dat";
	rf_run_reference();
	RF_EXPECT(rf_strip(rf.file1), "could not prepare a prior image");
	const char *old[] = { "xsystem4-frames 1 ordinary text", "tail" };
	const char *newer[] = { "replacement", "xsystem4-frames 1 ordinary text" };
	rc_write(rf.file1, old, 2);
	rc_write(rf.file1, newer, 2);
	rc_write(rf.file1, old, 2);
	return rf_bad;
}

static int rc_only_format(int version, int format)
{
	ain->version = version;
	config.save_format = format;
	rf_bad = 0;
	const char *comments[] = { "", "xsystem4-frames 1 1|0 0 0 0 0 0 0",
		"xsystem4-frames 1 ordinary text", "\x82\xa0", "\xb0\xa1" };
	const char *last[] = { "ordinary", "xsystem4-frames 1 1|0 0 0 0 0 0 0" };
	rc_write("rc-only.dat", comments, 5);
	rc_write("rc-only.dat", last, 2);
	rc_write("rc-only.dat", comments, 0);
	rc_write("rc-only.dat", comments, 5);
	return rf_bad;
}
/* Ordinary comments are a positive control for both save formats. */
static int rc_ordinary_format(int version, int format)
{
	ain->version = version;
	config.save_format = format;
	rf_bad = 0;
	const char *initial[] = { "", "ordinary", "ordinary", "spaces and punctuation: !?" };
	const char *replacement[] = { "replacement", "" };
	rc_write("rc-ordinary.dat", initial, 4);
	rc_write("rc-ordinary.dat", replacement, 2);
	rc_write("rc-ordinary.dat", initial, 0);
	rc_write("rc-ordinary.dat", initial, 4);
	return rf_bad;
}
static int rc_ordinary_rsm14(void) { return rc_ordinary_format(14, SAVE_FORMAT_RSM); }
static int rc_ordinary_rsm13(void) { return rc_ordinary_format(13, SAVE_FORMAT_RSM); }
static int rc_ordinary_json14(void) { return rc_ordinary_format(14, SAVE_FORMAT_JSON); }

static int rc_only_rsm14(void) { return rc_only_format(14, SAVE_FORMAT_RSM); }
static int rc_only_rsm13(void) { return rc_only_format(13, SAVE_FORMAT_RSM); }
static int rc_only_json14(void) { return rc_only_format(14, SAVE_FORMAT_JSON); }

static int test_resume_comments(void)
{
	sp_setup_dir();
	sp_run("RSM full image comments and frames", rc_full_image);
	sp_run("RSM older image prefix comments", rc_old_full);
	sp_run("v14 RSM ordinary comments", rc_ordinary_rsm14);
	sp_run("v13 RSM ordinary comments", rc_ordinary_rsm13);
	sp_run("v14 JSON ordinary comments", rc_ordinary_json14);
	sp_run("v14 RSM comments-only", rc_only_rsm14);
	sp_run("v13 RSM comments-only", rc_only_rsm13);
	sp_run("v14 JSON comments-only", rc_only_json14);
	sp_remove_dir();
	printf("RESUME-COMMENTS summary: %d/%d case(s) failed\n", sp_failed_cases, sp_cases);
	return sp_failed_cases ? 88 : 0;
}
