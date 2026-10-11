/* Native message fade: 0x5956b0/0x5956f0, 0x5bd205, 0x5bc859.
 * Independent clock samples from the native 200 ms ramp; actual HLL calls,
 * actual script GetMessageSpeedRate, actual glyph layout. No game assets.
 * DF14 is the left button of a wait for a message (0x547c90) under the
 * script's own CASClick, DF15 the pactex keys, DF16 the two times of
 * UpdateComponent. */
static int df_failures;
#define DF_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); puts(""); df_failures++; } } while (0)
static void df_setup(float setting)
{
	cn_gl_setup();
	lr_setup();
	int g=-1;
	for (int i=0;i<ain->nr_globals;i++) if (!strcmp(ain->globals[i].name,"g_AFConfig")) g=i;
	if(g<0) _exit(CN_ENV_EXIT);
	int s=alloc_struct(ain->globals[g].type.struc);
	heap[global_page_slot].page->values[g].i=s;
	heap[s].page->values[1].f=setting;
}
static void df_text(int no,const char *s,int msg)
{
	BU_PE("SetMessageWindowText",BU_I(no),BU_I(sf_str(s,-1)),BU_I(msg),BU_I(sf_str("",-1)),BU_I(0),BU_I(0));
}
static int df_fixed(int no) { return BU_PE("IsFixedMessageWindowText",BU_I(no)).i; }
static struct parts_text *df_layout(int no)
{
	parts_message_window_update(0);
	Point p;
	return parts_message_window_render_text(parts_get(no),&p);
}
/* A function of the fix that the engine under test does not have: the case
 * ends here and is reported apart from a failed assertion. */
enum { DF_NO_SYMBOL = 3 };
static void df_no_symbol(const char *name)
{
	printf("  NO SYMBOL: %s\n", name);
	fflush(NULL);
	_exit(DF_NO_SYMBOL);
}
static float df_alpha(int no,struct parts_text *t,int i)
{
	float (*fn)(struct parts *,struct parts_text *,int)=dlsym(RTLD_DEFAULT,"parts_message_window_glyph_alpha");
	if (!fn) df_no_symbol("parts_message_window_glyph_alpha");
	return fn(parts_get(no),t,i);
}
static void df_near(float got,float want) { DF_CHECK(fabsf(got-want)<0.001f,"alpha %.4f expected %.4f",got,want); }
static int df_speed(int no) { return BU_PE("GetMessageWindowTextSpeed",BU_I(no)).i; }
static bool df_window(int no)
{
	struct parts *parts = parts_try_get(no);
	return parts && parts->message;
}
/* The four calls take the window object of 0x53e010 as the other message
 * window calls do: a number above 0 gets its parts and its window made
 * (0x53d920, 0x536c20), with a text speed of 23 and not fixed before its
 * first update (0x4ee657, 0x4ee71e); 0 and below have no object. */
