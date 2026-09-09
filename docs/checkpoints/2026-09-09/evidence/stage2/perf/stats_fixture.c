
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <SDL.h>
#define max(a,b) ((a)>(b)?(a):(b))
static char line[2048];
static unsigned reports,frequency_calls,counter_calls,flag_calls;
static uint64_t now;
static void notice(const char *fmt,...) {va_list ap;va_start(ap,fmt);vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);reports++;}
#define NOTICE(...) notice(__VA_ARGS__)
struct { SDL_Window *window; } sdl;
Uint64 SDL_GetPerformanceFrequency(void) { frequency_calls++;return 1000000; }
Uint64 SDL_GetPerformanceCounter(void) { counter_calls++;return now; }
Uint64 SDL_GetTicks64(void) {return now/1000;}
Uint32 SDL_GetWindowFlags(SDL_Window *window) {(void)window;flag_calls++;return SDL_WINDOW_SHOWN;}
int SDL_GL_GetSwapInterval(void) {return 0;}
/* Opt-in wall-time telemetry. A sample is recorded only after SwapWindow
 * returns, never for an update/render call that did not present. */
#define STAGE2_PERF_SAMPLES 4096
static struct {
	bool initialized, enabled;
	uint64_t frequency, previous_present, window_start;
	uint64_t frames, total_swap, max_interval, max_swap;
	unsigned samples;
	uint64_t intervals[STAGE2_PERF_SAMPLES], swaps[STAGE2_PERF_SAMPLES];
} stage2_perf;

static uint64_t gfx_stage2_perf_begin_swap(void)
{
	if (!stage2_perf.initialized) {
		stage2_perf.initialized = true;
		const char *value = getenv("XSYS4_STAGE2_PERF");
		stage2_perf.enabled = value && *value && strcmp(value, "0");
		if (stage2_perf.enabled) {
			stage2_perf.frequency = SDL_GetPerformanceFrequency();
			if (!stage2_perf.frequency)
				stage2_perf.enabled = false;
		}
	}
	return stage2_perf.enabled ? SDL_GetPerformanceCounter() : 0;
}

static int gfx_stage2_perf_compare(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return (x > y) - (x < y);
}

static void gfx_stage2_perf_presented(uint64_t swap_start, uint64_t presented)
{
	if (!stage2_perf.previous_present) {
		stage2_perf.previous_present = stage2_perf.window_start = presented;
		return;
	}
	uint64_t interval = presented - stage2_perf.previous_present;
	uint64_t swap = presented - swap_start;
	stage2_perf.previous_present = presented;
	stage2_perf.frames++;
	stage2_perf.total_swap += swap;
	stage2_perf.max_interval = max(stage2_perf.max_interval, interval);
	stage2_perf.max_swap = max(stage2_perf.max_swap, swap);
	if (stage2_perf.samples < STAGE2_PERF_SAMPLES) {
		stage2_perf.intervals[stage2_perf.samples] = interval;
		stage2_perf.swaps[stage2_perf.samples++] = swap;
	}
	uint64_t elapsed = presented - stage2_perf.window_start;
	if ((double)elapsed / stage2_perf.frequency < 5.0)
		return;

	qsort(stage2_perf.intervals, stage2_perf.samples, sizeof(uint64_t), gfx_stage2_perf_compare);
	qsort(stage2_perf.swaps, stage2_perf.samples, sizeof(uint64_t), gfx_stage2_perf_compare);
	// Nearest-rank p95; expose sample count if an unusually fast caller caps it.
	unsigned p95 = (stage2_perf.samples * 95 + 99) / 100 - 1;
	double ms_per_tick = 1000.0 / stage2_perf.frequency;
	NOTICE("STAGE2_PERF t_ms=%llu window_ms=%.3f presents=%llu samples=%u avg_fps=%.3f p95_interval_ms=%.3f max_interval_ms=%.3f avg_swap_ms=%.3f p95_swap_ms=%.3f max_swap_ms=%.3f window_flags=0x%x vsync=%d",
		(unsigned long long)SDL_GetTicks64(), elapsed * ms_per_tick,
		(unsigned long long)stage2_perf.frames, stage2_perf.samples,
		stage2_perf.frames * (double)stage2_perf.frequency / elapsed,
		stage2_perf.intervals[p95] * ms_per_tick, stage2_perf.max_interval * ms_per_tick,
		stage2_perf.total_swap * ms_per_tick / stage2_perf.frames,
		stage2_perf.swaps[p95] * ms_per_tick, stage2_perf.max_swap * ms_per_tick,
		SDL_GetWindowFlags(sdl.window), SDL_GL_GetSwapInterval());
	stage2_perf.window_start = presented;
	stage2_perf.frames = stage2_perf.total_swap = 0;
	stage2_perf.max_interval = stage2_perf.max_swap = 0;
	stage2_perf.samples = 0;
}

