/* Declaration/prototype mismatches outside Array: TextFile, VSFile,
 * FileOperation, SealEngine string returns, LayoutBox float (2026-09-28).
 * Adapted from the batch-2 review probe; each case runs in a forked child. */
#include <sys/wait.h>
#include <unistd.h>
#include "parts.h"
static const char *cf_TMPDIR_;
static int cf_fails;

/* nth: 0-based index among declarations with this name (the "#N" in ain_code.txt) */
static int cf_fn(const char *lib, const char *name, int nth, int *libno)
{
	int l = ain_get_library(ain, lib);
	*libno = l;
	if (l < 0) return -1;
	for (int i = 0, k = 0; i < ain->libraries[l].nr_functions; i++)
		if (!strcmp(ain->libraries[l].functions[i].name, name) && k++ == nth)
			return i;
	return -1;
}
static int cf_str_slot(const char *s) { return heap_alloc_string(make_string(s, strlen(s))); }
#define CF_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); fflush(stdout); _exit(1); } } while (0)

static char *cf_tmpfile_with(const char *name, const char *content)
{
	char *path = xmalloc(strlen(cf_TMPDIR_) + strlen(name) + 2);
	sprintf(path, "%s/%s", cf_TMPDIR_, name);
	FILE *fp = fopen(path, "wb"); fputs(content, fp); fclose(fp);
	return path;
}
static void cf_t_textfile_readall(void)
{
	int lib, f = cf_fn("TextFile", "ReadAll", 0, &lib);
	char *path = cf_tmpfile_with("readall.txt", "hello\nworld");
	int text = cf_str_slot("");
	int sp = stack_ptr;
	stack_push(cf_str_slot(path)); stack_push(text);
	hll_call(lib, f, 0);
	CF_CHECK(stack_ptr == sp + 1 && stack[sp].i == 1, "ret");
	CF_CHECK(!strcmp(heap[text].s->text, "hello\nworld"), "text='%s'", heap[text].s->text);
	printf("  TextFile.ReadAll ok\n");
}
static void cf_t_textfile_readline(void)
{
	int lib, fo = cf_fn("TextFile", "OpenReader", 0, &lib), fl = cf_fn("TextFile", "ReadLine", 0, &lib);
	char *path = cf_tmpfile_with("lines.txt", "l1\r\nsecond line\n");
	int sp = stack_ptr;
	stack_push(cf_str_slot(path)); hll_call(lib, fo, 0);
	int h = stack_pop().i;
	CF_CHECK(h >= 0, "OpenReader");
	const char *want[2] = {"l1", "second line"};
	for (int i = 0; i < 2; i++) {
		int text = cf_str_slot("");
		stack_push(h); stack_push(text); hll_call(lib, fl, 0);
		CF_CHECK(stack_pop().i == 1, "ReadLine %d ret", i);
		CF_CHECK(!strcmp(heap[text].s->text, want[i]), "line %d='%s'", i, heap[text].s->text);
	}
	int text = cf_str_slot("keep");
	stack_push(h); stack_push(text); hll_call(lib, fl, 0);
	CF_CHECK(stack_pop().i == 0 && !strcmp(heap[text].s->text, "keep"), "EOF case");
	CF_CHECK(stack_ptr == sp, "stack");
	printf("  TextFile.ReadLine ok\n");
}
static void cf_t_vsfile(void)
{
	int lib, fw = cf_fn("VSFile", "OpenForWrite", 0, &lib), fr = cf_fn("VSFile", "OpenForRead", 0, &lib);
	int fws = cf_fn("VSFile", "WriteString", 0, &lib), frs = cf_fn("VSFile", "ReadString", 0, &lib);
	int fc = cf_fn("VSFile", "Close", 0, &lib);
	char path[1024]; snprintf(path, sizeof path, "%s/t.vsf", cf_TMPDIR_);
	stack_push(cf_str_slot(path)); hll_call(lib, fw, 0); CF_CHECK(stack_pop().i == 1, "OpenForWrite");
	stack_push(cf_str_slot("chunk")); hll_call(lib, fws, 0); stack_pop();
	hll_call(lib, fc, 0); stack_pop();
	stack_push(cf_str_slot(path)); hll_call(lib, fr, 0); CF_CHECK(stack_pop().i == 1, "OpenForRead");
	int text = cf_str_slot("");
	int sp = stack_ptr;
	stack_push(text); hll_call(lib, frs, 0);
	CF_CHECK(stack_ptr == sp + 1 && stack_pop().i == 1, "ReadString ret");
	CF_CHECK(!strcmp(heap[text].s->text, "chunk"), "text='%s'", heap[text].s->text);
	hll_call(lib, fc, 0); stack_pop();
	printf("  VSFile.ReadString(wrap) ok\n");
}
static void cf_t_filelist(void)
{
	int lib, ff = cf_fn("FileOperation", "GetFileList", 0, &lib), fd = cf_fn("FileOperation", "GetFolderList", 0, &lib);
	cf_tmpfile_with("a.txt", "x");
	int sp = stack_ptr;
	stack_push(cf_str_slot(cf_TMPDIR_)); hll_call(lib, ff, 0);
	CF_CHECK(stack_ptr == sp + 1, "stack");
	int r = stack_pop().i;
	CF_CHECK(r > 1 && heap[r].type == VM_PAGE && heap[r].page && heap[r].page->type == ARRAY_PAGE, "ret %d not an array", r);
	int found = 0;
	for (int i = 0; i < heap[r].page->nr_vars; i++)
		if (!strcmp(heap[heap[r].page->values[i].i].s->text, "a.txt")) found = 1;
	CF_CHECK(found, "a.txt missing");
	stack_push(cf_str_slot("/nonexistent/zz")); hll_call(lib, fd, 0);
	r = stack_pop().i;
	CF_CHECK(r > 1 && heap[r].page && heap[r].page->nr_vars == 0, "missing folder -> ret %d", r);
	printf("  FileOperation.GetFileList/GetFolderList ok\n");
}
static void cf_t_seal_text(void)
{
	/* GetInstanceInfoText needs a Reign plugin (GL); Tool_CreateFBXAscii shares
	 * the same return-path fix and runs headless. */
	int lib, f = cf_fn("SealEngine", "Tool_CreateFBXAscii", 0, &lib);
	int sp = stack_ptr;
	stack_push(0); stack_push(0); hll_call(lib, f, 0);
	CF_CHECK(stack_ptr == sp + 1, "stack");
	int r = stack_pop().i;
	CF_CHECK(string_index_valid(r) && heap[r].s && heap[r].s->size == 0, "ret slot %d not an empty string", r);
	int f2 = cf_fn("SealEngine", "GetInstanceInfoText", 0, &lib);
	CF_CHECK(libraries[lib][f2].return_type == &ffi_type_pointer, "InfoText CIF ret");
	printf("  SealEngine.Tool_CreateFBXAscii -> empty string slot %d\n", r);
}
static void cf_t_layoutbox(void)
{
	/* parts_table needs PE_Init (GL context); check the binding the selector
	 * chose and the CIF instead. */
	int lib, fs = cf_fn("PartsEngine", "SetLayoutBoxReturn", 0, &lib), fg = cf_fn("PartsEngine", "GetLayoutBoxReturnSize", 0, &lib);
#ifndef CONTROL
	CF_CHECK(libraries[lib][fs].fun == (void*)PE_SetLayoutBoxReturnF, "SetLayoutBoxReturn bound to %p", libraries[lib][fs].fun);
	CF_CHECK(libraries[lib][fs].args[2] == &ffi_type_float, "arg2 CIF");
	CF_CHECK(libraries[lib][fg].fun == (void*)PE_GetLayoutBoxReturnSizeF, "GetLayoutBoxReturnSize bound to %p", libraries[lib][fg].fun);
#else
	CF_CHECK(libraries[lib][fs].fun != (void*)PE_SetLayoutBoxReturn, "control: still int SetLayoutBoxReturn (float arg read from w2)");
#endif
	CF_CHECK(libraries[lib][fg].return_type == &ffi_type_float, "ret CIF");
	printf("  PartsEngine LayoutBox bound to float variants\n");
}
static void cf_run(const char *label, void (*t)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) { t(); fflush(stdout); _exit(0); }
	int st = 0; waitpid(pid, &st, 0);
	if (WIFSIGNALED(st)) { printf("%-40s CRASH signal %d\n", label, WTERMSIG(st)); cf_fails++; }
	else if (WEXITSTATUS(st)) { printf("%-40s FAIL\n", label); cf_fails++; }
	else printf("%-40s PASS\n", label);
}

static int test_cif(void)
{
	char tmpl[] = "/tmp/xsys4-cif-XXXXXX";
	cf_TMPDIR_ = mkdtemp(tmpl);
	assert(cf_TMPDIR_);
	cf_run("TextFile.ReadAll", cf_t_textfile_readall);
	cf_run("TextFile.OpenReader/ReadLine", cf_t_textfile_readline);
	cf_run("VSFile.ReadString", cf_t_vsfile);
	cf_run("FileOperation.GetFileList/GetFolderList", cf_t_filelist);
	cf_run("SealEngine string return", cf_t_seal_text);
	cf_run("PartsEngine LayoutBox binding", cf_t_layoutbox);
	printf("CIF summary: %d failure(s)\n", cf_fails);
	char cmd[128]; snprintf(cmd, sizeof cmd, "rm -rf %s", cf_TMPDIR_); system(cmd);
	return cf_fails ? 88 : 0;
}
