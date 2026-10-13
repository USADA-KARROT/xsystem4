/*
 * v13-heap (2026-10-13): heap fixes found by running a game older than v14
 * on upstream e8bd5ab and on this branch side by side.
 *
 * VA  array_pushback() must not finalize what the memory of the new element
 *     held before. Upstream grows the array through realloc_array(), which
 *     gives every new element its initial value; this branch finds the
 *     allocation large enough or grows it itself, and handed the stale bits
 *     to variable_set(), which released them as a heap reference whenever
 *     the element type is one.
 *
 * VB  exit_unref() must count a slot the way heap_unref() does. A string an
 *     expression leaves on the stack carries HEAP_TEMP_FLAG in its count
 *     (stack_push_string) until a function takes it as an argument;
 *     exit_unref() compared the whole word with 1, so such a string was
 *     decremented and never freed. hll_call() releases its arguments this
 *     way (variable_fini without destructors).
 *
 * Everything here is synthetic: pages, slots and strings made by the
 * fixture. No game bytecode and no game data. Every case runs in a child.
 */
#include <sys/wait.h>
#include <unistd.h>

static int vh_failures;
#define VH_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fflush(stdout); vh_failures++; } } while (0)

static const int vh_versions[] = { 14, 13 };

/* A live slot that no page refers to: a page slot without a page. */
static int vh_witness(void)
{
	int w = heap_alloc_slot(VM_PAGE);
	heap[w].page = NULL;
	return w;
}

static int vh_string(const char *text)
{
	int s = heap_alloc_slot(VM_STRING);
	heap[s].s = make_string(text, strlen(text));
	return s;
}

static bool vh_alive(int slot, uint32_t seq)
{
	return heap[slot].seq == seq && HEAP_REF(slot) > 0;
}

/* An array of `nr` elements in an allocation of `cap`: what is past nr_vars
 * holds `stale`, the way memory that was used before holds anything. */
static struct page *vh_slack_array(enum ain_data_type a_type, int nr, int cap, int stale)
{
	struct page *p = alloc_page(ARRAY_PAGE, a_type, cap);
	p->array.struct_type = -1;
	p->array.rank = 1;
	for (int i = 0; i < cap; i++)
		p->values[i].i = i < nr ? -1 : stale;
	p->nr_vars = nr;
	return p;
}

/* --- VA1: the stale bits name a live slot --- */

static void vh_pushback_stale_as(enum ain_data_type a_type, const char *what)
{
	const bool strings = a_type == AIN_ARRAY_STRING;
	size_t live = live_slots();
	int w = vh_witness(), e[4];
	uint32_t wseq = heap[w].seq;
	heap_ref(w); /* a second owner: one wrong release is seen and survived */
	struct page *p = vh_slack_array(a_type, 1, 8, w);
	p->values[0].i = e[0] = strings ? vh_string("first") : vh_witness();
	for (int i = 1; i < 4; i++) {
		e[i] = strings ? vh_string("pushed") : vh_witness();
		p = array_pushback(p, (union vm_value) { .i = e[i] }, a_type, -1);
		if (!vh_alive(w, wseq) || HEAP_REF(w) != 2) {
			VH_CHECK(false, "version %d, %s array, push %d: the slot named by the stale bits of the new element has "
				"%d reference(s), want 2 (it was released as the element's old value)", ain->version, what, i,
				vh_alive(w, wseq) ? HEAP_REF(w) : 0);
			return; /* the heap is not to be trusted from here on */
		}
	}
	bool same = p && p->type == ARRAY_PAGE && p->a_type == a_type && p->array.rank == 1 && p->nr_vars == 4;
	for (int i = 0; same && i < 4; i++)
		same = p->values[i].i == e[i] && HEAP_REF(e[i]) == 1;
	VH_CHECK(same, "version %d, %s array: the four elements are not the values pushed, each with one reference",
		ain->version, what);
	heap_unref(heap_alloc_page(p));
	heap_unref(w);
	heap_unref(w);
	VH_CHECK(live_slots() == live, "version %d, %s array: %+ld live slot(s) after the array and the witness were released",
		ain->version, what, (long)live_slots() - (long)live);
}

static void vh_pushback_stale(void)
{
	const int version = ain->version;
	for (unsigned k = 0; k < sizeof(vh_versions) / sizeof(vh_versions[0]); k++) {
		ain->version = vh_versions[k];
		vh_pushback_stale_as(AIN_ARRAY_STRING, "string");
		vh_pushback_stale_as(AIN_ARRAY_STRUCT, "struct");
		vh_pushback_stale_as(AIN_ARRAY_DELEGATE, "delegate");
	}
	ain->version = version;
	puts("VA1 the stale bits past the end of string, struct and delegate arrays name a live slot: three pushes each, "
	     "versions 14 and 13");
}

/* --- VA2 (guard): growth from nothing, and a push after a pop --- */

