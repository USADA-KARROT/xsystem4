/* A case runner shared by save-text-lines, movie-audio-length, parts-leaks
 * and math-inverse-trig: every case runs in its own fork, so that one case's
 * state (the parts table, ain->version, the character rule, the audio
 * device) never reaches the next one.
 *
 * The child writes "r" to a pipe when the case's setup is done (xc_ready:
 * the GL context, the fixture files, the audio stream), and "d" when the
 * case body returned; then it exits 0 (passed) or 1 (failed). How a child
 * that did not return is counted:
 *   - it exited with XC_ENV_EXIT (xc_env_fail: its environment could not be
 *     set up), or it ended in any way before "r" (the engine's ERROR for a
 *     shader exits with 1): an environment failure, not a failed case;
 *   - it ended after "r" without returning: the case failed. This is what
 *     the code under test does when it crashes or stops the case: a
 *     sanitizer report (with abort_on_error=1 and halt_on_error=1 it ends
 *     the child; ASan's ends it with SIGABRT here), another signal, the
 *     alarm (the case ran out of time), or the engine's ERROR / VM_ERROR
 *     (exit 1). It is printed as
 *     "  <id> CRASHED: ..." before the case's FAIL line.
 * A case that checked nothing fails.
 *
 * Output: "<id> PASS (status S)" or "<id> FAIL (status S)" per case (S is
 * the raw wait status: 0 or 256 for a case that returned, the signal or
 * the exit status of one that crashed), and last
 * "<LABEL> PASS|FAIL: <failed> of <n> cases failed; environment <env>". The
 * mode returns 0, 1, or 2 when a case could not run.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sanitizer/allocator_interface.h>

#define XC_ENV_EXIT 3
static int xc_failures, xc_checks;
#define XC_CHECK(c, ...) do { xc_checks++; if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); xc_failures++; } } while (0)

static int xc_fd = -1;
static bool xc_is_ready;

/* The case's setup is done: from here on, a child that ends without
 * returning is a failed case (see above). */
static void xc_ready(void)
{
	if (xc_is_ready)
		return;
	fflush(NULL);
	if (xc_fd < 0 || write(xc_fd, "r", 1) != 1)
		_exit(XC_ENV_EXIT);
	xc_is_ready = true;
}

static void xc_env_fail(const char *what)
{
	printf("  ENVIRONMENT: %s\n", what);
	fflush(stdout);
	_exit(XC_ENV_EXIT);
}

struct xc_case {
	const char *id;
	void (*fn)(void);
};

/*
 * Leak detection (save-text-lines, parts-leaks). LeakSanitizer is not
 * available with this ASan (detect_leaks=1 aborts: "not supported on this
 * platform") and `leaks` cannot read an ASan process, so the cases count
 * instead, with the ASan allocator's malloc and free hooks
 * (sanitizer/allocator_interface.h):
 *   - xc_watch(p): whether the block p is freed from now on (only the
 *     address is compared; the block is never read after it is freed);
 *   - xc_track_begin() / xc_track_end(): the blocks allocated in between
 *     and not freed since are kept in a fixed table (a hook allocates
 *     nothing). Of those still allocated (read only while
 *     __sanitizer_get_ownership says the block is allocated):
 *     xc_track_strings(text) counts each struct string holding text,
 *     xc_track_any_strings() each block that has the shape of a struct
 *     string (a reference count, a size that fits the block, the text that
 *     long and ending in 0), xc_track_cgs(w, h) each struct cg of w x h
 *     pixels, and xc_track_pixels(cg) each block of the size of cg's pixels
 *     and the same bytes (a CG's pixels left when only its struct is freed).
 * Every window checks itself: xc_track_begin allocates a canary block and
 * xc_track_end checks that the hook recorded it and that freeing it took
 * it out of the table; otherwise (and when more blocks were allocated than
 * the table holds) the case reports an environment failure, so that hooks
 * that do not run cannot make a count of 0. The hooks take no lock: a
 * block another thread allocates at the same moment may be missed, which
 * can only make a count lower.
 */
