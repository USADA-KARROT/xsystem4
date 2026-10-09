/* aref-value: A_REF of a struct of plain numbers makes a copy.
 *
 * Original (static disassembly): A_REF is "PageCopy" (0x66b790 -> 0x6794d0).
 * For a struct page (case 4, 0x67959a) it takes the type (0x681f30),
 * allocates a new object without running a constructor (0x679b30(type, 0))
 * and copies the members; the new slot is pushed. Every A_REF of a struct is
 * a copy.
 *
 * This VM shares the page of a v14 struct instead (heap_ref, the same slot).
 * `return CASColor::BLACK;` (.GLOBALREF, A_REF, RETURN) then hands out the
 * global itself, and what the callers do with their "copy" reaches the
 * constant: in a battle PlayerFrameView@GetAddColor returned the acting
 * unit's focus colour for every other unit, and dozens of unrelated parts
 * were given that add colour. The fix copies a struct whose members are all
 * int, float, bool, long int or enum (CASColor, CASPos, CASSize, ...); a
 * struct with reference members keeps sharing its page, as before (the full
 * value semantics of A_REF, X_SET and the others is a separate change).
 *
 * Cases (each in its own fork, after the real start-up function "0"):
 *   AV1 real bytecode PlayerFrameView@GetAddColor(Waiting): the result is
 *       not the global CASColor::BLACK's page, has its members, and writing
 *       to it or releasing it leaves the constant and its reference count
 *       alone.
 *   AV2 500 rounds of GetAddColor and GetMulColor over the focus values:
 *       BLACK and WHITE keep their members and reference counts, each
 *       result is a page of its own, and the live slots do not grow.
 * Only functions that exist before the fix are called, so before-check
 * builds.
 */
static int av_failures;
#define AV_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); av_failures++; } } while (0)

static bool av_is_color(int slot, int r, int g, int b, int a)
{
	if (!gi_live_struct(slot, gi_struct("CASColor")))
		return false;
	union vm_value *v = heap[slot].page->values;
	return v[0].i == r && v[1].i == g && v[2].i == b && v[3].i == a;
}

/* One call of a PlayerFrameView colour getter (it does not touch `this`);
 * the caller owns the result. */
static int av_get(const char *fn, int focus)
{
	int sp = stack_ptr;
	stack_push(focus);
	vm_call(gi_fn(fn), -1);
	int slot = stack_ptr - sp == 1 ? stack_pop().i : -1;
	stack_ptr = sp;
	return slot;
}

static void av_constant(void)
{
	gi_startup();
	int black = gi_gslot(gi_global("CASColor::BLACK"));
	AV_CHECK(av_is_color(black, 0, 0, 0, 255), "CASColor::BLACK is not (0,0,0,255) after the start-up");
	if (av_failures)
		return;
	int ref = heap[black].ref;
	int got = av_get("PlayerFrameView@GetAddColor", 6);	/* Waiting: return CASColor::BLACK */
	AV_CHECK(got != black, "GetAddColor returned the page of the global CASColor::BLACK (slot %d)", black);
	AV_CHECK(av_is_color(got, 0, 0, 0, 255), "the result is not a CASColor (0,0,0,255)");
	AV_CHECK(heap[black].ref == ref, "the global has %d reference(s) while the result lives, had %d", heap[black].ref, ref);
	if (got > 0 && da_live_page(got)) {
		/* What a caller does with its copy (a focus colour written into it). */
		heap[got].page->values[0].i = 219;
		heap[got].page->values[1].i = 136;
		AV_CHECK(av_is_color(black, 0, 0, 0, 255), "writing to the result changed CASColor::BLACK to (%d,%d,%d)",
			 heap[black].page->values[0].i, heap[black].page->values[1].i, heap[black].page->values[2].i);
		heap_unref(got);
	}
	AV_CHECK(da_live_page(black) && heap[black].ref == ref, "after releasing the result the global has %d reference(s), had %d",
		 da_live_page(black) ? heap[black].ref : 0, ref);
	AV_CHECK(av_is_color(black, 0, 0, 0, 255), "CASColor::BLACK changed");
	printf("AV1 checked\n");
}