static void df_missing(void)
{
	df_setup(.5f);
	DF_CHECK(!df_fixed(9810), "a window the query made is fixed before its first update");
	DF_CHECK(df_window(9810), "IsFixedMessageWindowText made no window");
	parts_message_window_update(0);
	DF_CHECK(df_fixed(9810), "a window without text is not fixed after an update");
	DF_CHECK(df_speed(9811) == 23 && df_window(9811), "GetMessageWindowTextSpeed of a new number: %d, window %d; want 23 and a window",
		 df_speed(9811), df_window(9811));
	BU_PE("SetMessageWindowTextSpeed", BU_I(9812), BU_I(77));
	DF_CHECK(df_window(9812) && df_speed(9812) == 77, "SetMessageWindowTextSpeed of a new number: window %d, speed %d; want a window and 77",
		 df_window(9812), df_speed(9812));
	BU_PE("FixMessageWindowText", BU_I(9813));
	DF_CHECK(df_window(9813) && df_fixed(9813), "FixMessageWindowText of a new number: window %d, fixed %d; want a fixed window",
		 df_window(9813), df_fixed(9813));
	/* A parts that is no message window becomes one. */
	br_box(9814, 100, 100, 200, 80, true);
	BU_PE("SetMessageWindowTextSpeed", BU_I(9814), BU_I(50));
	DF_CHECK(df_window(9814) && df_speed(9814) == 50, "another kind of parts: window %d, speed %d; want a window and 50",
		 df_window(9814), df_speed(9814));
	static const int none[] = { 0, -5 };
	for (int i = 0; i < 2; i++) {
		BU_PE("SetMessageWindowTextSpeed", BU_I(none[i]), BU_I(77));
		BU_PE("FixMessageWindowText", BU_I(none[i]));
		DF_CHECK(!df_fixed(none[i]) && !df_speed(none[i]) && !parts_try_get(none[i]),
			 "number %d: fixed %d, speed %d, parts %d; want no object", none[i], df_fixed(none[i]), df_speed(none[i]),
			 !!parts_try_get(none[i]));
	}
}
static void df_clock(void)
{
	df_setup(.5f); df_text(9810,"AB\nC",7);
	BU_PE("SetMessageWindowTextSpeed",BU_I(9810),BU_I(100));
	DF_CHECK(BU_PE("GetMessageWindowTextSpeed",BU_I(9810)).i==100,"speed round trip");
	DF_CHECK(!df_fixed(9810),"dirty window fixed before build");
	struct parts_text *t=df_layout(9810);
	DF_CHECK(t && t->nr_lines==2,"two lines");
	if (!t) return;
	df_near(df_alpha(9810,t,0),0);
	parts_message_window_update(100);
	df_near(df_alpha(9810,t,0),.5f); df_near(df_alpha(9810,t,1),0); df_near(df_alpha(9810,t,2),0);
	parts_message_window_update(100);
	df_near(df_alpha(9810,t,0),1); df_near(df_alpha(9810,t,1),.5f); df_near(df_alpha(9810,t,2),0);
	/* The line break took no slot: C starts at 200 and is full at 400. */
	parts_message_window_update(100);
	df_near(df_alpha(9810,t,2),.5f);
	parts_message_window_update(99);
	DF_CHECK(!df_fixed(9810),"fixed 1 ms before the last character is at full alpha");
	parts_message_window_update(1);
	df_near(df_alpha(9810,t,2),1);
	DF_CHECK(df_fixed(9810),"not fixed by the update that brings every character to full alpha");
}
static void df_fix_reset(void)
{
	df_setup(.5f); df_text(9810,"AB",8);
	DF_CHECK(!df_fixed(9810),"new text fixed");
	BU_PE("FixMessageWindowText",BU_I(9810));
	DF_CHECK(df_fixed(9810),"Fix on dirty text did not complete");
	struct parts_text *t=df_layout(9810); df_near(df_alpha(9810,t,1),1);
	df_text(9810,"AB",8); DF_CHECK(df_fixed(9810),"same text and ID restarted");
	df_text(9810,"AB",9); DF_CHECK(!df_fixed(9810),"changed ID did not restart");
	t=df_layout(9810); df_near(df_alpha(9810,t,0),0);
	df_text(9810,"",10); DF_CHECK(!df_fixed(9810),"dirty empty text fixed");
	df_layout(9810); DF_CHECK(df_fixed(9810),"built empty text unfinished");
}
static void df_tags(void)
{
	df_setup(.5f); df_text(9810,"${time 0}AB${/time}C\nD",11);
	BU_PE("SetMessageWindowTextSpeed",BU_I(9810),BU_I(100));
	struct parts_text *t=df_layout(9810);
	DF_CHECK(t && t->nr_lines==2 && t->lines[0].nr_chars==3,"markup removed from glyphs");
	df_near(df_alpha(9810,t,0),1);df_near(df_alpha(9810,t,1),1);df_near(df_alpha(9810,t,2),0);
	parts_message_window_update(100);
	df_near(df_alpha(9810,t,2),.5f);df_near(df_alpha(9810,t,3),0);
	df_text(9810,"${time 100}A${time 0}B${/time}C${/time}D",12);
	t=df_layout(9810); parts_message_window_update(150);
	df_near(df_alpha(9810,t,0),.75f);df_near(df_alpha(9810,t,1),1);df_near(df_alpha(9810,t,2),.25f);
}
static void df_rate(void)
{
	df_setup(0); df_text(9810,"AB",13);
	BU_PE("SetMessageWindowTextSpeed",BU_I(9810),BU_I(25));
	struct parts_text *t=df_layout(9810);parts_message_window_update(100);
	df_near(df_alpha(9810,t,1),0); /* slowest x4, interval 100 */
	int g=-1;for(int i=0;i<ain->nr_globals;i++)if(!strcmp(ain->globals[i].name,"g_AFConfig"))g=i;
	heap[heap[global_page_slot].page->values[g].i].page->values[1].f=1;
	df_text(9810,"AB",14);t=df_layout(9810);
	df_near(df_alpha(9810,t,0),1);df_near(df_alpha(9810,t,1),1);
	DF_CHECK(df_fixed(9810),"maximum setting not instantaneous");
}
/* Render integration: filled synthetic glyphs go through the real parts
 * shader and framebuffer. No manual completion helper in this case. */
