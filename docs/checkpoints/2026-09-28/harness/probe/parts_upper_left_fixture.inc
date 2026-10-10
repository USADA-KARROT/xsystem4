/* parts-upper-left: native 0x58bf80 / 0x58c020 / 0x58c0e0 share
 * 0x534bb0: transform the origin corner, retain float precision, write X
 * then Y. Missing parts leave output refs untouched; scalar getters return
 * zero. Integer half-size precedes the float transform. Default 2D camera
 * only; other camera/projection ratios are not claimed here.
 *
 * The combined AIN declaration has 4 arguments but SIX VM slots. Exercise
 * the real FFI and AFL wrapper; do not hide a bad return-slot count.
 * Fixtures use rectangles only, no GL, images, game saves or new symbols.
 */
static int ul_failures;
#define UL_CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); ul_failures++; } } while (0)
#define UL_NEAR(a,b) (fabsf((a)-(b)) < 0.002f)

static int ul_page(void)
{
	int pg = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_FLOAT, 2));
	heap[pg].page->values[0].f = 1234.25f;
	heap[pg].page->values[1].f = -4321.5f;
	return pg;
}

static void ul_combined(int no, int state, int pg, int xi, int yi)
{
	int lib, fn = ri_fn("Parts_GetPartsUpperLeftPos", &lib);
	int sp = stack_ptr, refs = HEAP_REF(pg);
	union vm_value args[] = { RI_I(no), RI_I(pg), RI_I(xi), RI_I(pg), RI_I(yi), RI_I(state) };
	for (int i = 0; i < 6; i++) stack_push(args[i]);
	hll_call(lib, fn, 0);
	UL_CHECK(stack_ptr == sp, "combined returned %d slots; want void", stack_ptr-sp);
	UL_CHECK(HEAP_REF(pg) == refs, "combined changed output page references");
	stack_ptr = sp;
}

static float ul_scalar(int no, int state, bool y)
{
	int lib, fn = ri_fn(y ? "Parts_GetPartsUpperLeftPosY" : "Parts_GetPartsUpperLeftPosX", &lib);
	int sp = stack_ptr;
	stack_push(RI_I(no)); stack_push(RI_I(state)); hll_call(lib, fn, 0);
	UL_CHECK(stack_ptr == sp+1, "scalar returned %d slots", stack_ptr-sp);
	float out = stack_ptr == sp+1 ? stack[sp].f : NAN;
	stack_ptr = sp;
	return out;
}

static void ul_expect(int no, int state, float x, float y)
{
	int pg = ul_page();
	ul_combined(no, state, pg, 0, 1);
	float ox=heap[pg].page->values[0].f, oy=heap[pg].page->values[1].f;
	UL_CHECK(UL_NEAR(ox,x) && UL_NEAR(oy,y), "combined (%g,%g), want (%g,%g)", ox,oy,x,y);
	float sx=ul_scalar(no,state,false), sy=ul_scalar(no,state,true);
	UL_CHECK(UL_NEAR(sx,x) && UL_NEAR(sy,y), "scalar (%g,%g), want (%g,%g)",sx,sy,x,y);
	heap_unref(pg);
}

static void ul_basic(void)
{
	ri_postlink();
	parts_get(9601); parts_get(9602);
	ri_pos(9601,838,0); ri_parent(9602,9601); ri_pos(9602,86,720);
	ri_rect(9602,120,240); ri_origin(9602,8); PE_UpdateComponent(0);
	ul_expect(9602,1,864,480);
	int pg=ul_page(); ul_combined(9602,1,pg,0,0);
	UL_CHECK(UL_NEAR(heap[pg].page->values[0].f,480), "aliased outputs must end with Y");
	heap_unref(pg);
}

static void ul_fraction(void)
{
	ri_postlink();parts_get(9611);parts_get(9612);
	ri_pos(9611,300,200);pt_mag(9611,.5f,.5f);
	ri_parent(9612,9611);ri_pos(9612,10,0);ri_rect(9612,17,13);ri_origin(9612,8);PE_UpdateComponent(0);
	ul_expect(9612,1,301,193.5f);
	UL_CHECK(PE_GetPartsUpperLeftPosX(9612,1)==301 && PE_GetPartsUpperLeftPosY(9612,1)==194,
		"legacy integer getter rounding changed");
	ri_pos(9611,-300,-200);PE_UpdateComponent(0);ul_expect(9612,1,-299,-206.5f);
	UL_CHECK(PE_GetPartsUpperLeftPosY(9612,1)==-207,"legacy negative rounding changed");
}

static void ul_rotated(void)
{
	ri_postlink();parts_get(9621);parts_get(9622);
	ri_pos(9621,300,200);pt_rot(9621,315);
	ri_parent(9622,9621);ri_pos(9622,10,0);ri_rect(9622,17,13);ri_origin(9622,8);PE_UpdateComponent(0);
	const float k=sqrtf(.5f);
	/* Corner is (10-8,-13) through a clockwise 45-degree parent. */
	ul_expect(9622,1,300-11*k,200-15*k);
	ri_lr(9622,1);ri_tb(9622,1);PE_UpdateComponent(0);
	/* Mirrored original corner, not the minimum of an axis-aligned box. */
	ul_expect(9622,1,300+31*k,200-5*k);
}