#define XC_TRACK_MAX 65536
static const volatile void *xc_track_blocks[XC_TRACK_MAX];
static volatile size_t xc_track_n;
static volatile bool xc_tracking, xc_track_overflow;
static const volatile void *xc_watched;
static volatile bool xc_watched_freed;
static void *xc_canary;

static void xc_malloc_hook(const volatile void *p, size_t size)
{
	(void)size;
	if (!xc_tracking || !p)
		return;
	if (xc_track_n >= XC_TRACK_MAX) {
		xc_track_overflow = true;
		return;
	}
	xc_track_blocks[xc_track_n++] = p;
}

static void xc_free_hook(const volatile void *p)
{
	if (!p)
		return;
	if (p == xc_watched)
		xc_watched_freed = true;
	for (size_t i = xc_track_n; i-- > 0;) {
		if (xc_track_blocks[i] == p) {
			xc_track_blocks[i] = NULL;
			break;
		}
	}
}

static void xc_hooks(void)
{
	static bool installed;
	if (!installed && !__sanitizer_install_malloc_and_free_hooks(xc_malloc_hook, xc_free_hook))
		xc_env_fail("cannot install the ASan allocator hooks");
	installed = true;
}

static void xc_watch(const void *p)
{
	xc_hooks();
	xc_watched_freed = false;
	xc_watched = p;
}

static bool xc_tracked(const void *p)
{
	for (size_t i = 0; i < xc_track_n; i++)
		if (xc_track_blocks[i] == p)
			return true;
	return false;
}

static void xc_track_begin(void)
{
	xc_hooks();
	xc_tracking = false;
	for (size_t i = 0; i < xc_track_n; i++)
		xc_track_blocks[i] = NULL;
	xc_track_n = 0;
	xc_track_overflow = false;
	xc_tracking = true;
	xc_canary = malloc(77);
	if (!xc_canary)
		xc_env_fail("cannot allocate the tracking canary");
	memset(xc_canary, 0xc5, 77);
}

static void xc_track_end(void)
{
	xc_tracking = false;
	if (xc_track_overflow)
		xc_env_fail("more blocks were allocated than the tracking table holds");
	if (!xc_tracked(xc_canary))
		xc_env_fail("the malloc hook did not record the tracking canary");
	free(xc_canary);
	if (xc_tracked(xc_canary))
		xc_env_fail("the free hook did not take the tracking canary out of the table");
	xc_canary = NULL;
}

/* A tracked block still allocated and at least `size` bytes long. */
static const void *xc_tracked_live(size_t i, size_t size)
{
	const void *p = (const void *)xc_track_blocks[i];
	if (!p || !__sanitizer_get_ownership(p) || __sanitizer_get_allocated_size(p) < size)
		return NULL;
	return p;
}

static int xc_track_strings(const char *text)
{
	const size_t len = strlen(text);
	int n = 0;
	for (size_t i = 0; i < xc_track_n; i++) {
		const struct string *s = xc_tracked_live(i, sizeof(struct string) + len + 1);
		if (s && s->size == (int)len && s->ref > 0 && !memcmp(s->text, text, len + 1))
			n++;
	}
	return n;
}

static int xc_track_cgs(int w, int h)
{
	int n = 0;
	for (size_t i = 0; i < xc_track_n; i++) {
		const struct cg *cg = xc_tracked_live(i, sizeof(struct cg));
		if (cg && __sanitizer_get_allocated_size(cg) == sizeof(struct cg) && cg->metrics.w == w
		    && cg->metrics.h == h && cg->pixels && __sanitizer_get_ownership(cg->pixels))
			n++;
	}
	return n;
}

static int xc_track_any_strings(void)
{
	int n = 0;
	for (size_t i = 0; i < xc_track_n; i++) {
		const struct string *s = xc_tracked_live(i, sizeof(struct string) + 1);
		if (!s)
			continue;
		const size_t size = __sanitizer_get_allocated_size(s);
		if (s->ref > 0 && s->size >= 0 && sizeof(struct string) + (size_t)s->size + 1 <= size
		    && s->text[s->size] == 0 && strlen(s->text) == (size_t)s->size) {
			char hex[2 * 12 + 1] = "";
			for (int k = 0; k < s->size && k < 12; k++)
				snprintf(hex + 2 * k, 3, "%02x", (uint8_t)s->text[k]);
			printf("  a string of %d byte(s) is still allocated: %s%s\n", s->size, hex, s->size > 12 ? ".." : "");
			n++;
		}
	}
	return n;
}