static int df_max_alpha(uint8_t *px)
{
	int a=0;for(int i=0;i<TS_VIEW_W*TS_VIEW_H;i++)a=max(a,px[i*4+3]);free(px);return a;
}
static void df_pixels(void)
{
	df_setup(.5f);ts_render_setup();
	df_text(9810,"AB",20);
	BU_PE("SetMessageWindowTextSpeed",BU_I(9810),BU_I(100));
	PE_SetMessageWindowTextFont(9810,0,20,255,255,255,0,0,0,0,0);
	PE_SetPos(9810,10,20);PE_SetAlpha(9810,128);PE_UpdateComponent(0);
	struct parts_text *t=df_layout(9810);ts_fill_glyphs(t);
	/* The no-glyph probe font may have zero advance. Space the synthetic
     * rectangles apart so the transparent second cell cannot overwrite first. */
	for (int i=0;i<t->lines[0].nr_chars;i++) t->lines[0].chars[i].advance=t->lines[0].chars[i].t.w+4;
	DF_CHECK(df_max_alpha(ts_draw(parts_get(9810)))==0,"frame zero not transparent");
	DF_CHECK(!df_fixed(9810),"frame zero completed");
	parts_message_window_update(100);
	printf("DF6 sample100 helper=%g,%g global=%d chars=%d\n",df_alpha(9810,t,0),df_alpha(9810,t,1),parts_get(9810)->global.alpha,t->lines[0].nr_chars);
	int a=df_max_alpha(ts_draw(parts_get(9810)));
	DF_CHECK(a>=62 && a<=66,"half glyph x half window alpha is %d, expected ~64",a);
	DF_CHECK(!df_fixed(9810),"half frame completed");
	parts_message_window_update(200);
	a=df_max_alpha(ts_draw(parts_get(9810)));
	DF_CHECK(a>=126 && a<=130,"full glyph x half window alpha is %d, expected ~128",a);
	DF_CHECK(df_fixed(9810),"renderer failed to finish text");
}
static void df_render_before_update(void)
{
	df_setup(0);df_text(9810,"AB",21);
	BU_PE("SetMessageWindowTextSpeed",BU_I(9810),BU_I(25));
	Point pos;struct parts_text *t=parts_message_window_render_text(parts_get(9810),&pos);
	DF_CHECK(!df_fixed(9810),"geometry query cleared pending speed");
	parts_message_window_update(100);
	t=parts_message_window_render_text(parts_get(9810),&pos);
	df_near(df_alpha(9810,t,1),0); /* actual script rate 4, not fallback 1 */
}
static void df_v13(void)
{
	df_setup(.5f);ain->version=13;
	struct string *s=cstr_to_string("${time 100}AB${/time}");
	PE_SetMessageWindowText(9810,s,22,NULL,0,0);free_string(s);
	Point p;struct parts_text *t=parts_message_window_render_text(parts_get(9810),&p);
	df_near(df_alpha(9810,t,0),1);df_near(df_alpha(9810,t,1),1);
}
/* The functions the runtime library table holds after _PostLink, which is
 * what a script call reaches: the original's declaration gets the new
 * function, another shape or an older program keeps what it had (the empty
 * function, or nothing until the later batch registers its own). */
static void df_bindings(void)
{
	df_setup(.5f);
	static const char *const names[] = {
		"SetMessageWindowTextSpeed", "GetMessageWindowTextSpeed", "FixMessageWindowText",
		"IsFixedMessageWindowText", "SetEnableSpeedUp", "IsEnableSpeedUp",
	};
	void (*reg)(void) = dlsym(RTLD_DEFAULT, "pe_v14_message_register");
	if (!reg)
		df_no_symbol("pe_v14_message_register");
	int lib = bu_lib("PartsEngine");
	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		char impl[64];
		snprintf(impl, sizeof(impl), "PE_%s", names[i]);
		void *want = dlsym(RTLD_DEFAULT, impl);
		if (!want)
			df_no_symbol(impl);
		int fn = ain_get_library_function(ain, lib, names[i]);
		DF_CHECK(fn >= 0, "%s: no declaration", names[i]);
		if (fn < 0)
			continue;
		struct ain_hll_function *f = &ain->libraries[lib].functions[fn];
		struct hll_function *bound = &libraries[lib][fn];
		DF_CHECK(bound->fun == want, "%s is not bound to %s", names[i], impl);
		enum ain_data_type ret = f->return_type.data;
		bound->fun = NULL;
		f->return_type.data = AIN_FLOAT;
		reg();
		DF_CHECK(bound->fun != want, "%s with another return type took %s", names[i], impl);
		f->return_type.data = ret;
		bound->fun = NULL;
		f->nr_arguments--;
		reg();
		DF_CHECK(bound->fun != want, "%s with another argument count took %s", names[i], impl);
		f->nr_arguments++;
		enum ain_data_type arg = f->arguments[0].type.data;
		bound->fun = NULL;
		f->arguments[0].type.data = AIN_FLOAT;
		reg();
		DF_CHECK(bound->fun != want, "%s with another argument type took %s", names[i], impl);
		f->arguments[0].type.data = arg;
		bound->fun = NULL;
		ain->version = 13;
		reg();
		DF_CHECK(bound->fun != want, "%s of a v13 program took %s", names[i], impl);
		ain->version = 14;
		bound->fun = NULL;
		reg();
		DF_CHECK(bound->fun == want, "%s was not bound again", names[i]);
	}
}
static void df_multibyte(void)
{
	df_setup(.5f);ain_is_gb18030=true;
	df_text(9810,"\xd6\xd0\xce\xc4\nA",30);
	BU_PE("SetMessageWindowTextSpeed",BU_I(9810),BU_I(100));
	struct parts_text *t=df_layout(9810);
	DF_CHECK(t && t->nr_lines==2 && t->lines[0].nr_chars==2,"GBK glyphs counted as bytes");
	parts_message_window_update(150);
	df_near(df_alpha(9810,t,0),.75f);df_near(df_alpha(9810,t,1),.25f);df_near(df_alpha(9810,t,2),0);
	/* Three characters: the last one starts at 200 and is full at 400. */
	parts_message_window_update(249);
	DF_CHECK(!df_fixed(9810),"GBK text fixed 1 ms before its third character is at full alpha");
	parts_message_window_update(1);
	DF_CHECK(df_fixed(9810),"GBK text of three characters not fixed at 400 ms (timed per byte?)");
	df_text(9810,"${time -100}A${/time}B",31);t=df_layout(9810);
	df_near(df_alpha(9810,t,0),1);df_near(df_alpha(9810,t,1),.5f);
}
/* +0x208: a property that differs makes the window not fixed until its next
 * update (0x4f25e0) or Fix (0x4f1f80); the same value again changes nothing
 * (0x595730, 0x595979, 0x5955c0). */