static void vh_pushback_growth(void)
{
	enum { N = 100 };
	static int e[N];
	const int version = ain->version;
	for (unsigned k = 0; k < sizeof(vh_versions) / sizeof(vh_versions[0]); k++) {
		ain->version = vh_versions[k];
		size_t live = live_slots();
		struct page *p = NULL;
		for (int i = 0; i < N; i++) {
			e[i] = vh_string("element");
			p = array_pushback(p, (union vm_value) { .i = e[i] }, AIN_ARRAY_STRING, -1);
		}
		bool same = p && p->type == ARRAY_PAGE && p->a_type == AIN_ARRAY_STRING && p->array.rank == 1 && p->nr_vars == N;
		for (int i = 0; same && i < N; i++)
			same = p->values[i].i == e[i] && HEAP_REF(e[i]) == 1;
		VH_CHECK(same, "version %d: %d pushes onto no array did not make the array of those %d values", ain->version, N, N);
		if (!same)
			break;
		/* The slot of the popped element is the next one handed out. */
		uint32_t last_seq = heap[e[N - 1]].seq;
		p = array_popback(p);
		VH_CHECK(p && p->nr_vars == N - 1 && !vh_alive(e[N - 1], last_seq), "version %d: PopBack kept the last element",
			ain->version);
		int again = vh_string("again");
		p = array_pushback(p, (union vm_value) { .i = again }, AIN_ARRAY_STRING, -1);
		VH_CHECK(p && p->nr_vars == N && p->values[N - 1].i == again && HEAP_REF(again) == 1,
			"version %d: the element pushed after a PopBack has %d reference(s), want 1", ain->version, HEAP_REF(again));
		heap_unref(heap_alloc_page(p));
		VH_CHECK(live_slots() == live, "version %d: %+ld live slot(s) after the grown array was released", ain->version,
			(long)live_slots() - (long)live);
	}
	ain->version = version;
	printf("VA2 guard: %d pushes from no array, a PopBack and one more push, versions 14 and 13\n", N);
}

/* --- VA3 (guard): the bits of a number are never a reference --- */

static void vh_pushback_scalar(void)
{
	static const enum ain_data_type types[] = { AIN_ARRAY_INT, AIN_ARRAY_FLOAT, AIN_ARRAY_BOOL };
	const int version = ain->version;
	for (unsigned k = 0; k < sizeof(vh_versions) / sizeof(vh_versions[0]); k++) {
		ain->version = vh_versions[k];
		for (unsigned t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
			size_t live = live_slots();
			int w = vh_witness();
			uint32_t wseq = heap[w].seq;
			heap_ref(w);
			struct page *p = vh_slack_array(types[t], 1, 8, w);
			p->values[0].i = 5;
			for (int i = 1; i < 4; i++)
				p = array_pushback(p, (union vm_value) { .i = 5 + i }, types[t], -1);
			VH_CHECK(vh_alive(w, wseq) && HEAP_REF(w) == 2, "version %d, type %d: pushing a number released the stale bits",
				ain->version, types[t]);
			VH_CHECK(p && p->nr_vars == 4 && p->values[0].i == 5 && p->values[1].i == 6 && p->values[2].i == 7
				&& p->values[3].i == 8, "version %d, type %d: the numbers pushed are not the elements", ain->version, types[t]);
			heap_unref(heap_alloc_page(p));
			if (vh_alive(w, wseq))
				heap_unref(w);
			if (vh_alive(w, wseq))
				heap_unref(w);
			VH_CHECK(live_slots() == live, "version %d, type %d: %+ld live slot(s) left", ain->version, types[t],
				(long)live_slots() - (long)live);
		}
	}
	ain->version = version;
	puts("VA3 guard: int, float and bool arrays with a live slot number past the end, versions 14 and 13");
}

/* --- VB1: a temporary string with one owner --- */

/* A string the way an expression leaves it on the stack. */
static int vh_temp_string(struct string *s)
{
	stack_push_string(s);
	return stack_pop().i;
}

