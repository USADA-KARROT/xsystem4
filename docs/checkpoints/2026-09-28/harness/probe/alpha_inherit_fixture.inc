/* alpha-inherit: native v14 nearest resolved alpha mask is inherited by
 * descendants. Test live parent/mask changes through the production resolver,
 * using real AIN HLL declarations for setters. No assets, GL or files.
 * Before the helper exists, use the exact old renderer's local-only selection.
 */
#include <dlfcn.h>
static int ac_failures;
#define AC_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ac_failures++; } } while (0)

static int ac_effective(struct parts *p)
{
	int (*resolve)(struct parts *) = dlsym(RTLD_DEFAULT, "parts_effective_alpha_clipper");
	return resolve ? resolve(p) : p->alpha_clipper_parts_no;
}
static void ac_mask(int p, int mask)
{
	BU_PE("SetComponentAlphaClipper", BU_I(p), BU_I(mask));
}
static void ac_expect(int p, int mask, const char *step)
{
	int actual = ac_effective(parts_get(p));
	AC_CHECK(actual == mask, "%s: got mask %d, want %d", step, actual, mask);
}
static void ac_inherit(void)
{
	bu_postlink();
	parts_get(18710); parts_get(18711);
	for (int i = 0; i < 4; i++) {
		parts_get(18700+i);
		if (i) PE_SetParentPartsNumber(18700+i, 18699+i);
	}
	ac_mask(18700,18710);
	ac_expect(18700,18710,"local mask");
	ac_expect(18703,18710,"three-level inherited mask");
	ac_mask(18701,18711);
	ac_expect(18703,18711,"nearest mask overrides ancestor");
	ac_mask(18703,18710);
	ac_expect(18703,18710,"own mask overrides parents");
	ac_mask(18703,0);
	PE_SetShow(18711, false);
	ac_expect(18703,18711,"dynamic Show false still masks");
	parts_set_edit_hidden(parts_get(18711), true);
	ac_expect(18703,18710,"editor-hidden mask falls through");
	parts_set_edit_hidden(parts_get(18711), false);
	ac_expect(18703,18711,"zero resumes inheritance");
	ac_mask(18703,18799);
	ac_expect(18703,18711,"unknown number keeps inherited mask");
	AC_CHECK(!parts_try_get(18799),"resolver created missing mask");
	AC_CHECK(parts_get(18703)->alpha_clipper_parts_no==18799,"resolver changed local property");
	ac_mask(18701,-1);
	ac_expect(18703,18710,"invalid local and parent fall through");
	ac_mask(18700,0);
	ac_expect(18703,0,"no resolved mask");
}
static void ac_transitions(void)
{
	bu_postlink();
	int ids[]={18800,18801,18802,18803,18810,18811};
	for (unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++) parts_get(ids[i]);
	PE_SetParentPartsNumber(18802,18800);
	PE_SetParentPartsNumber(18803,18802);
	ac_mask(18800,18810); ac_mask(18801,18811);
	ac_expect(18803,18810,"initial parent");
	PE_SetParentPartsNumber(18802,18801);
	ac_expect(18803,18811,"reparent next frame");
	PE_SetParentPartsNumber(18802,0);
	ac_expect(18803,0,"detached subtree");
	PE_SetParentPartsNumber(18802,18800);
	ac_mask(18802,18811);
	/* Valid but untextured masks still override. The renderer's pre-existing
	 * unsupported-mask fallback, not this resolver, handles their texture. */
	AC_CHECK(!parts_get(18811)->states[0].common.texture.handle,"fixture has texture");
	ac_expect(18803,18811,"untextured valid local mask");
	PE_ReleaseParts(18811);
	ac_expect(18803,18810,"released local mask falls through");
	parts_get(18811);
	ac_expect(18803,18811,"replacement number resolves live");
	ac_mask(18803,18803);
	ac_expect(18803,18803,"self mask does not recurse through masks");
}
static void ac_legacy(void)
{
	bu_postlink();
	parts_get(18900);parts_get(18901);parts_get(18910);
	PE_SetParentPartsNumber(18901,18900);
	ac_mask(18900,18910);
	int version=ain->version;ain->version=13;
	ac_expect(18901,0,"v13 does not inherit");
	ac_mask(18901,18910);ac_expect(18901,18910,"v13 local mask");
	ac_mask(18901,18999);ac_expect(18901,18999,"v13 unknown local preserved");
	AC_CHECK(!parts_try_get(18999),"legacy resolver created unknown mask");
	ain->version=version;
}
static int test_alpha_inherit(void)
{
	void (*cases[])(void)={ac_inherit,ac_transitions,ac_legacy};
	int failed=0;
	for(unsigned i=0;i<3;i++) {
		fflush(stdout);pid_t pid=fork();
		if(pid==0){alarm(30);ac_failures=0;cases[i]();fflush(stdout);_exit(ac_failures?1:0);}
		int st=0;bool ok=pid>0&&waitpid(pid,&st,0)==pid&&WIFEXITED(st)&&WEXITSTATUS(st)==0;
		printf("AC%u %s\n",i+1,ok?"ok":"FAIL");failed+=!ok;
	}
	printf("ALPHA-INHERIT %s: %d of 3 cases failed\n",failed?"FAIL":"PASS",failed);
	return failed?1:0;
}