static void df_dirty(void)
{
	df_setup(.5f);
	df_text(9810, "AB", 40);
	BU_PE("FixMessageWindowText", BU_I(9810));
	DF_CHECK(df_fixed(9810), "fixed text is not fixed");
	BU_PE("SetKeyWaitShow", BU_I(9810), BU_I(1));
	DF_CHECK(!df_fixed(9810), "fixed although the key wait mark changed");
	parts_message_window_update(0);
	DF_CHECK(df_fixed(9810), "an update did not clear the change of the mark");
	BU_PE("SetKeyWaitShow", BU_I(9810), BU_I(1));
	DF_CHECK(df_fixed(9810), "the same mark value again made the window dirty");
	BU_PE("SetMessageWindowTextArea", BU_I(9810), BU_I(1), BU_I(2), BU_I(300), BU_I(100));
	DF_CHECK(!df_fixed(9810), "fixed although the text area changed");
	BU_PE("FixMessageWindowText", BU_I(9810));
	DF_CHECK(df_fixed(9810), "Fix did not clear the change of the text area");
	BU_PE("SetMessageWindowTextArea", BU_I(9810), BU_I(1), BU_I(2), BU_I(300), BU_I(100));
	DF_CHECK(df_fixed(9810), "the same text area again made the window dirty");
	BU_PE("SetMessageWindowTextOriginPosMode", BU_I(9810), BU_I(7));
	DF_CHECK(!df_fixed(9810), "fixed although the origin mode changed");
	parts_message_window_update(0);
	DF_CHECK(df_fixed(9810), "an update did not clear the change of the origin mode");
	BU_PE("SetMessageWindowTextSpeed", BU_I(9810), BU_I(23));
	DF_CHECK(df_fixed(9810), "the default speed set again made the window dirty");
	BU_PE("SetKeyWaitPos", BU_I(9810), BU_I(5), BU_I(6), BU_I(7));
	DF_CHECK(!df_fixed(9810), "fixed although the mark moved");
	BU_PE("FixMessageWindowText", BU_I(9810));
	DF_CHECK(df_fixed(9810), "Fix did not clear the move of the mark");
	/* The font and the spacing (0x4eeb60, 0x5959b0): values that differ make
	 * the window dirty, the same values again change nothing, and text that
	 * Fix has ended is still fixed after the update that follows. */
	PE_SetMessageWindowTextFont(9810, 0, 22, 255, 255, 255, 0, 1, 2, 3, 1);
	DF_CHECK(!df_fixed(9810), "fixed although the font changed");
	BU_PE("FixMessageWindowText", BU_I(9810));
	DF_CHECK(df_fixed(9810), "Fix did not clear the change of the font");
	PE_SetMessageWindowTextFont(9810, 0, 22, 255, 255, 255, 0, 1, 2, 3, 1);
	DF_CHECK(df_fixed(9810), "the same font again made the window dirty");
	parts_message_window_update(0);
	DF_CHECK(df_fixed(9810), "the same font again let the fixed text fade once more");
	PE_SetMessageWindowTextFont(9810, 0, 22, 255, 255, 255, 0, 1, 2, 3, 2);
	DF_CHECK(!df_fixed(9810), "fixed although the edge weight of the font changed");
	BU_PE("FixMessageWindowText", BU_I(9810));
	PE_SetMessageWindowTextSpace(9810, 2, 3);
	DF_CHECK(!df_fixed(9810), "fixed although the spacing changed");
	BU_PE("FixMessageWindowText", BU_I(9810));
	DF_CHECK(df_fixed(9810), "Fix did not clear the change of the spacing");
	PE_SetMessageWindowTextSpace(9810, 2, 3);
	DF_CHECK(df_fixed(9810), "the same spacing again made the window dirty");
	parts_message_window_update(0);
	DF_CHECK(df_fixed(9810), "the same spacing again let the fixed text fade once more");
}

/* The window's text runs on the time its parts selects (0x5352b1): the
 * scaled passed time with speed-up enabled, as a new parts is (0x550a9c),
 * else the passed time. */