static void vh_exit_unref_temp(void)
{
	static const char *const how_names[] = { "exit_unref()", "variable_fini() without destructors", "the page that holds it" };
	const int version = ain->version;
	for (unsigned k = 0; k < sizeof(vh_versions) / sizeof(vh_versions[0]); k++) {
		ain->version = vh_versions[k];
		for (int how = 0; how < 3; how++) {
			size_t live = live_slots();
			struct string *text = make_string("temporary", 9);
			int s = vh_temp_string(string_ref(text));
			VH_CHECK(heap[s].ref == (HEAP_TEMP_FLAG | 1) && text->ref == 2,
				"setup: a string pushed on the stack has count word %#x, want the mark and 1", (unsigned)heap[s].ref);
			if (how == 0) {
				exit_unref(s);
			} else if (how == 1) {
				/* what hll_call() does with a string argument after the call */
				variable_fini((union vm_value) { .i = s }, AIN_STRING, false);
			} else {
				struct page *p = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, 2);
				p->array.struct_type = -1;
				p->array.rank = 1;
				p->values[0].i = s;
				p->values[1].i = vh_string("plain");
				exit_unref(heap_alloc_page(p));
			}
			VH_CHECK(heap[s].ref == 0 && text->ref == 1,
				"version %d, released through %s: the temporary string was not freed (count word %#x, %u owner(s) of its "
				"text; want 0 and 1)", ain->version, how_names[how], (unsigned)heap[s].ref, (unsigned)text->ref);
			if (how < 2) {
				int next = heap_alloc_slot(VM_STRING);
				heap[next].s = string_ref(&EMPTY_STRING);
				VH_CHECK(next == s, "version %d, released through %s: slot %d did not go back to the free list (the next "
					"one handed out is %d)", ain->version, how_names[how], s, next);
				heap_unref(next);
			}
			VH_CHECK(live_slots() == live, "version %d, released through %s: %+ld live slot(s) left", ain->version,
				how_names[how], (long)live_slots() - (long)live);
			free_string(text);
		}
	}
	ain->version = version;
	puts("VB1 a marked string with one owner, released through exit_unref(), through variable_fini() without destructors "
	     "and through its page, versions 14 and 13");
}

/* --- VB2 (guard): other owners, no mark, already released --- */

static void vh_exit_unref_guards(void)
{
	const int version = ain->version;
	for (unsigned k = 0; k < sizeof(vh_versions) / sizeof(vh_versions[0]); k++) {
		ain->version = vh_versions[k];
		size_t live = live_slots();
		struct string *text = make_string("shared", 6);
		int s = vh_temp_string(string_ref(text));
		heap_ref(s);
		exit_unref(s);
		VH_CHECK(heap[s].ref == (HEAP_TEMP_FLAG | 1) && text->ref == 2,
			"version %d: a marked string with two owners has count word %#x after exit_unref(), want the mark and 1",
			ain->version, (unsigned)heap[s].ref);
		heap_unref(s);
		VH_CHECK(heap[s].ref == 0 && text->ref == 1, "version %d: heap_unref() did not free the marked string", ain->version);
		int u = heap_alloc_slot(VM_STRING);
		heap[u].s = string_ref(text);
		heap_ref(u);
		exit_unref(u);
		VH_CHECK(heap[u].ref == 1 && text->ref == 2, "version %d: an unmarked string with two owners has count word %#x "
			"after exit_unref(), want 1", ain->version, (unsigned)heap[u].ref);
		exit_unref(u);
		VH_CHECK(heap[u].ref == 0 && text->ref == 1, "version %d: exit_unref() did not free an unmarked string",
			ain->version);
		exit_unref(u); /* released already: a warning and nothing else */
		VH_CHECK(heap[u].ref == 0 && text->ref == 1, "version %d: exit_unref() of a released slot changed it", ain->version);
		VH_CHECK(live_slots() == live, "version %d: %+ld live slot(s) left", ain->version, (long)live_slots() - (long)live);
		free_string(text);
	}
	ain->version = version;
	puts("VB2 guard: a marked string with two owners keeps its mark, an unmarked one is counted as before, a released "
	     "slot is left alone, versions 14 and 13");
}

static int test_v13_heap(void)
{
	static const struct { const char *name; void (*run)(void); } cases[] = {
		{ "VA1", vh_pushback_stale }, { "VA2", vh_pushback_growth }, { "VA3", vh_pushback_scalar },
		{ "VB1", vh_exit_unref_temp }, { "VB2", vh_exit_unref_guards },
	};
	int failed = 0, environment = 0;
	for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fflush(stdout);
		fflush(stderr);
		pid_t pid = fork();
		if (pid == 0) {
			alarm(60);
			vh_failures = 0;
			cases[i].run();
			fflush(stdout);
			_exit(vh_failures ? 1 : 0);
		}
		int st = 0;
		if (pid < 0 || waitpid(pid, &st, 0) != pid) {
			printf("%s ENVIRONMENT (fork/wait failed)\n", cases[i].name);
			environment++;
		} else if (WIFSIGNALED(st)) {
			printf("%s FAIL (terminated by signal %d)\n", cases[i].name, WTERMSIG(st));
			failed++;
		} else if (WEXITSTATUS(st) == 0) {
			printf("%s ok\n", cases[i].name);
		} else {
			printf("%s FAIL (exit %d)\n", cases[i].name, WEXITSTATUS(st));
			failed++;
		}
	}
	printf("V13-HEAP %s: %d of %zu cases failed, %d environment error(s)\n", failed || environment ? "FAIL" : "PASS", failed,
		sizeof(cases) / sizeof(cases[0]), environment);
	return environment ? 2 : failed ? 1 : 0;
}