static double field(const char *name) {const char *s=strstr(line,name);assert(s);return strtod(s+strlen(name),NULL);}
static void near(const char *name,double expected,double epsilon) {assert(fabs(field(name)-expected)<=epsilon);}
static void present_after(uint64_t interval,uint64_t swap) {
 now+=interval-swap;uint64_t begin=gfx_stage2_perf_begin_swap();now+=swap;
 if(stage2_perf.enabled)gfx_stage2_perf_presented(begin,SDL_GetPerformanceCounter());
}
int main(void) {
 unsetenv("XSYS4_STAGE2_PERF");for(int i=0;i<20;i++)assert(gfx_stage2_perf_begin_swap()==0);
 assert(!frequency_calls&&!counter_calls&&!reports);
 memset(&stage2_perf,0,sizeof(stage2_perf));setenv("XSYS4_STAGE2_PERF","0",1);
 assert(gfx_stage2_perf_begin_swap()==0&&!frequency_calls&&!counter_calls&&!reports);
 memset(&stage2_perf,0,sizeof(stage2_perf));setenv("XSYS4_STAGE2_PERF","1",1);
 now=1000000;present_after(16667,2000);assert(!reports&&stage2_perf.frames==0);
 for(int i=0;i<299;i++)present_after(16667,2000);assert(!reports);
 present_after(16667,2000);assert(reports==1&&flag_calls==1&&frequency_calls==1);
 near("window_ms=",5000.1,.001);near("presents=",300,0);near("samples=",300,0);
 near("avg_fps=",60,.002);near("p95_interval_ms=",16.667,.001);near("max_interval_ms=",16.667,.001);
 near("avg_swap_ms=",2,.001);near("p95_swap_ms=",2,.001);near("max_swap_ms=",2,.001);
 for(int i=0;i<99;i++)present_after(50000,2000);assert(reports==1);
 present_after(100000,5000);assert(reports==2);near("presents=",100,0);
 near("avg_fps=",100/5.05,.001);near("p95_interval_ms=",50,.001);near("max_interval_ms=",100,.001);
 near("avg_swap_ms=",2.03,.001);near("p95_swap_ms=",2,.001);near("max_swap_ms=",5,.001);
 present_after(6000000,1000);assert(reports==3);
 near("presents=",1,0);near("p95_interval_ms=",6000,.001);near("max_interval_ms=",6000,.001);
 near("avg_fps=",1/6.,.001);near("avg_swap_ms=",1,.001);
 for(int i=0;i<5000;i++)present_after(1000,100);
 assert(reports==4);near("presents=",5000,0);near("samples=",4096,0);near("avg_fps=",1000,.001);
 near("p95_interval_ms=",1,.001);near("max_interval_ms=",1,.001);
 puts("production PERF helper: disabled=0 I/O/timer, 60fps, p95/outlier, six-second stall, per-window reset and sample-cap accounting PASS");
 return 0;
}