static void df_scaled_time(void)
{
	df_setup(.5f);
	void (*update)(int, int) = dlsym(RTLD_DEFAULT, "parts_message_window_update_times");
	if (!update)
		df_no_symbol("parts_message_window_update_times");
	df_text(9810, "AB", 41);
	BU_PE("SetMessageWindowTextSpeed", BU_I(9810), BU_I(100));
	DF_CHECK(BU_PE("IsEnableSpeedUp", BU_I(9810)).i == 1, "a new parts does not start with speed-up enabled");
	struct parts_text *t = df_layout(9810);
	update(20, 100);
	df_near(df_alpha(9810, t, 0), .5f);
	df_near(df_alpha(9810, t, 1), 0);
	BU_PE("SetEnableSpeedUp", BU_I(9810), BU_I(0));
	DF_CHECK(BU_PE("IsEnableSpeedUp", BU_I(9810)).i == 0, "speed-up still enabled");
	update(20, 100);
	df_near(df_alpha(9810, t, 0), .6f);
	df_near(df_alpha(9810, t, 1), .1f);
	BU_PE("SetEnableSpeedUp", BU_I(9810), BU_I(1));
	update(20, 100);
	df_near(df_alpha(9810, t, 0), 1);
	df_near(df_alpha(9810, t, 1), .6f);
	/* Both calls take the parts of the get-or-create lookup (0x540250): a
	 * number above 0 that has none gets one, enabled as every new parts
	 * (0x550a9c); 0 and below have no object. */
	DF_CHECK(BU_PE("IsEnableSpeedUp", BU_I(9811)).i == 1 && parts_try_get(9811),
		 "IsEnableSpeedUp of a new number: %d, parts %d; want 1 and a parts", BU_PE("IsEnableSpeedUp", BU_I(9811)).i,
		 !!parts_try_get(9811));
	BU_PE("SetEnableSpeedUp", BU_I(9812), BU_I(0));
	DF_CHECK(parts_try_get(9812) && BU_PE("IsEnableSpeedUp", BU_I(9812)).i == 0,
		 "SetEnableSpeedUp(false) of a new number: parts %d, enabled %d; want a parts and 0", !!parts_try_get(9812),
		 BU_PE("IsEnableSpeedUp", BU_I(9812)).i);
	static const int none[] = { 0, -3 };
	for (int i = 0; i < 2; i++) {
		BU_PE("SetEnableSpeedUp", BU_I(none[i]), BU_I(1));
		DF_CHECK(BU_PE("IsEnableSpeedUp", BU_I(none[i])).i == 0 && !parts_try_get(none[i]),
			 "number %d: enabled %d, parts %d; want no object", none[i], BU_PE("IsEnableSpeedUp", BU_I(none[i])).i,
			 !!parts_try_get(none[i]));
	}
}

/* The fade ends in the update, shown or not: the text widget's update builds
 * its draw items every time (0x565200 -> 0x5b8020 -> 0x5bf2c0, where show is
 * only a value of the item), and 0x5bcf60 ends the fade of a run whose
 * characters are all at full alpha (0x5bd36a). Nothing is drawn in this
 * case: the time of an update fixes the text, and nothing else but Fix. */
static void df_update_fixes(void)
{
	df_setup(.5f);
	df_text(9810, "AB", 42);
	BU_PE("SetMessageWindowTextSpeed", BU_I(9810), BU_I(100));
	struct parts_text *t = df_layout(9810);
	DF_CHECK(!df_fixed(9810), "fixed at time 0");
	parts_message_window_update(299);
	df_near(df_alpha(9810, t, 1), .995f);
	DF_CHECK(!df_fixed(9810), "fixed 1 ms before the last character is at full alpha");
	parts_message_window_update(1);
	df_near(df_alpha(9810, t, 1), 1);
	DF_CHECK(df_fixed(9810), "not fixed by the update that reaches the end of the fade");
	/* A hidden window, and one whose parts is hidden, is updated all the same. */
	df_text(9810, "CD", 43);
	PE_SetShow(9810, false);
	parts_message_window_show = false;
	PE_UpdateComponent(0);
	DF_CHECK(!df_fixed(9810), "new text fixed without an update");
	parts_message_window_update(0);
	DF_CHECK(!df_fixed(9810), "new text fixed by an update at time 0");
	parts_message_window_update(300);
	DF_CHECK(df_fixed(9810), "a hidden window is not fixed by its update");
	/* Without an update no time passes. */
	df_text(9810, "EF", 44);
	parts_message_window_update(0);
	DF_CHECK(!df_fixed(9810), "new text fixed although no time passed");
	BU_PE("FixMessageWindowText", BU_I(9810));
	DF_CHECK(df_fixed(9810), "Fix did not fix the text");
}

/* The script reads the left button as it is, and every release within the
 * scope sends the whole-screen click when the button comes up, not before
 * (0x547c90 at 0x547f81 and 0x548064); the engine leaves g_EndPartsBusyLoop
 * to the script (EndWaitForClick). The script's CASClick, its bytecode run
 * unchanged, so ends a wait on the release, with the button up for the line
 * that follows, and takes a button held on entering a wait as the first
 * click, which steps nothing (CheckKeyClickStepMessage).
 * A button already down at BeginInput sends the whole-screen click when it
 * comes up (0x546030 samples it there), and a clickable parts gets its own
 * click after the whole-screen one, on the release (0x5788d0; see
 * input-model). */
static int df_key_is_down(int key)
{
	return key_is_down(key);
}

static int df_click_call(int click, const char *method, int nargs, const int *args)
{
	char name[64];
	snprintf(name, sizeof(name), "CASClick@%s", method);
	int fno = ain_get_function(ain, name);
	if (fno < 0) {
		DF_CHECK(false, "no %s", name);
		return -1;
	}
	call_method_args(fno, click, nargs, args);
	return ain->functions[fno].return_type.data == AIN_VOID ? 0 : stack_pop().i;
}

/* One input frame with the left button as given: how many messages it
 * queued and the first one's type, parts number and variables. */
