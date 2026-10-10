/* parts-upper-left: native 0x58bf80 / 0x58c020 / 0x58c0e0 share
 * 0x534bb0: transform the origin corner, retain float precision, write X
 * then Y. All three take the component from the get-or-create lookup
 * (0x540250 -> 0x53a0b0 -> 0x578ee0): a positive number without a component
 * gets a default one and its corner is reported; only a number of 0 or less
 * (0x539cb0) leaves the output refs untouched and makes the scalar getters
 * return zero. Integer half-size precedes the float transform. Default 2D
 * camera only; other camera/projection ratios are not claimed here.
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
	ri_postlink();
	/* A positive number without a component: each getter makes the default
	 * component (empty cell, 0x578f07 -> 0x578cb0) and reports its corner,
	 * (0,0) for a new one. One number per getter, so each is seen to create. */
	static const int missing[3]={9636,9637,9638};
	for(int i=0;i<3;i++)
		UL_CHECK(!parts_try_get(missing[i]),"missing fixture part %d already exists",missing[i]);
	int pg=ul_page();ul_combined(missing[0],1,pg,0,1);
	UL_CHECK(heap[pg].page->values[0].f==0 && heap[pg].page->values[1].f==0,
		"combined getter on a missing part left (%g,%g); want the new part's (0,0)",
		heap[pg].page->values[0].f,heap[pg].page->values[1].f);
	UL_CHECK(parts_try_get(missing[0]),"combined getter did not create the missing part");
	heap_unref(pg);
	UL_CHECK(ul_scalar(missing[1],1,false)==0,"scalar X of a missing part not zero");
	UL_CHECK(parts_try_get(missing[1]),"scalar X getter did not create the missing part");
	UL_CHECK(ul_scalar(missing[2],1,true)==0,"scalar Y of a missing part not zero");
	UL_CHECK(parts_try_get(missing[2]),"scalar Y getter did not create the missing part");
	/* The component a getter made is an ordinary one afterwards. */
	if(parts_try_get(missing[0])){ri_pos(missing[0],30,40);PE_UpdateComponent(0);ul_expect(missing[0],1,30,40);}
	/* A number of 0 or less has no component (0x539cb0 returns before the
	 * table is touched): nothing is made, the combined outputs keep what
	 * the caller put there and the scalar getters return zero. */
	static const int none[3]={0,-1,-9639};
	for(int i=0;i<3;i++) {
		pg=ul_page();ul_combined(none[i],1,pg,0,1);
		UL_CHECK(heap[pg].page->values[0].f==1234.25f && heap[pg].page->values[1].f==-4321.5f,
			"number %d changed output sentinels",none[i]);
		UL_CHECK(ul_scalar(none[i],1,false)==0 && ul_scalar(none[i],1,true)==0,"number %d scalar not zero",none[i]);
		UL_CHECK(!parts_try_get(none[i]),"number %d got a component",none[i]);
		heap_unref(pg);
	}
	parts_get(9631);ri_pos(9631,100,200);ri_rect(9631,17,13);ri_origin(9631,8);
	ri_hll("Parts_SetPartsRectangleDetectionSize",4,(union vm_value[]){RI_I(9631),RI_I(40),RI_I(30),RI_I(2)});
	PE_UpdateComponent(0);ul_expect(9631,1,92,187);ul_expect(9631,2,80,170);
	/* States outside 1..3 write nothing and return zero: a limit of this
	 * port's three states, not the original's rule. Natively the state goes
	 * unchecked to the content (0x534be8, vtable +0x20); a low-level widget
	 * indexes its four state cells with it (0x5b7cb0, cells 0..3 made at
	 * 0x5b730c), so EPartsState Disable (4) reads past them there and state
	 * 0 reads a cell this port does not have. Other widget kinds not read.
	 * The lookup comes first (0x58bfb4, then 0x58bfda), so a missing
	 * component is made whatever the state. */
	static const int bad_state[2]={0,4};
	for(int i=0;i<2;i++) {
		pg=ul_page();ul_combined(9631,bad_state[i],pg,0,1);
		UL_CHECK(heap[pg].page->values[0].f==1234.25f && heap[pg].page->values[1].f==-4321.5f,
			"state %d changed output sentinels",bad_state[i]);
		UL_CHECK(ul_scalar(9631,bad_state[i],false)==0 && ul_scalar(9631,bad_state[i],true)==0,
			"state %d scalar not zero",bad_state[i]);
		UL_CHECK(!parts_try_get(9632+i),"missing fixture part %d already exists",9632+i);
		ul_combined(9632+i,bad_state[i],pg,0,1);
		UL_CHECK(heap[pg].page->values[0].f==1234.25f && heap[pg].page->values[1].f==-4321.5f,
			"state %d on a missing part changed output sentinels",bad_state[i]);
		UL_CHECK(parts_try_get(9632+i),"state %d: the lookup did not create the missing part",bad_state[i]);
		heap_unref(pg);
	}
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
