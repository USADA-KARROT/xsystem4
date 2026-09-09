from pathlib import Path
import subprocess,json,hashlib
here=Path(__file__).resolve().parent
source=here.parent/'source/src/video.c'
s=source.read_text();a=s.index('/* Opt-in wall-time telemetry.');b=s.index('\nvoid gfx_swap(void)',a);block=s[a:b]
prefix=r'''
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
'''
tests=r'''
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
'''
fixture=here/'stats_fixture.c';fixture.write_text(prefix+block+tests)
cmd=['cc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk','-I/opt/homebrew/include/SDL2',str(fixture),'-o',str(here/'stats_fixture')]
p=subprocess.run(cmd,text=True,capture_output=True);r={'production_block_sha256':hashlib.sha256(block.encode()).hexdigest(),'compile_command':cmd,'compile_exit':p.returncode,'compile_stderr':p.stderr}
if p.returncode==0:
 q=subprocess.run([str(here/'stats_fixture')],text=True,capture_output=True);r.update(test_exit=q.returncode,stdout=q.stdout,stderr=q.stderr)
 print(q.stdout+q.stderr,end='')
else:print(p.stderr,end='')
(here/'stats-result.json').write_text(json.dumps(r,indent=2)+'\n')
raise SystemExit(r.get('test_exit',p.returncode))