struct df_frame {
	int n, type, no, x, y, key, last;
};
static struct df_frame df_input_frame(bool down)
{
	extern bool key_state[];
	int (*var)(int) = (int (*)(int))br_pe_fn("GetMessageVariableInt");
	assert(var);
	key_state[VK_LBUTTON] = down;
	PE_UpdateInputState(16);
	struct df_frame f = { .type = -1, .no = -1, .last = -1 };
	for (int t; (t = br_msg_type()) >= 0; br_msg_pop()) {
		if (!f.n++) {
			f.type = t;
			f.no = br_msg_parts();
			f.x = var(0);
			f.y = var(1);
			f.key = var(2);
		}
		f.last = br_msg_parts();
	}
	return f;
}

static void df_click_release(void)
{
	df_setup(.5f);
	extern bool key_state[];
	lr_replace_hll("IbisInputEngine", "Key_IsDown", (void *)df_key_is_down);
	int *busy = &heap[global_page_slot].page->values[2].i;	/* g_EndPartsBusyLoop */
	*busy = 0;
	int click = new_object_from_ain(ain_get_struct(ain, "CASClick"));
	const int any = INT_MIN, left_cancel[] = { VK_LBUTTON, 1 }, cancel = 1;
	const int BOX = 9820;
	br_box(BOX, 100, 100, 200, 80, true);
	mouse_set_pos(640, 300);
	PE_UpdateComponent(0);

	/* A wait entered with the button up. */
	PE_BeginInput();
	df_click_call(click, "Init", 2, left_cancel);
	struct df_frame f = df_input_frame(false);
	DF_CHECK(!f.n && !df_click_call(click, "IsClick", 1, &any), "a click without a press (%d message(s))", f.n);
	f = df_input_frame(true);
	DF_CHECK(!f.n && !*busy, "the press queued %d message(s) (type %d parts %d), g_EndPartsBusyLoop %d; want none and 0",
		 f.n, f.type, f.no, *busy);
	DF_CHECK(key_is_down(VK_LBUTTON), "the script does not see the pressed button");
	DF_CHECK(!df_click_call(click, "IsFirstClick", 0, NULL) && !df_click_call(click, "IsClick", 1, &any),
		 "the press alone is a click for the script");
	f = df_input_frame(true);
	DF_CHECK(!f.n && !*busy && key_is_down(VK_LBUTTON) && !df_click_call(click, "IsClick", 1, &any),
		 "the held button: %d message(s), g_EndPartsBusyLoop %d, down %d", f.n, *busy, key_is_down(VK_LBUTTON));
	/* The message has the cursor's position at the release (0x54806a). */
	mouse_set_pos(700, 320);
	f = df_input_frame(false);
	DF_CHECK(f.n == 1 && f.type == 4 && f.no == 0 && f.x == 700 && f.y == 320 && f.key == 1 && !*busy,
		 "the release queued %d message(s), first type %d parts %d (%d,%d,%d), g_EndPartsBusyLoop %d; want 1, 4, 0 (700,320,1), 0",
		 f.n, f.type, f.no, f.x, f.y, f.key, *busy);
	DF_CHECK(!key_is_down(VK_LBUTTON), "the released button reads as down");
	DF_CHECK(df_click_call(click, "IsClick", 1, &any) == 1, "the script's CASClick does not take the release as its click");
	f = df_input_frame(false);
	DF_CHECK(!f.n && !df_click_call(click, "IsClick", 1, &any), "one release gave a second click (%d message(s))", f.n);
	/* Released and pressed again between two frames: the script sees the
	 * new press at once. */
	df_input_frame(true);
	key_state[VK_LBUTTON] = false;
	key_state[VK_LBUTTON] = true;
	DF_CHECK(key_is_down(VK_LBUTTON), "a press that follows a release at once reads as up");
	f = df_input_frame(false);
	DF_CHECK(f.n == 1 && f.no == 0, "the release after it queued %d message(s) (parts %d), want the whole-screen click", f.n, f.no);

	/* A press outside the scope, as while a line fades in: the script sees
	 * it (and completes the line), nothing is sent for it, and the wait
	 * that follows takes it as the first click, which is no click for the
	 * script. The engine samples the held button at BeginInput (0x546030),
	 * so its release is the whole-screen click (0x548064), which no parts
	 * takes (there was no press in the scope). */
	PE_EndInput();
	f = df_input_frame(true);
	DF_CHECK(!f.n && key_is_down(VK_LBUTTON), "a press outside the scope: %d message(s), down %d", f.n, key_is_down(VK_LBUTTON));
	PE_BeginInput();
	df_click_call(click, "Reset", 1, &cancel);
	f = df_input_frame(true);
	DF_CHECK(!f.n && df_click_call(click, "IsFirstClick", 0, NULL) == 1, "a button held on entering the wait is not the first click");
	f = df_input_frame(false);
	DF_CHECK(f.n == 1 && f.no == 0 && !*busy, "the release of a press from before the wait queued %d message(s) (parts %d), g_EndPartsBusyLoop %d; want the whole-screen click and 0",
		 f.n, f.no, *busy);
	DF_CHECK(df_click_call(click, "IsFirstClick", 0, NULL) == 1 && !df_click_call(click, "IsClick", 1, &any),
		 "the release of the first click is a click");
	f = df_input_frame(true);
	DF_CHECK(!f.n && !df_click_call(click, "IsFirstClick", 0, NULL) && !df_click_call(click, "IsClick", 1, &any),
		 "the next press: %d message(s), or a click before its release", f.n);
	f = df_input_frame(false);
	DF_CHECK(f.n == 1 && f.no == 0 && df_click_call(click, "IsClick", 1, &any) == 1,
		 "the next click: %d message(s) (parts %d), or no click for the script", f.n, f.no);

	/* A clickable parts: the press sends nothing; its release sends the
	 * whole-screen click first and the parts' own MouseClick after it
	 * (0x548064, then 0x5788d0). */
	mouse_set_pos(150, 140);
	PE_UpdateComponent(0);
	df_input_frame(false);
	f = df_input_frame(true);
	DF_CHECK(!f.n, "a press on a clickable parts queued %d message(s), first type %d parts %d; want none", f.n, f.type, f.no);
	f = df_input_frame(false);
	DF_CHECK(f.n == 2 && f.type == 4 && f.no == 0 && f.last == BOX,
		 "its release queued %d message(s), first type %d parts %d, last parts %d; want 2, 4, 0 (the whole-screen click), %d",
		 f.n, f.type, f.no, f.last, BOX);

	/* The scope ends with the button down: its release sends nothing (the
	 * input does nothing outside a scope, 0x547cba). */
	mouse_set_pos(640, 300);
	PE_UpdateComponent(0);
	df_input_frame(false);
	f = df_input_frame(true);
	PE_EndInput();
	struct df_frame g = df_input_frame(false);
	DF_CHECK(!f.n && !g.n && !*busy, "a scope ended while the button was down: %d and %d message(s), g_EndPartsBusyLoop %d",
		 f.n, g.n, *busy);
	/* A scope opened while the button is down samples it (0x546030, into
	 * +0x40): the release is the whole-screen click. So is it for the outer
	 * scope when the inner one ends first (0x58a750 -> 0x53e830 samples
	 * again). The press itself went to no scope: no parts is clicked. */
	PE_BeginInput();
	df_input_frame(false);
	f = df_input_frame(true);
	PE_BeginInput();
	g = df_input_frame(false);
	DF_CHECK(!f.n && g.n == 1 && g.no == 0, "a scope opened while the button was down: %d and %d message(s) (parts %d); want 0 and the whole-screen click",
		 f.n, g.n, g.no);
	f = df_input_frame(true);
	PE_EndInput();
	g = df_input_frame(false);
	DF_CHECK(!f.n && g.n == 1 && g.no == 0, "an inner scope ended while the button was down: %d and %d message(s) (parts %d) for the outer one; want 0 and the whole-screen click",
		 f.n, g.n, g.no);
	/* The outer scope takes the next click as before. */
	f = df_input_frame(true);
	g = df_input_frame(false);
	DF_CHECK(!f.n && g.n == 1 && g.no == 0, "the outer scope's next click: %d and %d message(s) (parts %d)", f.n, g.n, g.no);
	PE_EndInput();
}