static int xc_track_pixels(const struct cg *ref)
{
	const size_t size = __sanitizer_get_allocated_size(ref->pixels);
	int n = 0;
	for (size_t i = 0; i < xc_track_n; i++) {
		const void *p = xc_tracked_live(i, size);
		if (p && __sanitizer_get_allocated_size(p) == size && !memcmp(p, ref->pixels, size))
			n++;
	}
	return n;
}

static int xc_run(const char *label, const struct xc_case *cases, int n, unsigned seconds)
{
	int failed = 0, env = 0;
	for (int i = 0; i < n; i++) {
		int fd[2];
		if (pipe(fd)) {
			printf("%s ENVIRONMENT FAILURE (pipe)\n", cases[i].id);
			env++;
			continue;
		}
		fflush(NULL);
		pid_t pid = fork();
		if (pid == 0) {
			close(fd[0]);
			xc_fd = fd[1];
			xc_is_ready = false;
			alarm(seconds);
			xc_failures = xc_checks = 0;
			cases[i].fn();
			printf("  %s checks=%d failures=%d\n", cases[i].id, xc_checks, xc_failures);
			fflush(NULL);
			if (!xc_is_ready) {
				printf("  ENVIRONMENT: %s never reported its setup done\n", cases[i].id);
				fflush(NULL);
				_exit(XC_ENV_EXIT);
			}
			if (write(fd[1], "d", 1) != 1)
				_exit(XC_ENV_EXIT);
			_exit(xc_failures || !xc_checks ? 1 : 0);
		}
		close(fd[1]);
		int st = 0;
		pid_t w = -1;
		if (pid > 0) {
			do { w = waitpid(pid, &st, 0); } while (w < 0 && errno == EINTR);
		}
		/* The child is gone; what it wrote is in the pipe (never wait for
		 * more: nothing else holds the other end, but do not depend on it). */
		char got[8];
		ssize_t nr = -1;
		if (fcntl(fd[0], F_SETFL, O_NONBLOCK) == 0)
			nr = read(fd[0], got, sizeof got);
		close(fd[0]);
		bool ready = false, done = false;
		for (ssize_t k = 0; k < nr; k++) {
			ready |= got[k] == 'r';
			done |= got[k] == 'd';
		}
		const bool reaped = pid > 0 && w == pid;
		const bool exited = reaped && WIFEXITED(st);
		const int code = exited ? WEXITSTATUS(st) : -1;
		if (reaped && ready && done && exited && (code == 0 || code == 1)) {
			printf("%s %s (status %d)\n", cases[i].id, code ? "FAIL" : "PASS", st);
			failed += code != 0;
			continue;
		}
		if (reaped && ready && !done && !(exited && code == XC_ENV_EXIT)) {
			if (WIFSIGNALED(st))
				printf("  %s CRASHED: signal %d%s\n", cases[i].id, WTERMSIG(st),
				       WTERMSIG(st) == SIGALRM ? " (the alarm: the case ran out of time)"
				       : WTERMSIG(st) == SIGABRT ? " (abort: a sanitizer report or an assertion)" : "");
			else
				printf("  %s CRASHED: exit %d without returning (the engine's ERROR or VM_ERROR)\n",
				       cases[i].id, code);
			printf("%s FAIL (status %d)\n", cases[i].id, st);
			failed++;
			continue;
		}
		printf("%s ENVIRONMENT FAILURE (status %d%s)\n", cases[i].id, st,
		       !reaped ? ", the child was not reaped" : !ready ? ", before the case's setup was done"
		       : done ? ", an unexpected exit status" : ", the case reported its environment");
		env++;
	}
	printf("%s %s: %d of %d cases failed; environment %d\n", label, failed || env ? "FAIL" : "PASS",
	       failed, n, env);
	fflush(stdout);
	return env ? 2 : failed ? 1 : 0;
}