static void av_rounds(void)
{
	gi_startup();
	int black = gi_gslot(gi_global("CASColor::BLACK")), white = gi_gslot(gi_global("CASColor::WHITE"));
	AV_CHECK(av_is_color(black, 0, 0, 0, 255) && av_is_color(white, 255, 255, 255, 255), "the constants after the start-up");
	if (av_failures)
		return;
	int bref = heap[black].ref, wref = heap[white].ref;
	/* One warm-up round, then the slot count must stay. */
	int w = av_get("PlayerFrameView@GetMulColor", 0);
	if (w > 0 && w != white && w != black)
		heap_unref(w);
	size_t live = live_slots();
	int shared = 0, wrong = 0;
	for (int i = 0; i < 500; i++) {
		int focus = i % 7;
		int add = av_get("PlayerFrameView@GetAddColor", focus);
		int mul = av_get("PlayerFrameView@GetMulColor", focus);
		shared += add == black || add == white || mul == black || mul == white || add == mul;
		/* Exclude (3): add (255,0,186), multiply BLACK; Waiting (6):
		 * multiply (200,200,200); otherwise add BLACK, multiply WHITE. */
		wrong += !(focus == 3 ? av_is_color(add, 255, 0, 186, 255) : av_is_color(add, 0, 0, 0, 255));
		wrong += !(focus == 3 ? av_is_color(mul, 0, 0, 0, 255)
			   : focus == 6 ? av_is_color(mul, 200, 200, 200, 255) : av_is_color(mul, 255, 255, 255, 255));
		/* The caller scribbles on both and lets them go. */
		for (int k = 0; k < 2; k++) {
			int s = k ? mul : add;
			if (s > 0 && s != black && s != white && da_live_page(s)) {
				heap[s].page->values[k].i = 77 + i % 100;
				heap_unref(s);
			}
		}
	}
	AV_CHECK(!shared, "%d result(s) of 1000 were the page of a constant or of each other", shared);
	AV_CHECK(!wrong, "%d result(s) of 1000 did not hold the colour the function returns", wrong);
	AV_CHECK(av_is_color(black, 0, 0, 0, 255), "CASColor::BLACK is (%d,%d,%d) after the rounds",
		 heap[black].page->values[0].i, heap[black].page->values[1].i, heap[black].page->values[2].i);
	AV_CHECK(av_is_color(white, 255, 255, 255, 255), "CASColor::WHITE is (%d,%d,%d) after the rounds",
		 heap[white].page->values[0].i, heap[white].page->values[1].i, heap[white].page->values[2].i);
	AV_CHECK(heap[black].ref == bref && heap[white].ref == wref, "reference counts BLACK %d (was %d), WHITE %d (was %d)",
		 heap[black].ref, bref, heap[white].ref, wref);
	AV_CHECK(live_slots() == live, "live slots %zu after the rounds, %zu before", live_slots(), live);
	printf("AV2 checked\n");
}

static int av_run(const char *name, void (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		av_failures = 0;
		alarm(120);
		fn();
		fflush(stdout);
		_exit(av_failures ? 1 : 0);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	printf("%s %s%s\n", name, ok ? "PASS" : "FAIL", WIFSIGNALED(status) ? " (crashed)" : "");
	fflush(stdout);
	return ok ? 0 : 1;
}

static int test_aref_value(void)
{
	int failed = 0;
	failed += av_run("AV1 a getter returning CASColor::BLACK gives a copy", av_constant);
	failed += av_run("AV2 500 rounds leave the colour constants alone", av_rounds);
	printf("AREF-VALUE %s: %d of 2 cases failed\n", failed ? "FAIL" : "PASS", failed);
	return failed ? 93 : 0;
}