/* The pactex keys: 字速度 of a message window, 0 without the key (0x4f0ee0),
 * and スピードアップ有効 of any component, 1 without the key and stored as
 * "== 1" (0x554b75). */
#define DF_SPEED "\xd7\xd6\xcb\xd9\xb6\xc8"								/* 字速度 */
#define DF_SPEED_UP "\xa5\xb9\xa5\xd4\xa9\x60\xa5\xc9\xa5\xa2\xa5\xc3\xa5\xd7\xd3\xd0\x84\xbf"	/* スピードアップ有効 */

/* name { 種類別情報 { 部件タイプ = 信息窗口, [字速度] }, [スピードアップ有効] };
 * a negative value leaves the key out. */
static struct ex_tree df_window_node(const char *name, int speed, int speed_up)
{
	struct ex_tree t = bu_branch(name, 2);
	t.children[0] = bu_branch(BU_INFO, speed >= 0 ? 2 : 1);
	t.children[0].children[0] = bu_sleaf(BU_TYPE, KW_MSGWIN);
	if (speed >= 0)
		t.children[0].children[1] = bu_ileaf(DF_SPEED, speed);
	if (speed_up >= 0)
		t.children[1] = bu_ileaf(DF_SPEED_UP, speed_up);
	else
		t.nr_children = 1;
	return t;
}

static bool df_read_activity_file(struct string *name, possibly_unused struct string *filename, possibly_unused bool edit)
{
	struct ex *e = xcalloc(1, sizeof(*e));
	e->nr_blocks = 1;
	e->blocks = xcalloc(1, sizeof(*e->blocks));
	e->blocks[0].name = cstr_to_string("FadeActivity");
	e->blocks[0].val.type = EX_TREE;
	struct ex_tree *top = xcalloc(1, sizeof(*top));
	*top = bu_branch("FadeActivity", 1);
	e->blocks[0].val.tree = top;
	top->children[0] = bu_comp("\xa5\xeb\xa9\x60\xa5\xc8\xb2\xbf\xbc\xfe", BU_LAYOUT, 0, 4);
	struct ex_tree *kids = &top->children[0].children[1];
	kids->children[0] = df_window_node("WinSpeed", 31, -1);
	kids->children[1] = df_window_node("WinNoKeys", -1, -1);
	kids->children[2] = df_window_node("WinOff", 23, 0);
	kids->children[3] = df_window_node("WinTwo", 23, 2);
	bool ok = probe_load_activity_tree(name, e);
	ex_free(e);
	return ok;
}

