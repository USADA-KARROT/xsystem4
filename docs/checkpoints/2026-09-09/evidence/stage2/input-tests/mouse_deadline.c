/* Production mouse handling with deterministic SDL time/events; no SDL init/UI. */
#include <stdio.h>
#include <string.h>
#include INPUT_SOURCE

struct sdl_private sdl;
static uint32_t ticks;
static int trace_reads, warnings, checks, failures;

Uint32 SDL_GetTicks(void) { return ticks; }
void SDL_GetWindowSize(SDL_Window *w, int *x, int *y)
{ (void)w; *x=800; *y=600; trace_reads++; }
void SDL_GL_GetDrawableSize(SDL_Window *w, int *x, int *y)
{ (void)w; *x=800; *y=600; trace_reads++; }
Uint32 SDL_GetMouseState(int *x, int *y)
{ *x=17; *y=19; trace_reads++; return 0; }
void SDL_PumpEvents(void) {}
void SDL_WarpMouseInWindow(SDL_Window *w, int x, int y) { (void)w; (void)x; (void)y; }
void sys_warning(const char *format, ...) { (void)format; warnings++; }

static void check(bool ok, const char *label)
{
	checks++;
	if (!ok) { failures++; fprintf(stderr,"FAIL: %s\n",label); }
}

static void button(int which, int state, uint32_t time)
{
	ticks=time;
	SDL_MouseButtonEvent event = {.button=which, .state=state};
	mouse_event(&event);
}

static void pump_release(uint32_t time)
{
	ticks=time;
	release_pending_mouse_buttons(time);
}

int main(void)
{
	unsetenv("XSYS4_STAGE2_TRACE");
	for (int b=1; b<=3; b++) {
		enum sact_keycode code=sdl_to_sact_button(b);
		button(b,SDL_PRESSED,100); button(b,SDL_RELEASED,100);
		check(key_is_down(code),"same-pump DOWN+UP remains observable");
		pump_release(149); check(key_is_down(code),"short press stays down before deadline");
		pump_release(150); check(!key_is_down(code),"pending UP releases at deadline");
		button(b,SDL_PRESSED,200);
		pump_release(251); check(key_is_down(code),"real held button survives 50ms");
		pump_release(450); check(key_is_down(code),"real held button survives 250ms");
		button(b,SDL_RELEASED,450); check(!key_is_down(code),"late actual UP releases immediately");
		button(b,SDL_PRESSED,500); button(b,SDL_RELEASED,510);
		button(b,SDL_PRESSED,530); pump_release(550);
		check(key_is_down(code),"new DOWN cancels earlier pending release");
		pump_release(600); check(key_is_down(code),"repeated DOWN remains held until new UP");
		button(b,SDL_RELEASED,600); check(!key_is_down(code),"new UP completes repeated DOWN");
		button(b,SDL_PRESSED,UINT32_MAX-24); button(b,SDL_RELEASED,UINT32_MAX-20);
		check(key_is_down(code),"early UP across ticks wrap remains pending");
		pump_release(24); check(key_is_down(code),"wrapped deadline has not passed");
		pump_release(25); check(!key_is_down(code),"wrapped deadline releases pending UP");
		button(b,SDL_PRESSED,UINT32_MAX-49); button(b,SDL_RELEASED,UINT32_MAX-40);
		check(key_is_down(code),"deadline exactly zero remains a valid future deadline");
		pump_release(0); check(!key_is_down(code),"zero deadline releases pending UP at wrap");
		button(b,SDL_RELEASED,UINT32_MAX-10);
		check(!key_is_down(code),"UP without a preceding DOWN never creates a press");
	}
	bool keys_before[VK_NR_KEYCODES];
	uint32_t deadlines_before[8];
	memcpy(keys_before,key_state,sizeof keys_before);
	memcpy(deadlines_before,mouse_hold_until,sizeof deadlines_before);
	int unsupported[]={0,4,7,8,255};
	for (unsigned i=0;i<sizeof unsupported/sizeof *unsupported;i++) {
		button(unsupported[i],SDL_PRESSED,1000); button(unsupported[i],SDL_RELEASED,1001);
		check(!memcmp(keys_before,key_state,sizeof keys_before)
			&& !memcmp(deadlines_before,mouse_hold_until,sizeof deadlines_before),
			"unsupported button does not change key/deadline storage");
	}
	setenv("XSYS4_STAGE2_TRACE","0",1);
	button(SDL_BUTTON_LEFT,SDL_PRESSED,1100); button(SDL_BUTTON_LEFT,SDL_RELEASED,1101);
	check(trace_reads==0 && warnings==0,"trace=0 performs no trace reads or logging");
	pump_release(1150);
	setenv("XSYS4_STAGE2_TRACE","",1);
	button(SDL_BUTTON_LEFT,SDL_PRESSED,1200); button(SDL_BUTTON_LEFT,SDL_RELEASED,1201);
	check(trace_reads==0 && warnings==0,"empty trace setting is disabled");
	pump_release(1250);
	trace_reads=warnings=0;
	setenv("XSYS4_STAGE2_TRACE","1",1);
	for (int i=0;i<50;i++) button(0,SDL_PRESSED,1300+i);
	check(trace_reads==120 && warnings==40,"trace retains a forty-event cap");
	unsetenv("XSYS4_STAGE2_TRACE");
	sdl.w=800; sdl.h=600; sdl.viewport=(SDL_Rect){0,0,800,600};
	int x,y;
	mouse_get_pos(&x,&y); check(x==17 && y==19,"normal coordinate polling is unchanged");
	mouse_set_pos(85,82); mouse_get_pos(&x,&y);
	check(x==85 && y==82,"existing explicit coordinate override is unchanged");
	pump_release(2000); mouse_get_pos(&x,&y);
	check(x==85 && y==82,"deadline handling does not clear coordinate override");
	printf("%d checks, %d failures\n",checks,failures);
	return failures ? 1 : 0;
}