static void ul_missing_state(void)
{
	ri_postlink();const int missing=9639;
	UL_CHECK(!parts_try_get(missing),"missing fixture part already exists");
	int pg=ul_page();ul_combined(missing,1,pg,0,1);
	UL_CHECK(heap[pg].page->values[0].f==1234.25f && heap[pg].page->values[1].f==-4321.5f,
		"missing part changed output sentinels");
	UL_CHECK(ul_scalar(missing,1,false)==0 && ul_scalar(missing,1,true)==0,"missing scalar not zero");
	UL_CHECK(!parts_try_get(missing),"read-only getter created a missing part");heap_unref(pg);
	parts_get(9631);ri_pos(9631,100,200);ri_rect(9631,17,13);ri_origin(9631,8);
	ri_hll("Parts_SetPartsRectangleDetectionSize",4,(union vm_value[]){RI_I(9631),RI_I(40),RI_I(30),RI_I(2)});
	PE_UpdateComponent(0);ul_expect(9631,1,92,187);ul_expect(9631,2,80,170);
	/* Invalid states retain a safe no-write contract, not a claimed native
	 * subtype rule (those have not all been disassembled). */
	pg=ul_page();ul_combined(9631,0,pg,0,1);
	UL_CHECK(heap[pg].page->values[0].f==1234.25f && heap[pg].page->values[1].f==-4321.5f,
		"invalid state changed output sentinels");heap_unref(pg);
}

static void ul_script_formula(void)
{
	ri_postlink();parts_get(9641);parts_get(9642);
	ri_pos(9641,838,0);ri_parent(9642,9641);ri_rect(9642,120,240);ri_origin(9642,8);
	int pg=ul_page(), targets[]={192,500,628};
	for(int i=0;i<3;i++) {
		ri_pos(9642,86,720);PE_UpdateComponent(0);
		union vm_value args[]={RI_I(9642),RI_I(pg),RI_I(0),RI_I(pg),RI_I(1),RI_I(1)};
		int sp=stack_ptr,csp=call_stack_ptr,refs=HEAP_REF(pg);size_t ip=instr_ptr;
		for(int j=0;j<6;j++)stack_push(args[j]);
		function_call(nm_fn("AFL_Parts_GetUpperLeftPos"),VM_RETURN);vm_execute();instr_ptr=ip;
		UL_CHECK(stack_ptr==sp && call_stack_ptr==csp,"AFL wrapper stack imbalance");stack_ptr=sp;
		UL_CHECK(HEAP_REF(pg)==refs,"AFL wrapper changed output page references");
		int current=(int)(heap[pg].page->values[0].f+60);
		UL_CHECK(current==924,"AFL current centre %d, want924",current);
		/* CustomerViewSet.MoveOut formula; motion timing is verified in GUI. */
		int destination=86+targets[i]-current;
		ri_pos(9642,destination,720);PE_UpdateComponent(0);
		ul_expect(9642,1,targets[i]-60,480);
	}
	heap_unref(pg);
}

/* The selector is pre-existing; call it with a harmless sentinel instead
 * of invoking an unknown ABI. This also checks the v13 fallback boundary. */
extern void *hll_shape_select_function(const char *, const struct ain_hll_function *, void *);
static float ul_sentinel(int number, int state) { return number + state; }
static void ul_shape_guard(void)
{
	int lib, fn=ri_fn("Parts_GetPartsUpperLeftPosX",&lib);
	struct ain_hll_function f=ain->libraries[lib].functions[fn];
	void *fallback=(void *)ul_sentinel;
	UL_CHECK(hll_shape_select_function("PartsEngine",&f,fallback)!=fallback,
		"known float(int,int) shape did not select the v14 implementation");
	ain->version=13;
	UL_CHECK(hll_shape_select_function("PartsEngine",&f,fallback)==fallback,"v13 binding changed");
	ain->version=14;
	f.return_type.data=AIN_INT;
	UL_CHECK(hll_shape_select_function("PartsEngine",&f,fallback)==fallback,"unknown return shape changed");
	f=ain->libraries[lib].functions[fn];f.nr_arguments=1;
	UL_CHECK(hll_shape_select_function("PartsEngine",&f,fallback)==fallback,"unknown arity changed");
}

static int ul_run(const char *name,void (*fn)(void))
{
	fflush(stdout);pid_t pid=fork();
	if(pid<0){perror("fork");return 1;}
	if(!pid){ul_failures=0;alarm(30);fn();fflush(stdout);_exit(ul_failures?1:0);}
	int status=0;bool waited=waitpid(pid,&status,0)==pid;
	bool ok=waited && WIFEXITED(status) && WEXITSTATUS(status)==0;
	printf("%s %s\n",name,ok?"PASS":"FAIL");return ok?0:1;
}

static int test_parts_upper_left(void)
{
	int failed=0;
	failed+=ul_run("UL1 combined and aliased output ABI",ul_basic);
	failed+=ul_run("UL2 float corner and integer legacy guard",ul_fraction);
	failed+=ul_run("UL3 rotated and mirrored original corner",ul_rotated);
	failed+=ul_run("UL4 missing part and per-state dimensions",ul_missing_state);
	failed+=ul_run("UL5 real AFL wrapper and customer destinations",ul_script_formula);
	failed+=ul_run("UL6 declaration selection and legacy fallback",ul_shape_guard);
	printf("PARTS-UPPER-LEFT %s: %d of 6 cases failed\n",failed?"FAIL":"PASS",failed);
	return failed?1:0;
}