static void df_loader(void)
{
	df_setup(.5f);
	lr_replace_hll("PartsEngine", "ReadActivityFile", (void *)df_read_activity_file);
	int n = sf_str("Fade", -1), f = sf_str("Fade", -1);
	bool ok = BU_PE("ReadActivityFile", BU_I(n), BU_I(f), BU_I(0)).i;
	static const struct { const char *name; int speed, speed_up; } want[] = {
		{ "WinSpeed", 31, 1 }, { "WinNoKeys", 0, 1 }, { "WinOff", 23, 0 }, { "WinTwo", 23, 0 },
	};
	int no[4];
	for (int i = 0; i < 4; i++) {
		no[i] = BU_PE("GetActivityPartsNumber", BU_I(n), BU_I(sf_str(want[i].name, -1))).i;
		DF_CHECK(ok && df_window(no[i]), "%s: activity loaded %d, parts %d, window %d", want[i].name, ok, no[i], df_window(no[i]));
		if (!df_window(no[i]))
			return;
		DF_CHECK(df_speed(no[i]) == want[i].speed && BU_PE("IsEnableSpeedUp", BU_I(no[i])).i == want[i].speed_up,
			 "%s: text speed %d, speed-up %d; want %d, %d", want[i].name, df_speed(no[i]),
			 BU_PE("IsEnableSpeedUp", BU_I(no[i])).i, want[i].speed, want[i].speed_up);
	}
	/* With the key the text fades in, without it the text shows at once. */
	df_text(no[0], "AB", 45);
	df_text(no[1], "AB", 46);
	struct parts_text *t = df_layout(no[0]);
	df_near(df_alpha(no[0], t, 1), 0);
	DF_CHECK(!df_fixed(no[0]) && df_fixed(no[1]), "after the first update: with the key fixed %d, without it %d; want 0, 1",
		 df_fixed(no[0]), df_fixed(no[1]));
	parts_message_window_update(31);
	df_near(df_alpha(no[0], t, 0), .155f);
	df_near(df_alpha(no[0], t, 1), 0);
	parts_message_window_update(1);
	df_near(df_alpha(no[0], t, 1), .005f);
}

/* The second time of UpdateComponent, the scaled passed time, reaches the
 * window through the HLL call (0x535260 has it at [ebp+0x70] and selects it
 * at 0x5352b1). */
static void df_update_component(void)
{
	df_setup(.5f);
	void (*update_view)(void) = dlsym(RTLD_DEFAULT, "frame_pacing_update_view");
	if (!update_view)
		df_no_symbol("frame_pacing_update_view");
	extern void audio_reset(void);
	audio_reset();	/* no audio device in the probe */
	df_text(9810, "AB", 47);
	BU_PE("SetMessageWindowTextSpeed", BU_I(9810), BU_I(100));
	struct parts_text *t = df_layout(9810);
	update_view();	/* UpdateView presents the frame, not this call */
	BU_PE("UpdateComponent", BU_I(20), BU_I(100), BU_I(1), BU_F(1.0f), BU_F(1.0f));
	df_near(df_alpha(9810, t, 0), .5f);
	df_near(df_alpha(9810, t, 1), 0);
	BU_PE("SetEnableSpeedUp", BU_I(9810), BU_I(0));
	update_view();
	BU_PE("UpdateComponent", BU_I(20), BU_I(100), BU_I(1), BU_F(1.0f), BU_F(1.0f));
	df_near(df_alpha(9810, t, 0), .6f);
	df_near(df_alpha(9810, t, 1), .1f);
}

static int test_dialogue_fade(void)
{
	const struct { const char *name;void(*fn)(void); } cases[]={
		{"DF1",df_missing},{"DF2",df_clock},{"DF3",df_fix_reset},{"DF4",df_tags},{"DF5",df_rate},{"DF6",df_pixels},{"DF7",df_render_before_update},{"DF8",df_v13},{"DF9",df_bindings},{"DF10",df_multibyte},
		{"DF11",df_dirty},{"DF12",df_scaled_time},{"DF13",df_update_fixes},{"DF14",df_click_release},
		{"DF15",df_loader},{"DF16",df_update_component}};
	int failed=0,no_symbol=0;
	for(size_t i=0;i<sizeof cases/sizeof cases[0];i++) {
		fflush(NULL);pid_t p=fork();if(!p){df_failures=0;cases[i].fn();printf("%s %s\n",cases[i].name,df_failures?"FAIL":"PASS");fflush(NULL);_exit(df_failures?1:0);}
		int s;waitpid(p,&s,0);if(!WIFEXITED(s)||WEXITSTATUS(s))failed++;
		if(WIFEXITED(s)&&WEXITSTATUS(s)==DF_NO_SYMBOL){no_symbol++;printf("%s NO SYMBOL\n",cases[i].name);}
		else if(!WIFEXITED(s)||WEXITSTATUS(s)>1)printf("%s ABORTED\n",cases[i].name);
	}
	printf("DIALOGUE-FADE %s: %d of %d cases failed",failed?"FAIL":"PASS",failed,(int)(sizeof cases/sizeof cases[0]));
	if(no_symbol)printf(" (%d of them for a missing function)",no_symbol);
	puts("");return !!failed;
}
