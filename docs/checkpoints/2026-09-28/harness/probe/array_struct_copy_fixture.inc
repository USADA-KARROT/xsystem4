/* array-struct-copy: storing a struct value in a v14 array of value structs
 * makes the element the array's own object, a copy of the value.
 *
 * Original (static disassembly of the unpacked executable):
 *   - PushBack and Add (0x6476b0) call Insert at the end; Insert (0x647990)
 *     clamps the index (0x6479cb..0x6479e5), opens a new element there with
 *     the array's vtable +0x58 (0x67f5c0, called at 0x6479ef) and stores the
 *     value with the element setter 0x646d70(index, value, 0) (0x647a31).
 *   - 0x67f5c0 makes the new element with 0x656970(slot, type, ctx, 0)
 *     (0x67f726; an empty array goes through 0x67fe20(1, 0), which calls
 *     the same function): for a struct (type 13, 0x656a12) that is
 *     0x679b30(struct, 0), a new object whose members get their defaults
 *     (0x679c2c..0x679c99) and whose constructor is not called (the flag
 *     is 0; 0x679baf).
 *   - 0x646d70 dispatches on the array's element type (table 0x647088 /
 *     0x647064). A struct (13 -> 0x646e38) takes the element object
 *     (vtable +0x1c) and assigns the value's object to it (its vtable +4,
 *     0x681820, which calls 0x656d60 for each member). A value that is not
 *     a live struct page (0x67ac30 returns 0) assigns nothing (0x681833),
 *     and neither does a struct of another member count (0x681843..
 *     0x681859): the element keeps its defaults.
 *   - 0x656d60 per member type (table 0x657004 / 0x656fe0): int, float,
 *     bool and enum (10, 11, 47, 92) are moved; a string (12) is copied into
 *     the element's own string (0x67db50); a struct (13) is assigned
 *     recursively into the element's own member object; an array (79) is
 *     copied into the element's own array (0x67ffb0: as many default
 *     elements as the source has, each assigned with 0x656d60 by 0x6801d0);
 *     a delegate (63) is cleared and copied (0x652710); references, wraps
 *     and interfaces (18, 19, 20, 21, 51, 67, 80, 82, 87, 89, 93) copy their
 *     slots, retain the new object (0x679f10) and release the old one
 *     (0x679f90): shared.
 *   - Fill (0x648120) calls the same setter 0x646d70 for every element of
 *     the clamped range: an existing element object is assigned in place.
 *     Realloc with a value (0x647510) is Realloc (vtable +0x50) followed by
 *     Fill over the new tail.
 *   - Erase (0x647a60 -> 0x67f750; the last element through 0x67f5a0 ->
 *     0x67ec50) does not write the array's element type (+0x10).
 * This VM (before the change) stores the value's own page for Add, PushBack
 * and Insert (src/hll/Array.c), and A_REF shares a struct with reference
 * members (src/vm.c A_REF): the element is the caller's object, and a later
 * change through the array is a change of the caller's object. Fill and
 * Realloc with a value replace each element by a vm_copy of the value,
 * which deep-copies wraps and interfaces. Erase leaves an emptied array
 * without a page.
 *
 * Port strategy the expectations allow for: this VM leaves a new v14
 * object's string, struct and delegate members -1 until they are used
 * (page.c init_struct_slot). That is the original's default object, not a
 * null one, so a -1 member counts as the default: a string member -1 or ""
 * passes for the original's new empty string, and a -1 member in the value
 * does not stop the copy of the members after it.
 *
 * Cases that fail before the change (the copy):
 *   SC1  Add of A_REF of a struct (string and number members): the element
 *        is a new object of the same struct with equal members; changing
 *        it leaves the source alone and the other way round; the source
 *        keeps only its owner's reference; no function at all runs during
 *        the store (no constructor; 0x679b30 with 0, the assignment is
 *        native); nothing is left after the source and the array go.
 *   SC2  PushBack, the same checks.
 *   SC3  Insert at the head, the tail, the middle and two clamped indices
 *        (-5 and 99): each inserted element is a new copy; the elements
 *        that were there keep their objects and order.
 *   SC4  Fill#0 (all elements): every element keeps its object and gets
 *        the value's content; a wrap member is shared, not copied; no
 *        constructor runs. An element without an object (-1, as an older
 *        resize here leaves one) gets a new object with the content (port
 *        strategy: natively every element has its object).
 *   SC5  Fill#1 (index, length), including a clamped negative index: only
 *        the elements in the range are assigned, in place.
 *   SC6  Realloc#1 (n, value): the old element is untouched; each new one
 *        is its own object with the value's content, a wrap member shared.
 *   SC7  The shape of FrameInfoCollection@CopySubFrames: thirteen FrameInfo
 *        (constructed by FrameInfo@2) in a "cache" array, StartFrame 0..12,
 *        with nested strings, a nested array<string>, a nested array of
 *        structs and an option<int> set; each one A_REF'd and added to
 *        another array<FrameInfo>, whose elements then get StartFrame and
 *        StartTime (here in frame units) moved by 96, as SetSubFrameStart
 *        and its postset do. A second pass over the cache (from index 0
 *        while StartFrame < 32) still finds thirteen frames, and the cache
 *        is unchanged. Every added frame equals its cache frame member by
 *        member at every level, each string, array and struct its own; a
 *        delegate entry the constructor made for one of the frame's own
 *        objects (a layer collection's events bound to the collection) is
 *        for the copy's object in the copy (0x6818f0 -> 0x682080 ->
 *        0x682330 at each level). No function runs during the stores.
 *   SC8  Add, PushBack and Insert of a struct with a (bare) destructor, as
 *        alloc_struct makes it: after the source's owner and the array let
 *        go, the destructor has run twice, once for the source and once for
 *        the element (the element was made with flag 0, so its +0x3c stays 0
 *        and 0x6813c0 runs the destructor, 0x681403; presumably +0x28 of
 *        the struct's descriptor is the destructor). No function runs during
 *        the store: the inner objects this VM gives a new object's wrap
 *        members are not the original's (0x6569b8 leaves them -1) and go
 *        without their destructors.
 *   SC9  string member: the element has its own string slot, equal bytes;
 *        a change in place (push_back) or a new string in the element
 *        leaves the source's.
 *   SC10 struct member: the element's member is its own object with equal
 *        numbers; changing it leaves the source's member alone.
 *   SC11 array member (array<int>): the element's array is its own page
 *        with equal values; changing it leaves the source's alone.
 *   SC12 delegate member: the element's delegate is its own page with the
 *        same entry; clearing it leaves the source's entry.
 *   SC13 wrap<struct> member: shared, the object retained once more; one
 *        reference back after the source goes, and one more after the
 *        array goes.
 *   SC14 interface member: object and vtable offset shared, the object
 *        retained once more (and given back as in SC13).
 *   SC15 ref member: shared, the object retained once more (as SC13).
 *        (SC13-SC15 fail before the change because the element is the
 *        source object; after it their member checks keep a deep copy of
 *        wraps, interfaces and references from passing.)
 *        SC9-SC15 also Fill#0 two elements whose members all hold objects:
 *        each element keeps its object and its struct member object
 *        (0x656eb6 assigns into it), the members follow the rules above
 *        with both elements holding the shared objects, the member objects
 *        the elements gave up are released (0x679f90; SC13: the old wrapped
 *        object of element 0, a struct with a bare destructor, runs it once
 *        as its last reference goes), no constructor runs, nothing is left.
 *   SC16 Nested members: a struct whose array member holds structs with
 *        wrap<struct> members (0x656f45 -> 0x67ffb0 -> 0x6801d0 ->
 *        0x656d60 for each element): the element's array and each struct
 *        in it are its own, the numbers equal, the wraps shared (each
 *        wrapped object retained once per copied element, given back when
 *        the element goes).
 *   SC17 None (-1) into a typed array of value structs: Add, Insert and
 *        PushBack make a new object of the array's struct with default
 *        members and no constructor (0x67f5c0; 0x646e38 -> 0x681820 leaves
 *        it); Fill#0 with none leaves every element object and its content
 *        and returns the length (0x648120); Realloc#1(3, none) leaves the
 *        old element and gives live new ones.
 *   SC18 An array emptied by Erase (this VM had no page then): Add, Add,
 *        Insert at 0 and PushBack each give a new copy, and Fill#0 then
 *        assigns every element in place. The element type belongs to the
 *        array object natively (0x646d70 reads it; Erase does not write it,
 *        see above).
 *   SC19 A value whose object members are -1 (alloc_struct, unfilled) and
 *        whose numbers follow them: the numbers are copied (port strategy
 *        above; natively the value's members are default objects). Fill#0
 *        of such a value into an element whose struct member is an object
 *        assigns the defaults into that same object.
 *   SC31 A value with no reference but the one handed over (as NEW or a
 *        function's result leaves it): the element is a new object; the
 *        value is gone after the call (CALLHLL releases it, 0x659d46), its
 *        destructor run once there, the element's when the array goes.
 *   SC32 The value is an element of the same array (A_REF(arr[0])): Fill#0
 *        leaves element 0's object and content as they were and gives
 *        element 1 that content, a wrap member then shared by both with one
 *        reference each (0x656f7c retains the new object before it releases
 *        the old one); Add makes a third, new copy.
 *   SC33 A struct of another member count into a typed array: Add makes a
 *        new object of the array's struct with its defaults, Fill#0 leaves
 *        the elements as they are (0x681843..0x681859); the value is not
 *        touched. A struct of the same member count keeps this VM's older
 *        handling (the handed value stored): natively 0x681820 would assign
 *        it member by member, which this VM does not do where the member
 *        kinds may differ (port strategy).
 *   SC35 array<string> and array<struct> members: the element's array is
 *        its own page; each string its own slot with equal bytes, each
 *        struct its own object with equal members; a change to them leaves
 *        the source's.
 *   SC36 Twenty Adds into one array (the page grows in place and by
 *        reallocation): every element is its own copy of its value.
 *   SC38 An array<string> member holding a page of another kind (a page of
 *        numbers with 0 in its cells, as an older resize leaves one; a
 *        generic page of strings): the copy is a new page of that kind with
 *        the same values, each one its teardown releases retained once more.
 *        A slot without a page (as variable_initval makes an array) is
 *        copied as a slot of its own without a page.
 *
 *   SC39 A struct with a wrap<T> member, T given a bare destructor for the
 *        case (lent from a struct that has one; this AIN has no such wrap
 *        shape of its own): Add runs no function at all; the element shares
 *        the value's wrapped object, which is destroyed once after both go.
 *        Also one level deeper (T's own wrap<U>, U given the destructor).
 *        Natively the new element's wrap slot is -1 (0x6569b8) and no other
 *        object is made; here the inner objects alloc_struct gave the new
 *        element go without their destructors (port strategy).
 *   SC40 The same for the other objects this VM makes for a copy, where
 *        this AIN has the shape (the kinds it has not are printed), the
 *        destructor lent as in SC39: an array member's new struct elements
 *        whose wraps hold an object with a destructor, and a struct member
 *        made for the copy holding such a wrap: Add runs no function;
 *        Fill#0 of a value whose struct member is -1, the element's member
 *        a struct with a destructor: that member object is kept and given
 *        the defaults through a default object made here, and no
 *        destructor runs. And an array member of
 *        one-slot wraps: the copy is a new page sharing each wrapped object
 *        with one more reference (0x6801d0 -> 0x656d60 type 82 -> 0x656f7c).
 *   SC44 Two structs on a cycle of array members, A {int, array<B>,
 *        array<B>} and B {int, array<A>} (synthetic, see below; B is met
 *        twice while A is checked): a value of A, then one of
 *        B, three levels deep, is copied at every level (each element and
 *        each nested array its own, the numbers equal; 0x656f45 -> 0x67ffb0
 *        -> 0x6801d0 -> 0x656d60), the source keeps its owner's reference
 *        only, nothing is left after.
 *   SC45 A struct holding an array of itself, C {int, array<C>}: the same.
 *   SC46-SC55, SC57 The delegate entries that point at the value: an
 *        object stored by value keeps the handlers bound to the value (the
 *        first and last steps of 0x681820, described before SC46 below).
 *        SC46 an outside delegate (Add, PushBack, Insert; the order of a
 *        call), SC47 an entry's environment, SC48 a delegate member bound
 *        to its own object, SC49 a struct member bound outside, SC50 an
 *        array member's struct element bound outside, SC51 an array of
 *        structs whose element is bound to the outer value, SC52 Fill (the
 *        element's own entries dropped; none and another struct change
 *        nothing), SC53 the order of the new entries, SC54 a store from a
 *        handler of the delegate, SC55 edges worked out from the steps,
 *        SC57 the game's EndEventCallbackCollection@Add and a frame's
 *        end-update events.
 *   SC58 EmplaceBack after Erase emptied the array: a new object of the
 *        array's struct, constructed (0x6476e0 calls Realloc).
 *   SC59 AddRange or Concat into a slot without a page, then Add: the
 *        element is a copy of the value's struct. Before v14 the page made
 *        there keeps what it held (a guard part).
 *   SC60 Struct members, worked out from the steps: a member's delegate
 *        bound to the outer value points at the element in the copy and
 *        stays in the value; Fill#0 into an element's member object drops
 *        its entries and gives it the value's member's; Fill#0 of a value
 *        whose member is the default object drops the entries of the
 *        element's member object.
 *   SC61 A member sharing an object (a wrap, a ref, an interface) whose
 *        delegate is bound to the value: the walk of the last step does not
 *        go into it, so the object's delegate keeps the value's entry and
 *        gets the element's (and its entry for itself), on Add and on
 *        Fill#0 of a value sharing the element's object.
 *   SC62 The older handling's copies (version 13, and in v14 a struct
 *        with a long int member): Fill#0 replaces the element by a copy
 *        with no entry (a guard part, this branch's handling).
 *   SC63 Fill#0 into an element's member object: its own entries go (one
 *        only it had, its entry in its own delegate), it gets the value's
 *        member's; the value's member the default object leaves none.
 *   SC64 Three levels, the middle object bound in its member's delegate
 *        and in its own: each level's walk goes into struct members.
 *   SC65 Realloc#1 with a value: the new tail gets the value's entries.
 *   SC66 Delegates in the value's list that no longer point at it (an
 *        entry taken out, a freed slot used again) change nothing.
 *   SC67 A delegate member's entries with environments: the copy and the
 *        three steps keep each environment's references balanced (one
 *        more per entry while it is there, back to its own at the end).
 *   SC68 An array member of wraps of a delegate: the walk of the last
 *        step does not go through the wrap, the wrapped delegate is shared
 *        and gets the element's entry.
 *   SC69 Fill while a script run by the first step changes the array (a
 *        push that moves its page, a shorter Realloc, the array let go):
 *        each round reads the array again.
 *   SC70 Fill while a destructor run by the release of what an element gave
 *        up moves the array's page: the array's slot keeps the page the
 *        array has after the call.
 *   SC71 Fill#1 while a script run by the first step makes the array
 *        shorter than the range: the round past the end is left out (this
 *        branch's expectation, see SC71).
 *   SC72 Fill over an element without an object after a script moved the
 *        array's page: the new object goes into the present page (the -1
 *        element is this VM's, see SC72).
 * Guards (pass before and after):
 *   SC20 wrap elements (type operand 0x10002): PushBack and Insert store the
 *        object itself with one more reference (0x646d70 type 82 ->
 *        0x646f66 shares).
 *   SC21 interface elements (0x10003): object and offset stored, shared.
 *   SC22 plain values (operand 1): a number equal to a live slot is stored
 *        as a number and its slot is not touched.
 *   SC23 strings (operand 2) in array<string>: this branch's rule (the
 *        array shares the slot, one more reference). Not the original's.
 *   SC24 a struct of plain numbers through the real A_REF (which copies it
 *        here, vm.c v14_struct_is_plain_value): the element is not the
 *        source and nothing leaks.
 *   SC25 versions 13 and 11: PushBack(2), Add(2) and Insert(2) of a struct
 *        keep this branch's v11-v13 rule (the page itself, nothing given
 *        back; HP7); Fill#0, Fill#1 and Realloc#1 keep theirs (the element
 *        replaced by a copy, the new tail a copy); Erase to empty leaves no
 *        page.
 *   SC26 the string member bytes (an SJIS and a GBK double-byte sequence)
 *        come out byte for byte in both probe charsets; the copy has no
 *        charset branch natively (0x646d70, 0x656d60).
 *   SC27 positions: Insert at 0, at the end and clamped indices, and Add
 *        into an empty typed array: count, order and the inserted
 *        element's numbers.
 *   SC28 none into an array without a page, a generic page, or a page of
 *        numbers (no element type known here): stored as -1, nothing made.
 *        A v14 number array emptied by Erase keeps this branch's rule: no
 *        page (only an array of value structs keeps its page).
 *   SC29 a wrap box (STRUCT_PAGE with index -1) with operand 2: stored as
 *        it is, one more reference (an unknown shape keeps its handling).
 *   SC30 empty arrays: Fill#0 returns 0, Fill#1 on an empty array returns
 *        0, Realloc#1(0, value) stays empty; nothing is made or released.
 *   SC34 Copy (all) and AddRange of a typed array of value structs keep
 *        this branch's handling (natively 0x648210 -> 0x6470e0, which also
 *        assigns a struct element, type 13 -> 0x64719c; outside this
 *        change).
 *   SC37 A struct with a member the original cannot assign (0x656fc8: a
 *        long int, type 91, 100, the function type) or this VM does not copy
 *        (an option of a reference payload, a nested array of rank > 1 or of
 *        delegates, arrays, options or two-slot elements), directly or in
 *        the structs of an array member: two Adds keep the older handling.
 *        Also one struct for each such kind this AIN has (the kinds it does
 *        not have are printed).
 *   SC41 A struct with a member of type 87 or a ref enum (two slots, the
 *        second a void member; synthetic): natively shared like a wrap
 *        (0x656f7c), but this VM's teardown releases neither type, so a copy
 *        could not keep their references balanced: two Adds keep the older
 *        handling.
 *   SC42 Two structs on a cycle of array members, one of them with a long
 *        int member: A {int, array<B>, array<D>, long int}, B {int,
 *        array<A>} and D {int} (synthetic; D is checked after B, before A's
 *        long int). B reaches the long int through A, so neither is
 *        copied: two Adds of A and then two of B keep the older handling.
 *   SC43 The same, B stored first.
 *   SC56 A struct this VM does not copy (a long int member) and v13 keep
 *        the delegate entries for the value (the element is the value). So
 *        does SC51's struct with an array of delegates.
 * The synthetic structs (SC41-SC45) are appended to a copy of the AIN in
 * memory that is the current AIN for the case only; SC42 also prints how
 * many cycles of struct and array<struct> members this AIN has and its
 * longest chain of them.
 * Where the expectations are this branch's, not the original's: SC23, SC25,
 * SC28, SC29, SC34, SC37, SC41-SC43 and SC56, SC33's same member count and
 * SC51's array of delegates (guards of the present handling or of this
 * VM's limits); SC52's value that is an element of the array (this VM
 * skips both steps there, see SC52); SC54 (natively the value a store is
 * handed is A_REF's temporary copy, which goes before the handler returns:
 * the removal of its entry lowers the limit of the call in progress,
 * 0x652330, so that call does not reach y; here A_REF shares, outside
 * this change); SC62 (guards of the older handling); SC69's shorter array
 * and released array (memory safety: the round stops; what the original's
 * element access does past the end was not read); SC38 (pages of another kind,
 * port strategy), SC39 and SC40's objects only this VM makes, and SC4's -1
 * element (fix cases resting on this VM's representation); SC24's
 * count of copies (here A_REF already copies a plain struct); SC8's and
 * SC31's destructor counts (natively A_REF copies every struct, a third
 * object with its own destructor; here it shares, which is outside this
 * change). The entries and orders of SC46-SC55, SC60, SC61 and SC63-SC68 are
 * worked out from the disassembly, not seen run natively, and so is SC69's
 * longer array (0x648120); SC58's constructor rests on the flag
 * Realloc gives its new tail (0x67f4d0).
 * Not covered: Copy, Concat, AddRange and Duplicate as copies (natively
 * 0x6470e0; SC34 guards their present handling); EmplaceBack (no value)
 * other than after Erase (SC58); the delegate steps of a struct assigned by
 * SR_ASSIGN, Copy and its family or A_REF's copy;
 * A_REF's own copy of a struct (FrameInfoCache::Get); X_ASSIGN and X_SET
 * of struct values; a member type the original cannot assign (0x656d60
 * fails at a long int, type 91 or 100); an option of a reference payload
 * (0x656da8; this VM keeps the older handling for such a struct); a nested
 * array of rank > 1, or of delegates, arrays, options or two-slot elements
 * (the older handling too); ref int, float, string, bool, delegate
 * and array members (0x656f7c like ref struct; this AIN's shapes were not
 * checked here); a wrap<struct> nested in a struct member (no struct of
 * this AIN has one in a small enough shape; SC16 reaches the same rule
 * through an array member); a struct whose first member is a wrap of a
 * struct (a base here, resolve_struct_member); the original's assignment
 * of a value of another struct with the same member count (SC33 guards
 * this VM's older handling); a destructor that changes the array during
 * a store; nested arrays and delegates as elements (natively 0x646d70
 * reports them as types it cannot copy, 0x646fb8); the lower-case
 * Pushback.
 *
 * The structs are picked at run time by the shape of their members (the
 * fewest members, at most 64, then the lowest index; a destructor only if
 * its body is a bare RETURN) and printed with their index; the cases do not
 * depend on which struct of that shape it is.
 *
 * Every case runs in its own child (forked_cases.inc). Exit code 1 is a
 * failed check, 2 the environment (the AIN lacks a struct shape a case
 * looks for).
 */

#define SC_CHECK XC_CHECK

enum sc_kind { SC_NUM, SC_STR, SC_STRUCT, SC_ARRAY, SC_DG, SC_WRAP, SC_IFACE, SC_REF, SC_OPTION, SC_VOID, SC_OTHER };

static enum sc_kind sc_kind_of(const struct ain_type *t)
{
	switch (t->data) {
	case AIN_INT: case AIN_FLOAT: case AIN_BOOL: case AIN_ENUM:
		return SC_NUM;
	case AIN_STRING:
		return SC_STR;
	case AIN_STRUCT:
		return SC_STRUCT;
	case AIN_ARRAY:
		return SC_ARRAY;
	case AIN_DELEGATE:
		return SC_DG;
	case AIN_WRAP:
		return SC_WRAP;
	case AIN_IFACE:
		return SC_IFACE;
	case AIN_REF_STRUCT:
		return SC_REF;
	case AIN_OPTION:
		return SC_OPTION;
	case AIN_VOID:
		return SC_VOID;
	default:
		return SC_OTHER;
	}
}

/* No destructor, or one whose body is a bare RETURN. */
static bool sc_quiet_dtor(int sno)
{
	int d = ain->structures[sno].destructor;
	if (d <= 0)
		return true;
	return d < ain->nr_functions && ain->functions[d].address + 2 <= ain->code_size
	    && get_opcode(ain->functions[d].address) == RETURN;
}

/* A struct whose members are numbers and strings only, with at least one
 * number (the struct of a struct member). */
static bool sc_flat(int sno)
{
	if (sno < 0 || sno >= ain->nr_structures)
		return false;
	struct ain_struct *s = &ain->structures[sno];
	int nums = 0;
	if (s->nr_members <= 0 || !sc_quiet_dtor(sno))
		return false;
	for (int i = 0; i < s->nr_members; i++) {
		enum sc_kind k = sc_kind_of(&s->members[i].type);
		if (k == SC_NUM)
			nums++;
		else if (k != SC_STR)
			return false;
	}
	return nums > 0;
}

/* The struct the case kind k uses: every member a number, a string, a void
 * (the second slot of an interface) or of kind k; at least one of kind k
 * (a wrap only of a struct and not as the first member; a struct member
 * only of a flat struct; an array only of int); a number when want_num;
 * a quiet destructor. The one with the fewest members, the lowest index
 * among those. */
static int sc_pick(enum sc_kind k, bool want_num, bool want_str, const char *what)
{
	int best = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		/* at most 64 members: the reference tables below hold 64 */
		if (s->nr_members <= 0 || s->nr_members > 64 || !s->name || !sc_quiet_dtor(i))
			continue;
		int nk = 0, nn = 0, ns = 0;
		bool ok = true;
		for (int m = 0; m < s->nr_members && ok; m++) {
			const struct ain_type *t = &s->members[m].type;
			enum sc_kind mk = sc_kind_of(t);
			if (mk == SC_NUM) { nn++; continue; }
			if (mk == SC_STR && k != SC_STR) { ns++; continue; }
			if (mk == SC_VOID && k == SC_IFACE) continue;
			if (mk != k) { ok = false; break; }
			switch (k) {
			case SC_STR: ns++; break;
			case SC_STRUCT: ok = sc_flat(t->struc); break;
			case SC_ARRAY: ok = t->array_type && t->array_type->data == AIN_INT && t->rank <= 1
					&& s->members[m].name && strcmp(s->members[m].name, "<vtable>"); break;
			case SC_WRAP: ok = m > 0 && t->array_type && t->array_type->data == AIN_STRUCT
					&& t->struc >= 0 && sc_flat(t->struc); break;
			case SC_IFACE: ok = m + 1 < s->nr_members && s->members[m + 1].type.data == AIN_VOID; break;
			case SC_REF: ok = t->struc >= 0 && t->struc < ain->nr_structures; break;
			default: break;
			}
			nk++;
		}
		if (!ok || (k == SC_STR ? ns == 0 : nk == 0) || (want_num && nn == 0) || (want_str && ns == 0))
			continue;
		if (best < 0 || s->nr_members < ain->structures[best].nr_members)
			best = i;
	}
	if (best < 0) {
		printf("  ENVIRONMENT: no struct for %s\n", what);
		xc_env_fail("struct shape");
	}
	printf("  %s: struct %d (%d members, constructor %d, destructor %d)\n", what, best,
	       ain->structures[best].nr_members, ain->structures[best].constructor, ain->structures[best].destructor);
	return best;
}

/* A struct of numbers only (fewest members, lowest index). */
static int sc_pick_plain(void)
{
	int best = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		bool ok = s->nr_members > 0 && s->nr_members <= 64 && s->name && sc_quiet_dtor(i);
		for (int m = 0; ok && m < s->nr_members; m++)
			ok = sc_kind_of(&s->members[m].type) == SC_NUM;
		if (ok && (best < 0 || s->nr_members < ain->structures[best].nr_members))
			best = i;
	}
	if (best < 0)
		xc_env_fail("no struct of numbers");
	printf("  a struct of numbers only: struct %d (%d members)\n", best, ain->structures[best].nr_members);
	return best;
}

static int sc_first(int sno, enum sc_kind k)
{
	struct ain_struct *s = &ain->structures[sno];
	for (int i = 0; i < s->nr_members; i++)
		if (sc_kind_of(&s->members[i].type) == k)
			return i;
	xc_env_fail("member");
	return -1;
}

static struct page *sc_page(int slot)
{
	return slot > 0 && heap_index_valid(slot) && heap[slot].type == VM_PAGE ? heap[slot].page : NULL;
}

static bool sc_same_text(int a, int b)
{
	if (a <= 0 || b <= 0 || !heap_index_valid(a) || !heap_index_valid(b)
	    || heap[a].type != VM_STRING || heap[b].type != VM_STRING || !heap[a].s || !heap[b].s)
		return false;
	return heap[a].s->size == heap[b].s->size && !memcmp(heap[a].s->text, heap[b].s->text, heap[a].s->size);
}

static bool sc_text_is(int a, const char *t, int n)
{
	return a > 0 && heap_index_valid(a) && heap[a].type == VM_STRING && heap[a].s
	    && heap[a].s->size == n && !memcmp(heap[a].s->text, t, n);
}

static void sc_set_text(struct page *p, int m, const char *t, int n)
{
	int old = p->values[m].i;
	p->values[m].i = sf_str(t, n);
	if (old > 0 && heap_index_valid(old) && HEAP_REF(old) > 0)
		heap_unref(old);
}

/* Synthetic member values: numbers base + 7 * member (a float member gets
 * that as its bits), strings "m<member>-<base>" (SC26 sets its own bytes).
 * Elements already in an array use base 1000, the stored values 2000. */
static void sc_fill_base(int obj, int base)
{
	struct page *p = sc_page(obj);
	struct ain_struct *s = &ain->structures[p->index];
	for (int m = 0; m < s->nr_members && m < p->nr_vars; m++) {
		enum sc_kind k = sc_kind_of(&s->members[m].type);
		if (k == SC_NUM) {
			p->values[m].i = base + 7 * m;
		} else if (k == SC_STR) {
			char t[16];
			int n = snprintf(t, sizeof t, "m%d-%d", m, base);
			sc_set_text(p, m, t, n);
			p = sc_page(obj);
		}
	}
}

static void sc_fill_flat(int obj) { sc_fill_base(obj, 1000); }

/* A new object of sno with every member set (a struct member made and
 * filled when it is none; an array member [7, 8]; a delegate member one
 * entry for `target`; a wrap, interface or reference member `target`, one
 * reference each). The caller owns one reference. */
static int sc_new(int sno, int target)
{
	int obj = alloc_struct(sno);
	sc_fill_base(obj, 2000);
	struct ain_struct *s = &ain->structures[sno];
	for (int m = 0; m < s->nr_members; m++) {
		struct page *p = sc_page(obj);
		const struct ain_type *t = &s->members[m].type;
		switch (sc_kind_of(t)) {
		case SC_STRUCT: {
			int c = p->values[m].i;
			if (!sc_page(c)) {
				c = alloc_struct(t->struc);
				sc_page(obj)->values[m].i = c;
			}
			sc_fill_base(c, 2000);
			break;
		}
		case SC_ARRAY: {
			union vm_value dim = { .i = 2 };
			int a = heap_alloc_page(alloc_array(1, &dim, AIN_ARRAY_INT, -1, false));
			heap[a].page->values[0].i = 7;
			heap[a].page->values[1].i = 8;
			int old = sc_page(obj)->values[m].i;
			sc_page(obj)->values[m].i = a;
			if (old > 0 && heap_index_valid(old) && HEAP_REF(old) > 0)
				heap_unref(old);
			break;
		}
		case SC_DG: {
			int d = heap_alloc_page(delegate_new_from_method(target, ain->structures[sno].constructor > 0
				? ain->structures[sno].constructor : 1));
			int old = sc_page(obj)->values[m].i;
			sc_page(obj)->values[m].i = d;
			if (old > 0 && heap_index_valid(old) && HEAP_REF(old) > 0)
				heap_unref(old);
			break;
		}
		case SC_WRAP: {
			int w = p->values[m].i;
			if (sc_page(w) && heap[w].page->type == STRUCT_PAGE && heap[w].page->index >= 0) {
				sc_fill_base(w, 2000);
			} else {
				int c = alloc_struct(t->struc);
				sc_fill_base(c, 2000);
				sc_page(obj)->values[m].i = c;
				if (w > 0 && heap_index_valid(w) && HEAP_REF(w) > 0)
					heap_unref(w);
			}
			break;
		}
		case SC_IFACE:
		case SC_REF: {
			int old = p->values[m].i;
			heap_ref(target);
			sc_page(obj)->values[m].i = target;
			if (sc_kind_of(t) == SC_IFACE)
				sc_page(obj)->values[m + 1].i = 3;
			if (old > 0 && heap_index_valid(old) && HEAP_REF(old) > 0)
				heap_unref(old);
			break;
		}
		default:
			break;
		}
	}
	return obj;
}

static int sc_flat_struct(void);

static unsigned long sc_dtor_calls(int sno)
{
	int d = ain->structures[sno].destructor;
	return d > 0 && d < 40000 ? calls[d] : 0;
}

/* The value the bytecode ".LOCALREF x; A_REF" hands to the call. */
static int sc_handed(int obj)
{
	int v = hp_aref(obj);
	if (v <= 0)
		xc_env_fail("A_REF gave none");
	return v;
}

/* Compares element e with source src member by member (the member rules of
 * 0x656d60, one level) and reports what is wrong; shared_refs_before[m],
 * when given, is the reference count member m's shared object had before
 * the store: it must have one more while the element lives. */
static void sc_compare(const char *what, int e, int src, const int *shared_refs_before)
{
	struct page *pe = sc_page(e), *ps = sc_page(src);
	SC_CHECK(pe && ps && e != src, "%s: the element is %s (slot %d, source %d)", what,
		 !pe ? "not a live page" : e == src ? "the source object itself" : "?", e, src);
	if (!pe || !ps || e == src)
		return;
	SC_CHECK(pe->type == STRUCT_PAGE && pe->index == ps->index && pe->nr_vars == ps->nr_vars,
		 "%s: the element is struct %d with %d slots, the source struct %d with %d", what, pe->index, pe->nr_vars,
		 ps->index, ps->nr_vars);
	if (pe->index != ps->index || pe->nr_vars != ps->nr_vars)
		return;
	struct ain_struct *s = &ain->structures[ps->index];
	for (int m = 0; m < s->nr_members && m < ps->nr_vars; m++) {
		const struct ain_type *t = &s->members[m].type;
		int a = pe->values[m].i, b = ps->values[m].i;
		switch (sc_kind_of(t)) {
		case SC_NUM:
			SC_CHECK(a == b, "%s: number member %d is %d, the source's %d", what, m, a, b);
			break;
		case SC_STR:
			SC_CHECK(a != b && sc_same_text(a, b), "%s: string member %d is slot %d, the source's %d (%s)", what, m, a, b,
				 a == b ? "the same slot" : sc_same_text(a, b) ? "equal text" : "different text");
			break;
		case SC_STRUCT: {
			struct page *ca = sc_page(a), *cb = sc_page(b);
			SC_CHECK(ca && cb && a != b, "%s: struct member %d is slot %d, the source's %d", what, m, a, b);
			if (ca && cb && a != b) {
				for (int k = 0; k < cb->nr_vars && k < ca->nr_vars; k++) {
					enum sc_kind ck = sc_kind_of(&ain->structures[cb->index].members[k].type);
					if (ck == SC_NUM)
						SC_CHECK(ca->values[k].i == cb->values[k].i, "%s: member %d.%d is %d, the source's %d", what, m,
							 k, ca->values[k].i, cb->values[k].i);
					else if (ck == SC_STR)
						SC_CHECK(ca->values[k].i != cb->values[k].i && sc_same_text(ca->values[k].i, cb->values[k].i),
							 "%s: string member %d.%d is not its own equal string", what, m, k);
				}
			}
			break;
		}
		case SC_ARRAY: {
			struct page *ca = sc_page(a), *cb = sc_page(b);
			bool eq = ca && cb && ca->nr_vars == cb->nr_vars;
			for (int k = 0; eq && k < cb->nr_vars; k++)
				eq = ca->values[k].i == cb->values[k].i;
			SC_CHECK(a != b && eq, "%s: array member %d is slot %d (%d elements), the source's %d (%d)", what, m, a,
				 ca ? ca->nr_vars : -1, b, cb ? cb->nr_vars : -1);
			break;
		}
		case SC_DG: {
			struct page *ca = sc_page(a), *cb = sc_page(b);
			int oa = -1, fa = -1, ob = -2, fb = -2;
			bool ga = ca && ca->type == DELEGATE_PAGE && delegate_get(ca, 0, &oa, &fa);
			bool gb = cb && cb->type == DELEGATE_PAGE && delegate_get(cb, 0, &ob, &fb);
			SC_CHECK(a != b && ga && gb && oa == ob && fa == fb && delegate_numof(ca) == delegate_numof(cb),
				 "%s: delegate member %d is slot %d, the source's %d (entries %d/%d)", what, m, a, b,
				 ca ? delegate_numof(ca) : -1, cb ? delegate_numof(cb) : -1);
			break;
		}
		case SC_WRAP:
		case SC_IFACE:
		case SC_REF:
			SC_CHECK(a == b, "%s: %s member %d is slot %d, the source's %d: not shared", what,
				 sc_kind_of(t) == SC_WRAP ? "wrap" : sc_kind_of(t) == SC_IFACE ? "interface" : "reference", m, a, b);
			if (sc_kind_of(t) == SC_IFACE)
				SC_CHECK(pe->values[m + 1].i == ps->values[m + 1].i, "%s: interface offset %d, the source's %d", what,
					 pe->values[m + 1].i, ps->values[m + 1].i);
			if (a == b && shared_refs_before && b > 0)
				SC_CHECK(HEAP_REF(b) == shared_refs_before[m] + 1, "%s: the shared object of member %d has %d reference(s), "
					 "want %d (one more, for the element)", what, m, HEAP_REF(b), shared_refs_before[m] + 1);
			break;
		default:
			break;
		}
	}
}

/* Changes element e and checks src, then changes src and checks e. */
static void sc_independent(const char *what, int e, int src)
{
	struct page *pe = sc_page(e), *ps = sc_page(src);
	if (!pe || !ps || e == src) {
		SC_CHECK(false, "%s: no separate element to change", what);
		return;
	}
	struct ain_struct *s = &ain->structures[ps->index];
	for (int m = 0; m < s->nr_members; m++) {
		const struct ain_type *t = &s->members[m].type;
		pe = sc_page(e);
		ps = sc_page(src);
		switch (sc_kind_of(t)) {
		case SC_NUM: {
			int was = ps->values[m].i;
			pe->values[m].i = was + 500;
			SC_CHECK(ps->values[m].i == was, "%s: writing the element's number %d changed the source's to %d", what, m,
				 ps->values[m].i);
			ps->values[m].i = was - 500;
			SC_CHECK(pe->values[m].i == was + 500, "%s: writing the source's number %d changed the element's to %d", what,
				 m, pe->values[m].i);
			ps->values[m].i = was;
			break;
		}
		case SC_STR: {
			int sv = ps->values[m].i;
			char keep[64];
			int n = heap_index_valid(sv) && heap[sv].s && heap[sv].s->size < 64 ? heap[sv].s->size : 0;
			if (n)
				memcpy(keep, heap[sv].s->text, n);
			int ev = pe->values[m].i;
			if (ev > 0 && heap_index_valid(ev) && heap[ev].type == VM_STRING) {
				/* a change in place (S_PUSHBACK2) */
				string_push_back(&heap[ev].s, 'x');
				SC_CHECK(sc_text_is(ps->values[m].i, keep, n), "%s: a change in place to the element's string %d changed "
					 "the source's", what, m);
				/* S_ASSIGN into the element's member: a new string in its slot */
				struct string *old = heap[ev].s;
				heap[ev].s = make_string("changed", 7);
				if (old)
					free_string(old);
			}
			SC_CHECK(sc_text_is(ps->values[m].i, keep, n), "%s: a new string in the element's member %d changed the source's",
				 what, m);
			break;
		}
		case SC_STRUCT: {
			struct page *ca = sc_page(pe->values[m].i), *cb = sc_page(ps->values[m].i);
			if (!ca || !cb || ca == cb) {
				SC_CHECK(false, "%s: struct member %d is not separate", what, m);
				break;
			}
			for (int k = 0; k < cb->nr_vars && k < ca->nr_vars; k++) {
				if (sc_kind_of(&ain->structures[cb->index].members[k].type) != SC_NUM)
					continue;
				int was = cb->values[k].i;
				ca->values[k].i = was + 77;
				SC_CHECK(cb->values[k].i == was, "%s: writing member %d.%d of the element changed the source's", what, m, k);
				break;
			}
			break;
		}
		case SC_ARRAY: {
			struct page *ca = sc_page(pe->values[m].i), *cb = sc_page(ps->values[m].i);
			if (!ca || !cb || ca == cb) {
				SC_CHECK(false, "%s: array member %d is not separate", what, m);
				break;
			}
			int was = cb->nr_vars ? cb->values[0].i : 0;
			if (ca->nr_vars)
				ca->values[0].i = was + 11;
			SC_CHECK(!cb->nr_vars || cb->values[0].i == was, "%s: writing the element's array member %d changed the source's",
				 what, m);
			break;
		}
		case SC_DG: {
			struct page *ca = sc_page(pe->values[m].i), *cb = sc_page(ps->values[m].i);
			if (!ca || !cb || ca == cb) {
				SC_CHECK(false, "%s: delegate member %d is not separate", what, m);
				break;
			}
			int had = delegate_numof(cb);
			delegate_clear(ca);
			SC_CHECK(delegate_numof(sc_page(ps->values[m].i)) == had && had == 1,
				 "%s: clearing the element's delegate %d left the source's with %d entries (had %d)", what, m,
				 delegate_numof(sc_page(ps->values[m].i)), had);
			break;
		}
		default:
			break;
		}
	}
}

/* -1 is this VM's default string member (init_struct_slot); natively a new
 * empty string (0x6569fc). */
static bool sc_default_text(int a)
{
	return a == -1 || sc_text_is(a, "", 0);
}

/* --- deep comparison (every level, the member rules of 0x656d60) --- */

static void sc_deep_struct(const char *what, int e, int src, int depth);

/* The pairs of objects (the element's, the source's) on the way down through
 * struct members, from the last array element (or the top) on. A struct
 * assignment points the target's own delegates (its delegate members and
 * those of its struct members, at any depth, not into an array of structs)
 * at the target where they pointed at the value (0x6818f0 -> 0x682080 ->
 * 0x682330), at each level; so an entry of the source for one of these
 * source objects is, in the copy, an entry for the copy's object. */
static struct { int e, src; } sc_deep_pair[16];
static int sc_deep_npairs;

static int sc_deep_counterpart(int ob)
{
	for (int i = sc_deep_npairs; i-- > 0;)
		if (sc_deep_pair[i].src == ob)
			return sc_deep_pair[i].e;
	return ob;
}

/* A value a of the element against b of the source, declared type t. */
static void sc_deep_value(const char *what, int a, int b, const struct ain_type *t, int depth)
{
	if (depth > 8)
		return;
	switch (sc_kind_of(t)) {
	case SC_NUM:
		SC_CHECK(a == b, "%s: number %d, the source's %d", what, a, b);
		break;
	case SC_STR:
		if (b <= 1)	/* no string here (-1, or the reserved 0 an older resize leaves): kept */
			SC_CHECK(a == b, "%s: slot %d for the source's %d", what, a, b);
		else if (sc_text_is(b, "", 0))
			SC_CHECK(a != b, "%s: an empty string shared (slot %d)", what, a);
		else
			SC_CHECK(a != b && sc_same_text(a, b), "%s: string slot %d, the source's %d (%s)", what, a, b,
				 a == b ? "the same slot" : "not equal text");
		break;
	case SC_STRUCT:
		if (!sc_page(b))
			break;
		SC_CHECK(a != b && sc_page(a), "%s: struct slot %d, the source's %d", what, a, b);
		if (a != b && sc_page(a))
			sc_deep_struct(what, a, b, depth + 1);
		break;
	case SC_ARRAY: {
		if (b == -1 || !heap_index_valid(b) || heap[b].type != VM_PAGE)
			break;
		struct page *pa = a > 0 && heap_index_valid(a) && heap[a].type == VM_PAGE ? heap[a].page : NULL;
		struct page *pb = heap[b].page;
		SC_CHECK(a != b, "%s: the array is the source's own (slot %d)", what, b);
		if (!pb || a == b)
			break;
		SC_CHECK(pa && pa->nr_vars == pb->nr_vars, "%s: array of %d element(s), the source's %d", what,
			 pa ? pa->nr_vars : -1, pb->nr_vars);
		if (!pa || pa->nr_vars != pb->nr_vars)
			break;
		/* each element is a level of its own (0x6801d0 -> 0x681820), which
		 * the outer levels' walk does not enter */
		int npairs = sc_deep_npairs;
		sc_deep_npairs = 0;
		for (int i = 0; i < pb->nr_vars; i++) {
			char w[160];
			snprintf(w, sizeof w, "%s[%d]", what, i);
			sc_deep_value(w, pa->values[i].i, pb->values[i].i, t->array_type, depth + 1);
		}
		sc_deep_npairs = npairs;
		break;
	}
	case SC_DG: {
		struct page *ca = sc_page(a), *cb = sc_page(b);
		if (!cb || cb->type != DELEGATE_PAGE || delegate_numof(cb) == 0)
			break;
		SC_CHECK(a != b && ca && ca->type == DELEGATE_PAGE && delegate_numof(ca) == delegate_numof(cb),
			 "%s: delegate slot %d, the source's %d", what, a, b);
		for (int k = 0; a != b && ca && ca->type == DELEGATE_PAGE && k < delegate_numof(cb) && k < delegate_numof(ca); k++) {
			int oa = -1, fa = -1, ob = -2, fb = -2;
			delegate_get(ca, k, &oa, &fa);
			delegate_get(cb, k, &ob, &fb);
			SC_CHECK(oa == sc_deep_counterpart(ob) && fa == fb, "%s: delegate entry %d differs", what, k);
		}
		break;
	}
	case SC_WRAP:
	case SC_IFACE:
	case SC_REF:
		SC_CHECK(a == b, "%s: slot %d, the source's %d: not shared", what, a, b);
		break;
	default:
		break;
	}
}

static void sc_deep_struct(const char *what, int e, int src, int depth)
{
	struct page *pe = sc_page(e), *ps = sc_page(src);
	if (!pe || !ps || pe->index != ps->index || pe->nr_vars != ps->nr_vars || ps->index < 0) {
		SC_CHECK(false, "%s: struct %d (%d slots), the source's %d (%d)", what, pe ? pe->index : -1, pe ? pe->nr_vars : -1,
			 ps ? ps->index : -1, ps ? ps->nr_vars : -1);
		return;
	}
	struct ain_struct *s = &ain->structures[ps->index];
	if (depth == 0)
		sc_deep_npairs = 0;
	int npairs = sc_deep_npairs;
	if (npairs < 16) {
		sc_deep_pair[npairs].e = e;
		sc_deep_pair[npairs].src = src;
		sc_deep_npairs = npairs + 1;
	}
	for (int m = 0; m < s->nr_members && m < ps->nr_vars; m++) {
		const struct ain_type *t = &s->members[m].type;
		char w[160];
		snprintf(w, sizeof w, "%s.%s", what, s->members[m].name ? s->members[m].name : "?");
		if (sc_kind_of(t) == SC_OPTION) {
			int slots = ss_type_slot_count(t);
			for (int k = 0; k < slots && m + k < ps->nr_vars; k++)
				SC_CHECK(sc_page(e)->values[m + k].i == sc_page(src)->values[m + k].i, "%s: option slot %d is %d, the source's %d",
					 w, k, sc_page(e)->values[m + k].i, sc_page(src)->values[m + k].i);
			continue;
		}
		if (sc_kind_of(t) == SC_IFACE && m + 1 < ps->nr_vars)
			SC_CHECK(sc_page(e)->values[m + 1].i == sc_page(src)->values[m + 1].i, "%s: interface offset differs", w);
		sc_deep_value(w, sc_page(e)->values[m].i, sc_page(src)->values[m].i, t, depth);
	}
	sc_deep_npairs = npairs;
}

/* Every function's entries so far (the probe counts each call). */
static unsigned long sc_calls_total(void)
{
	unsigned long n = 0;
	for (int i = 0; i < ain->nr_functions && i < 40000; i++)
		n += calls[i];
	return n;
}

/* The constructors' entries so far. */
static unsigned long sc_ctor_total(void)
{
	unsigned long n = 0;
	for (int i = 0; i < ain->nr_structures; i++) {
		int c = ain->structures[i].constructor;
		if (c > 0 && c < 40000)
			n += calls[c];
	}
	return n;
}

/* A new array<string> page of n strings "<tag><i>". */
static int sc_str_array(int n, const char *tag)
{
	union vm_value dim = { .i = n };
	int a = heap_alloc_page(alloc_array(1, &dim, AIN_ARRAY_STRING, -1, false));
	for (int i = 0; i < n; i++) {
		char t[32];
		int k = snprintf(t, sizeof t, "%s%d", tag, i);
		int old = heap[a].page->values[i].i;
		int s = sf_str(t, k);
		heap[a].page->values[i].i = s;
		if (old > 0 && heap_index_valid(old) && HEAP_REF(old) > 0)
			heap_unref(old);
	}
	return a;
}

/* Puts the slot v into member m of obj, letting go of what was there
 * (quietly: an inner object alloc_struct made and no script has seen). */
static void sc_put_member(int obj, int m, int v)
{
	int old = sc_page(obj)->values[m].i;
	sc_page(obj)->values[m].i = v;
	if (old > 0 && heap_index_valid(old) && HEAP_REF(old) > 0)
		exit_unref(old);
}

/* Member m of obj as an object of its struct, made when it is -1. */
static int sc_member_obj(int obj, int m)
{
	const struct ain_type *t = &ain->structures[sc_page(obj)->index].members[m].type;
	int v = sc_page(obj)->values[m].i;
	if (!sc_page(v)) {
		v = alloc_struct(t->struc);
		sc_put_member(obj, m, v);
	}
	return v;
}


/* The shared member objects' reference counts of src, by member index. */
static void sc_shared_refs(int src, int *refs, int n)
{
	struct page *p = sc_page(src);
	struct ain_struct *s = &ain->structures[p->index];
	for (int m = 0; m < n; m++)
		refs[m] = 0;
	for (int m = 0; m < s->nr_members && m < n; m++) {
		enum sc_kind k = sc_kind_of(&s->members[m].type);
		int v = p->values[m].i;
		if ((k == SC_WRAP || k == SC_IFACE || k == SC_REF) && v > 0 && heap_index_valid(v))
			refs[m] = HEAP_REF(v);
	}
}

/* One store of A_REF(src) into a fresh typed array; checks the element as
 * the original makes it (a new object, the member rules, independence),
 * the references and the destructors; releases everything and checks
 * that nothing is left. how: "Add", "PushBack" or "Insert". */
static void sc_store_one(const char *what, int sno, const char *how)
{
	size_t live0 = live_slots();
	int target = alloc_struct(sc_flat_struct());
	int src = sc_new(sno, target);
	uint32_t sseq = heap[src].seq;
	int refs[64];
	sc_shared_refs(src, refs, 64);
	int arr = hp_array(AIN_ARRAY_STRUCT, sno);
	unsigned long d0 = sc_dtor_calls(sno);
	int ctor = ain->structures[sno].constructor;
	unsigned long c0 = ctor > 0 && ctor < 40000 ? calls[ctor] : 0;
	int v = sc_handed(src);
	/* the shared member objects, to follow their references */
	int sh[64];
	uint32_t shseq[64];
	for (int m = 0; m < 64; m++) {
		sh[m] = refs[m] > 0 ? sc_page(src)->values[m].i : 0;
		shseq[m] = sh[m] > 0 ? heap[sh[m]].seq : 0;
	}
	unsigned long call0 = sc_calls_total();
	if (!strcmp(how, "Insert"))
		hp_hll("Insert", 0, 2, 3, (int[]){ arr, 0, v });
	else
		hp_hll(how, 0, 2, 2, (int[]){ arr, v });
	/* no constructor, nor anything else: 0x679b30(struct, 0) and the
	 * native assignment */
	SC_CHECK(sc_calls_total() == call0, "%s: %lu function(s) ran during Array.%s", what, sc_calls_total() - call0, how);
	SC_CHECK(hp_numof(arr) == 1, "%s: Array.%s left %d element(s)", what, how, hp_numof(arr));
	int e = hp_elem(arr, 0);
	SC_CHECK(hp_refs(src, sseq) == 1, "%s: after Array.%s the source has %d reference(s), want its owner's 1", what, how,
		 hp_refs(src, sseq));
	sc_compare(what, e, src, refs);
	if (e != src && sc_page(e)) {
		SC_CHECK(HEAP_REF(e) == 1, "%s: the element has %d reference(s), want the array's 1", what, HEAP_REF(e));
		if (ctor > 0 && ctor < 40000)
			SC_CHECK(calls[ctor] == c0, "%s: the constructor ran %lu time(s) for the element; natively the new element "
				 "is made with 0x679b30(struct, 0)", what, calls[ctor] - c0);
		sc_independent(what, e, src);
	}
	uint32_t eseq = e > 0 && heap_index_valid(e) ? heap[e].seq : 0;
	heap_unref(src);
	SC_CHECK(e == src || (hp_alive(e, eseq) && !hp_alive(src, sseq)),
		 "%s: after the source's owner let go the element is %s and the source %s", what, hp_alive(e, eseq) ? "alive" : "gone",
		 hp_alive(src, sseq) ? "alive" : "gone");
	for (int m = 0; e != src && m < 64; m++)
		if (sh[m] > 0)
			SC_CHECK(hp_refs(sh[m], shseq[m]) == refs[m], "%s: after the source went the shared object of member %d has %d "
				 "reference(s), want %d (the source's given back, the element's kept)", what, m, hp_refs(sh[m], shseq[m]),
				 refs[m]);
	heap_unref(arr);
	SC_CHECK(!hp_alive(e, eseq) && !hp_alive(src, sseq), "%s: after the array went the element is %s", what,
		 hp_alive(e, eseq) ? "alive" : "gone");
	for (int m = 0; e != src && m < 64; m++)
		if (sh[m] > 0)
			SC_CHECK(hp_refs(sh[m], shseq[m]) == refs[m] - 1, "%s: after the array went the shared object of member %d has "
				 "%d reference(s), want %d", what, m, hp_refs(sh[m], shseq[m]), refs[m] - 1);
	if (ain->structures[sno].destructor > 0)
		SC_CHECK(sc_dtor_calls(sno) - d0 == 2, "%s: the destructor ran %lu time(s), want 2 (the source and the element)",
			 what, sc_dtor_calls(sno) - d0);
	while (hp_alive(src, sseq))
		heap_unref(src);
	heap_unref(target);
	SC_CHECK(live_slots() == live0, "%s: %zu live slot(s) after everything was released, %zu before", what, live_slots(),
		 live0);
}

static int sc_flat_struct(void)
{
	return sc_pick(SC_STR, true, true, "a struct of numbers and strings");
}

static void sc1_add(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	sc_store_one("SC1 Add", sno, "Add");
	printf("SC1 Add of a struct value: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc2_pushback(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	sc_store_one("SC2 PushBack", sno, "PushBack");
	printf("SC2 PushBack of a struct value: %s\n", xc_failures ? "FAIL" : "ok");
}

/* A typed array (as X_A_INIT makes it for array<T>) of n objects of sno,
 * each held by the array alone. Built directly, not through the store
 * under test. */
static int sc_array_of(int sno, int n, const int *objs)
{
	union vm_value dim = { .i = n };
	struct page *p = alloc_array(1, &dim, AIN_ARRAY_STRUCT, sno, false);
	for (int i = 0; i < n; i++)
		p->values[i].i = objs[i];
	return heap_alloc_page(p);
}

static int sc_filled_array(int sno, int n, int *objs)
{
	for (int i = 0; i < n; i++) {
		objs[i] = alloc_struct(sno);
		sc_fill_flat(objs[i]);
	}
	int arr = sc_array_of(sno, n, objs);
	if (hp_numof(arr) != n)
		xc_env_fail("could not build the array");
	return arr;
}

static void sc3_insert(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	sc_store_one("SC3 Insert into an empty array", sno, "Insert");
	static const struct { int at, lands; } at[] = { { 0, 0 }, { 2, 2 }, { 1, 1 }, { -5, 0 }, { 99, 2 } };
	for (unsigned i = 0; i < sizeof at / sizeof *at; i++) {
		size_t live0 = live_slots();
		int objs[2];
		int arr = sc_filled_array(sno, 2, objs);
		int src = alloc_struct(sno);
		sc_fill_flat(src);
		struct page *ps = sc_page(src);
		for (int m = 0; m < ps->nr_vars; m++)
			if (sc_kind_of(&ain->structures[sno].members[m].type) == SC_NUM)
				ps->values[m].i = 4000 + 10 * (int)i + m;
		uint32_t sseq = heap[src].seq;
		int v = sc_handed(src);
		unsigned long call0 = sc_calls_total();
		hp_hll("Insert", 0, 2, 3, (int[]){ arr, at[i].at, v });
		int e = hp_elem(arr, at[i].lands);
		SC_CHECK(sc_calls_total() == call0, "SC3 Insert at %d: %lu function(s) ran", at[i].at, sc_calls_total() - call0);
		char what[48];
		snprintf(what, sizeof what, "SC3 Insert at %d", at[i].at);
		SC_CHECK(hp_numof(arr) == 3, "%s: %d elements", what, hp_numof(arr));
		int k = 0;
		for (int j = 0; j < 3 && hp_numof(arr) == 3; j++) {
			if (j == at[i].lands)
				continue;
			int want = objs[k++];
			SC_CHECK(hp_elem(arr, j) == want, "%s: element %d is slot %d, want the old element %d", what, j,
				 hp_elem(arr, j), want);
		}
		SC_CHECK(hp_refs(src, sseq) == 1, "%s: the source has %d reference(s), want 1", what, hp_refs(src, sseq));
		sc_compare(what, e, src, NULL);
		if (e != src && sc_page(e))
			sc_independent(what, e, src);
		heap_unref(src);
		heap_unref(arr);
		while (hp_alive(src, sseq))
			heap_unref(src);
		SC_CHECK(live_slots() == live0, "%s: %zu live slot(s) after, %zu before", what, live_slots(), live0);
	}
	printf("SC3 Insert of a struct value at the head, tail, middle and clamped indices: %s\n", xc_failures ? "FAIL" : "ok");
}

/* Fill and Realloc#1: the value is A_REF(src). check_from..check_to are the
 * elements that must have been assigned in place (objs[i] kept). */
static void sc_fill_case(const char *what, int sno, int nobjs, const char *fn, int ord, int nargs, const int *extra,
			 int want_ret, int from, int to)
{
	size_t live0 = live_slots();
	int target = alloc_struct(sc_flat_struct());
	int objs[4];
	int arr = sc_filled_array(sno, nobjs, objs);
	int src = sc_new(sno, target);
	uint32_t sseq = heap[src].seq;
	int refs[64];
	sc_shared_refs(src, refs, 64);
	int v = sc_handed(src);
	int args[5] = { arr }, out[2] = { -999, -999 };
	for (int i = 0; i < nargs - 2; i++)
		args[1 + i] = extra[i];
	args[nargs - 1] = v;
	unsigned long ctor0 = sc_ctor_total();
	int left = oa_hll(fn, ord, 2, nargs, args, out, 2);
	/* Fill assigns in place and makes nothing (0x646e38); an element's old
	 * member objects are released, so destructors may run. Realloc makes
	 * its new tail with flag 1 (0x67f4d0), which constructs. */
	if (!strcmp(fn, "Fill"))
		SC_CHECK(sc_ctor_total() == ctor0, "%s: %lu constructor(s) ran", what, sc_ctor_total() - ctor0);
	if (want_ret != -999)
		SC_CHECK(left == 1 && out[0] == want_ret, "%s: returned %d (%d slot(s)), want %d", what, out[0], left, want_ret);
	SC_CHECK(hp_refs(src, sseq) == 1, "%s: the source has %d reference(s), want its owner's 1", what, hp_refs(src, sseq));
	for (int i = 0; i < hp_numof(arr); i++) {
		int e = hp_elem(arr, i);
		char w[64];
		snprintf(w, sizeof w, "%s element %d", what, i);
		if (i < nobjs)
			SC_CHECK(e == objs[i], "%s: is slot %d, want the element's own object %d (natively assigned in place, "
				 "0x646e38)", w, e, objs[i]);
		if (i >= from && i < to) {
			/* every element of the range holds the shared objects once */
			int r2[64];
			for (int m = 0; m < 64; m++)
				r2[m] = refs[m] + (to - from) - 1;
			sc_compare(w, e, src, r2);
		} else if (i < nobjs) {
			struct page *pe = sc_page(e);
			for (int m = 0; pe && m < pe->nr_vars; m++)
				if (sc_kind_of(&ain->structures[sno].members[m].type) == SC_NUM)
					SC_CHECK(pe->values[m].i == 1000 + 7 * m, "%s: outside the range but member %d is %d", w, m,
						 pe->values[m].i);
		}
	}
	for (int i = from; i < to && i < hp_numof(arr); i++)
		for (int j = i + 1; j < to && j < hp_numof(arr); j++)
			SC_CHECK(hp_elem(arr, i) != hp_elem(arr, j), "%s: elements %d and %d are one object", what, i, j);
	if (from < to && from < hp_numof(arr) && hp_elem(arr, from) != src && sc_page(hp_elem(arr, from)))
		sc_independent(what, hp_elem(arr, from), src);
	heap_unref(src);
	heap_unref(arr);
	while (hp_alive(src, sseq))
		heap_unref(src);
	heap_unref(target);
	SC_CHECK(live_slots() == live0, "%s: %zu live slot(s) after, %zu before", what, live_slots(), live0);
}

static int sc_wrap_struct(void)
{
	return sc_pick(SC_WRAP, true, false, "a struct with a wrap<struct> member");
}

static void sc4_fill_all(void)
{
	int flat = sc_flat_struct(), wrap = sc_wrap_struct();
	xc_ready();
	sc_fill_case("SC4 Fill#0", flat, 3, "Fill", 0, 2, NULL, 3, 0, 3);
	sc_fill_case("SC4 Fill#0 (wrap member)", wrap, 2, "Fill", 0, 2, NULL, 2, 0, 2);
	{
		/* an element without an object (-1, as an older resize of this VM
		 * leaves one) gets a new one with the value's content; natively
		 * every element has its object (port strategy) */
		size_t live0 = live_slots();
		int objs[2] = { alloc_struct(flat), -1 };
		sc_fill_flat(objs[0]);
		int arr = sc_array_of(flat, 2, objs);
		int src = alloc_struct(flat);
		sc_fill_base(src, 2000);
		uint32_t sseq = heap[src].seq;
		oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(src) }, (int[2]){ 0 }, 2);
		int e1 = hp_elem(arr, 1);
		SC_CHECK(hp_elem(arr, 0) == objs[0] && e1 != src && sc_page(e1) && HEAP_REF(e1) == 1,
			 "SC4 Fill#0 over a -1 element: elements %d,%d (the value %d)", hp_elem(arr, 0), e1, src);
		if (sc_page(e1) && e1 != src)
			sc_compare("SC4 Fill#0 over a -1 element", e1, src, NULL);
		heap_unref(src);
		heap_unref(arr);
		while (hp_alive(src, sseq))
			heap_unref(src);
		SC_CHECK(live_slots() == live0, "SC4 -1 element: %zu live slot(s) after, %zu before", live_slots(), live0);
	}
	printf("SC4 Fill of every element: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc5_fill_range(void)
{
	int flat = sc_flat_struct();
	xc_ready();
	sc_fill_case("SC5 Fill#1(1, 1)", flat, 3, "Fill", 1, 4, (int[]){ 1, 1 }, 1, 1, 2);
	/* 0x648144..0x64814d: a negative index shortens the length by it */
	sc_fill_case("SC5 Fill#1(-1, 2)", flat, 3, "Fill", 1, 4, (int[]){ -1, 2 }, 1, 0, 1);
	sc_fill_case("SC5 Fill#1(2, 5)", flat, 3, "Fill", 1, 4, (int[]){ 2, 5 }, 1, 2, 3);
	printf("SC5 Fill of a range: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc6_realloc_fill(void)
{
	int flat = sc_flat_struct(), wrap = sc_wrap_struct();
	xc_ready();
	sc_fill_case("SC6 Realloc#1(3)", flat, 1, "Realloc", 1, 3, (int[]){ 3 }, -999, 1, 3);
	sc_fill_case("SC6 Realloc#1(3) (wrap member)", wrap, 1, "Realloc", 1, 3, (int[]){ 3 }, -999, 1, 3);
	printf("SC6 Realloc with a value: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC7: the shape of CopySubFrames --- */

/* Nested content of frame i: BgLighting.ColorText, Sound.<Sounds> (an
 * array<string>), and CgLayers.m_layers: m_list (two FrameInfoCgLayer with
 * a <CgName> and a <Z>) and m_selected (an option<int> with a value). */
static void sc7_nested(int f, int i)
{
	int fi = sc_page(f)->index;
	char t[16];
	int bg = sc_member_obj(f, hp_member(fi, "BgLighting"));
	int n = snprintf(t, sizeof t, "c%d", i);
	sc_set_text(sc_page(bg), hp_member(sc_page(bg)->index, "ColorText"), t, n);
	int snd = sc_member_obj(f, hp_member(fi, "Sound"));
	sc_put_member(snd, hp_member(sc_page(snd)->index, "<Sounds>"), sc_str_array(2, "s"));
	int cg = sc_member_obj(f, hp_member(fi, "CgLayers"));
	int ly = sc_member_obj(cg, hp_member(sc_page(cg)->index, "m_layers"));
	int lno = sc_page(ly)->index;
	int ml = hp_member(lno, "m_list"), sel = hp_member(lno, "m_selected");
	const struct ain_type *lt = &ain->structures[lno].members[ml].type;
	if (!lt->array_type || lt->array_type->data != AIN_STRUCT)
		xc_env_fail("m_list");
	int cno = lt->array_type->struc, o[2];
	for (int k = 0; k < 2; k++) {
		o[k] = alloc_struct(cno);
		n = snprintf(t, sizeof t, "g%d-%d", i, k);
		sc_set_text(sc_page(o[k]), hp_member(cno, "<CgName>"), t, n);
		sc_page(o[k])->values[hp_member(cno, "<Z>")].i = 10 * i + k;
	}
	sc_put_member(ly, ml, sc_array_of(cno, 2, o));
	if (ss_type_slot_count(&ain->structures[lno].members[sel].type) != 2)
		xc_env_fail("m_selected");
	sc_page(ly)->values[sel].i = 1;
	sc_page(ly)->values[sel + 1].i = 0;	/* has a value */
}

static void sc7_copysub_shape(void)
{
	int fi = hp_struct("FrameInfo");
	int m_start = hp_member(fi, "<StartFrame>"), m_time = hp_member(fi, "<StartTime>");
	int m_end = hp_member(fi, "<EndTime>"), m_frame = hp_member(fi, "Frame"), m_key = hp_member(fi, "m_exBaseKey");
	int init = hp_fn("FrameInfo@2", 0);
	int ctor = ain->structures[fi].constructor;
	printf("  FrameInfo: struct %d (%d members, constructor %d, destructor %d)\n", fi, ain->structures[fi].nr_members, ctor,
	       ain->structures[fi].destructor);
	xc_ready();
	size_t live0 = live_slots();
	enum { N = 13, END = 32, SHIFT = 96 };
	int frame[N];
	uint32_t fseq[N];
	for (int i = 0; i < N; i++) {
		frame[i] = alloc_struct(fi);
		vm_call(init, frame[i]);
		struct page *p = sc_page(frame[i]);
		p->values[m_frame].i = i < N - 1 ? 1 : 20;
		p->values[m_start].i = i;
		p->values[m_time].i = i;
		p->values[m_end].i = i < N - 1 ? i + 1 : END;
		sc_set_text(sc_page(frame[i]), m_key, "k", 1);
		sc7_nested(frame[i], i);
		fseq[i] = heap[frame[i]].seq;
	}
	int cache = sc_array_of(fi, N, frame);	/* each frame the cache's alone */
	if (hp_numof(cache) != N)
		xc_env_fail("cache");
	unsigned long c0 = ctor > 0 && ctor < 40000 ? calls[ctor] : 0;
	for (int pass = 0; pass < 2; pass++) {
		int frames = hp_array(AIN_ARRAY_STRUCT, fi), taken = 0;
		unsigned long ran = 0;
		/* index from 0 while StartFrame < END: "f.StartFrame >= endFrame" ends the loop */
		for (int i = 0; i < hp_numof(cache); i++) {
			int f = hp_elem(cache, i);
			if (sc_page(f)->values[m_start].i >= END)
				break;
			int v = sc_handed(f);
			unsigned long call0 = sc_calls_total();
			hp_hll("Add", 0, 2, 2, (int[]){ frames, v });
			ran += sc_calls_total() - call0;
			taken++;
		}
		SC_CHECK(ran == 0, "pass %d: %lu function(s) ran during the stores", pass + 1, ran);
		SC_CHECK(taken == N, "pass %d took %d frame(s) from the cache, want %d (cache StartFrame now %d..%d)", pass + 1, taken,
			 N, sc_page(hp_elem(cache, 0))->values[m_start].i, sc_page(hp_elem(cache, N - 1))->values[m_start].i);
		int shared = 0;
		for (int i = 0; i < hp_numof(frames); i++)
			for (int j = 0; j < N; j++)
				shared += hp_elem(frames, i) == hp_elem(cache, j);
		SC_CHECK(shared == 0, "pass %d: %d of %d added element(s) are the cache's own objects", pass + 1, shared,
			 hp_numof(frames));
		/* SetSubFrameStart: StartFrame += SHIFT, StartTime follows (postset) */
		for (int i = 0; i < hp_numof(frames); i++) {
			struct page *p = sc_page(hp_elem(frames, i));
			if (!p)
				continue;
			p->values[m_start].i += SHIFT;
			p->values[m_time].i = p->values[m_start].i;
		}
		bool intact = true;
		for (int i = 0; i < N; i++) {
			struct page *p = sc_page(hp_elem(cache, i));
			intact &= p && p->values[m_start].i == i && p->values[m_time].i == i;
		}
		SC_CHECK(intact, "pass %d: moving the added frames moved the cache's (first StartFrame %d, StartTime %d)", pass + 1,
			 sc_page(hp_elem(cache, 0))->values[m_start].i, sc_page(hp_elem(cache, 0))->values[m_time].i);
		for (int i = 0; i < hp_numof(frames); i++) {
			int e = hp_elem(frames, i);
			struct page *p = sc_page(e);
			if (p && e != hp_elem(cache, i) && sc_page(hp_elem(cache, i))) {
				/* every level but the two moved numbers */
				int ce = hp_elem(cache, i);
				int ks = p->values[m_start].i, kt = p->values[m_time].i;
				p->values[m_start].i = sc_page(ce)->values[m_start].i;
				p->values[m_time].i = sc_page(ce)->values[m_time].i;
				char w[48];
				snprintf(w, sizeof w, "pass %d frame %d", pass + 1, i);
				sc_deep_struct(w, e, ce, 0);
				p->values[m_start].i = ks;
				p->values[m_time].i = kt;
			}
			if (p && e != hp_elem(cache, i))
				SC_CHECK(p->values[m_frame].i == (i < N - 1 ? 1 : 20) && p->values[m_end].i == (i < N - 1 ? i + 1 : END)
					 && sc_same_text(p->values[m_key].i, sc_page(hp_elem(cache, i))->values[m_key].i)
					 && p->values[m_key].i != sc_page(hp_elem(cache, i))->values[m_key].i,
					 "pass %d: added frame %d does not carry the cache frame's members", pass + 1, i);
		}
		heap_unref(frames);
	}
	if (ctor > 0 && ctor < 40000)
		SC_CHECK(calls[ctor] == c0, "the FrameInfo constructor ran %lu time(s) for added elements, want 0", calls[ctor] - c0);
	for (int i = 0; i < N; i++)
		SC_CHECK(hp_alive(frame[i], fseq[i]) && HEAP_REF(frame[i]) == 1, "cache frame %d has %d reference(s), want 1", i,
			 hp_refs(frame[i], fseq[i]));
	heap_unref(cache);
	SC_CHECK(live_slots() == live0, "%zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC7 the CopySubFrames shape: a second pass over the cache: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC8: destructors --- */

/* A struct with a destructor whose body is a bare RETURN that A_REF shares
 * here (not every member a plain number) and whose members are numbers,
 * strings, structs, arrays, delegates, wraps, interfaces or references
 * (nothing this VM may not copy); the fewest members, the lowest index. */
static int sc_pick_dtor(void)
{
	int best = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		if (s->destructor <= 0 || !sc_quiet_dtor(i) || s->nr_members <= 0 || s->nr_members > 64)
			continue;
		bool ok = true;
		int other = 0;
		for (int m = 0; ok && m < s->nr_members; m++) {
			enum sc_kind k = sc_kind_of(&s->members[m].type);
			ok = k != SC_OTHER && k != SC_OPTION;
			other += k != SC_NUM;
		}
		if (ok && other && (best < 0 || s->nr_members < ain->structures[best].nr_members))
			best = i;
	}
	if (best < 0) {
		printf("  ENVIRONMENT: no struct with a bare destructor that A_REF shares\n");
		xc_env_fail("struct shape");
	}
	printf("  a struct with a bare destructor: struct %d (%d members, constructor %d, destructor %d)\n", best,
	       ain->structures[best].nr_members, ain->structures[best].constructor, ain->structures[best].destructor);
	return best;
}

/* The source and the element are two objects: each destructor runs once
 * when its last owner lets go (the element is made with flag 0, so its
 * +0x3c, set only at 0x679d3e for a nonzero flag, stays 0 and 0x6813c0 runs
 * the destructor, 0x681403; natively the A_REF copy is a third object,
 * released by the call; here A_REF shares, which is outside this change).
 * The source is as alloc_struct makes it. */
static void sc8_destructors(void)
{
	int sno = sc_pick_dtor();
	xc_ready();
	static const char *how[] = { "Add", "PushBack", "Insert" };
	for (int h = 0; h < 3; h++) {
		size_t live0 = live_slots();
		int src = alloc_struct(sno), arr = hp_array(AIN_ARRAY_STRUCT, sno);
		uint32_t sseq = heap[src].seq;
		unsigned long d0 = sc_dtor_calls(sno);
		int v = sc_handed(src);
		unsigned long call0 = sc_calls_total();
		if (h == 2)
			hp_hll(how[h], 0, 2, 3, (int[]){ arr, 0, v });
		else
			hp_hll(how[h], 0, 2, 2, (int[]){ arr, v });
		/* nothing runs: the inner objects this VM gave the new element's
		 * wraps are not the original's (0x6569b8 leaves -1) */
		SC_CHECK(sc_calls_total() == call0, "%s: %lu function(s) ran during the store", how[h], sc_calls_total() - call0);
		int e = hp_elem(arr, 0);
		uint32_t eseq = e > 0 && heap_index_valid(e) ? heap[e].seq : 0;
		SC_CHECK(hp_numof(arr) == 1 && e != src && hp_alive(e, eseq), "%s: the element is %d, the source %d", how[h], e, src);
		heap_unref(src);
		SC_CHECK(sc_dtor_calls(sno) - d0 == 1 && !hp_alive(src, sseq),
			 "%s: after the source's owner let go the destructor ran %lu time(s), want 1 (the source's)", how[h],
			 sc_dtor_calls(sno) - d0);
		heap_unref(arr);
		SC_CHECK(sc_dtor_calls(sno) - d0 == 2 && !hp_alive(e, eseq),
			 "%s: after the array went the destructor ran %lu time(s) in all, want 2 (the source and the element)",
			 how[h], sc_dtor_calls(sno) - d0);
		while (hp_alive(src, sseq))
			heap_unref(src);
		SC_CHECK(live_slots() == live0, "%s: %zu live slot(s) after, %zu before", how[h], live_slots(), live0);
	}
	printf("SC8 destructors of the source and the element: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC9..SC15: the member rules --- */

/* Fill#0 over two elements whose members all hold objects (sc_new): every
 * element keeps its object, and its struct member object (0x656eb6 assigns
 * into it); the members are the value's by the member rules; the objects
 * the elements gave up are released (0x679f90 for the shared ones: the old
 * wrapped object of element 0 is a struct with a bare destructor, which
 * runs once as its last reference goes, 0x6813c0); nothing is left. */
static void sc_member_fill(const char *what, int sno)
{
	size_t live0 = live_slots();
	int target = alloc_struct(sc_flat_struct()), dno = sc_pick_dtor();
	int old_target = alloc_struct(sc_flat_struct());	/* what the elements' references held */
	int objs[2] = { sc_new(sno, old_target), sc_new(sno, old_target) };
	struct ain_struct *s = &ain->structures[sno];
	int keep_struct[64], mw = -1;
	unsigned long d0 = sc_dtor_calls(dno);
	for (int m = 0; m < s->nr_members && m < 64; m++) {
		keep_struct[m] = sc_page(objs[0])->values[m].i;
		if (sc_kind_of(&s->members[m].type) == SC_WRAP && mw < 0) {
			mw = m;
			sc_put_member(objs[0], m, alloc_struct(dno));	/* its old wrapped object */
		}
	}
	for (int i = 0; i < 2; i++)
		sc_fill_base(objs[i], 1000);
	int arr = sc_array_of(sno, 2, objs);
	int src = sc_new(sno, target);
	uint32_t sseq = heap[src].seq;
	int refs[64];
	sc_shared_refs(src, refs, 64);
	unsigned long ctor0 = sc_ctor_total();
	oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(src) }, (int[2]){ 0 }, 2);
	SC_CHECK(sc_ctor_total() == ctor0, "%s Fill#0: %lu constructor(s) ran", what, sc_ctor_total() - ctor0);
	bool in_place = hp_elem(arr, 0) == objs[0] && hp_elem(arr, 1) == objs[1] && sc_page(objs[0]) && sc_page(objs[1]);
	SC_CHECK(in_place, "%s Fill#0: elements %d,%d, want %d,%d (in place)", what, hp_elem(arr, 0), hp_elem(arr, 1), objs[0],
		 objs[1]);
	if (in_place) {
		int r2[64];
		for (int m = 0; m < 64; m++)
			r2[m] = refs[m] + 1;	/* both elements hold the value's shared objects: sc_compare adds the other */
		char w[96];
		snprintf(w, sizeof w, "%s Fill#0 element 0", what);
		sc_compare(w, objs[0], src, r2);
		for (int m = 0; m < s->nr_members && m < 64; m++)
			if (sc_kind_of(&s->members[m].type) == SC_STRUCT)
				SC_CHECK(sc_page(objs[0])->values[m].i == keep_struct[m], "%s: struct member %d is slot %d, want its own %d "
					 "(assigned in place)", w, m, sc_page(objs[0])->values[m].i, keep_struct[m]);
		if (mw >= 0)
			SC_CHECK(sc_dtor_calls(dno) - d0 == 1, "%s: the old wrapped object's destructor ran %lu time(s), want 1", w,
				 sc_dtor_calls(dno) - d0);
	}
	heap_unref(src);
	heap_unref(arr);
	while (hp_alive(src, sseq))
		heap_unref(src);
	heap_unref(target);
	heap_unref(old_target);
	SC_CHECK(live_slots() == live0, "%s Fill#0: %zu live slot(s) after, %zu before", what, live_slots(), live0);
}

static void sc_member_case(const char *id, enum sc_kind k, const char *what)
{
	int sno = sc_pick(k, false, false, what);
	xc_ready();
	char w[64];
	snprintf(w, sizeof w, "%s %s", id, what);
	sc_store_one(w, sno, "Add");
	sc_member_fill(w, sno);
	printf("%s %s: %s\n", id, what, xc_failures ? "FAIL" : "ok");
}

static void sc9_string(void) { sc_member_case("SC9", SC_STR, "a string member"); }
static void sc10_struct(void) { sc_member_case("SC10", SC_STRUCT, "a struct member"); }
static void sc11_array(void) { sc_member_case("SC11", SC_ARRAY, "an array<int> member"); }
static void sc12_delegate(void) { sc_member_case("SC12", SC_DG, "a delegate member"); }
static void sc13_wrap(void) { sc_member_case("SC13", SC_WRAP, "a wrap<struct> member"); }
static void sc14_iface(void) { sc_member_case("SC14", SC_IFACE, "an interface member"); }
static void sc15_ref(void) { sc_member_case("SC15", SC_REF, "a ref struct member"); }

/* --- SC17, SC18 --- */

static void sc17_none(void)
{
	int sno = sc_flat_struct();
	int ctor = ain->structures[sno].constructor;
	xc_ready();
	size_t live0 = live_slots();
	int arr = hp_array(AIN_ARRAY_STRUCT, sno);
	unsigned long c0 = ctor > 0 && ctor < 40000 ? calls[ctor] : 0;
	unsigned long call0 = sc_calls_total();
	/* Add, then Insert at 0, then PushBack: elements 1, 0 and 2 */
	hp_hll("Add", 0, 2, 2, (int[]){ arr, -1 });
	int first = hp_elem(arr, 0);
	hp_hll("Insert", 0, 2, 3, (int[]){ arr, 0, -1 });
	hp_hll("PushBack", 0, 2, 2, (int[]){ arr, -1 });
	SC_CHECK(sc_calls_total() == call0, "none: %lu function(s) ran during the stores", sc_calls_total() - call0);
	SC_CHECK(hp_numof(arr) == 3 && hp_elem(arr, 1) == first, "none: %d element(s), element 1 is %d (the first, %d)",
		 hp_numof(arr), hp_elem(arr, 1), first);
	static const char *how[] = { "Insert", "Add", "PushBack" };
	for (int i = 0; i < 3 && hp_numof(arr) == 3; i++) {
		int e = hp_elem(arr, i);
		struct page *p = sc_page(e);
		SC_CHECK(p && p->type == STRUCT_PAGE && p->index == sno,
			 "%s of none: the element is %d (%s); natively a new object of the array's struct (0x67f5c0), "
			 "which the assignment leaves as it is (0x67ac30 gives 0, 0x681833)", how[i], e, p ? "a page" : "no page");
		if (!p || p->type != STRUCT_PAGE || p->index != sno)
			continue;
		for (int m = 0; m < p->nr_vars; m++) {
			enum sc_kind k = sc_kind_of(&ain->structures[sno].members[m].type);
			if (k == SC_NUM)
				SC_CHECK(p->values[m].i == 0, "%s of none: number member %d is %d, want the default 0 (0x6569a9)", how[i],
					 m, p->values[m].i);
			else if (k == SC_STR)
				/* natively a new empty string (0x6569fc); -1 here (init_struct_slot) */
				SC_CHECK(sc_default_text(p->values[m].i), "%s of none: string member %d is not the default", how[i], m);
		}
		SC_CHECK(HEAP_REF(e) == 1, "%s of none: the element has %d reference(s), want the array's 1", how[i], HEAP_REF(e));
		for (int j = 0; j < i; j++)
			SC_CHECK(hp_elem(arr, j) != e, "none: elements %d and %d are one object", j, i);
	}
	if (ctor > 0 && ctor < 40000)
		SC_CHECK(calls[ctor] == c0, "none ran the constructor %lu time(s)", calls[ctor] - c0);
	heap_unref(arr);
	/* Fill#0 with none: nothing is assigned (0x681833), the length returned */
	int objs[2];
	arr = sc_filled_array(sno, 2, objs);
	int mn = sc_first(sno, SC_NUM), ms = sc_first(sno, SC_STR);
	int s0 = sc_page(objs[0])->values[ms].i;
	int out[2] = { -9, -9 };
	oa_hll("Fill", 0, 2, 2, (int[]){ arr, -1 }, out, 2);
	SC_CHECK(out[0] == 2 && hp_elem(arr, 0) == objs[0] && hp_elem(arr, 1) == objs[1]
		 && sc_page(objs[0])->values[mn].i == 1000 + 7 * mn && sc_page(objs[0])->values[ms].i == s0,
		 "Fill#0 of none: returned %d, elements %d,%d (were %d,%d); want 2 and the elements as they were", out[0],
		 hp_elem(arr, 0), hp_elem(arr, 1), objs[0], objs[1]);
	heap_unref(arr);
	/* Realloc#1(3, none): the old element stays, the new ones are live */
	arr = sc_filled_array(sno, 1, objs);
	oa_hll("Realloc", 1, 2, 3, (int[]){ arr, 3, -1 }, out, 2);
	SC_CHECK(hp_numof(arr) == 3 && hp_elem(arr, 0) == objs[0] && sc_page(objs[0])->values[mn].i == 1000 + 7 * mn
		 && sc_page(hp_elem(arr, 1)) && sc_page(hp_elem(arr, 2)) && hp_elem(arr, 1) != hp_elem(arr, 2),
		 "Realloc#1(3, none): %d element(s) %d,%d,%d (the old %d)", hp_numof(arr), hp_elem(arr, 0), hp_elem(arr, 1),
		 hp_elem(arr, 2), objs[0]);
	heap_unref(arr);
	SC_CHECK(live_slots() == live0, "%zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC17 none into an array of value structs: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc18_after_erase(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	size_t live0 = live_slots();
	int objs[1];
	int arr = sc_filled_array(sno, 1, objs);
	hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
	if (hp_numof(arr) != 0)
		xc_env_fail("Erase did not empty the array");
	/* Add, Add, Insert at 0, PushBack: elements 1, 2, 0, 3 */
	static const char *how[] = { "Add", "Add", "Insert", "PushBack" };
	static const int lands[] = { 1, 2, 0, 3 };
	int src[4];
	uint32_t sseq[4];
	for (int k = 0; k < 4; k++) {
		src[k] = alloc_struct(sno);
		sc_fill_base(src[k], 2000 + 100 * k);
		sseq[k] = heap[src[k]].seq;
		int v = sc_handed(src[k]);
		if (k == 2)
			hp_hll("Insert", 0, 2, 3, (int[]){ arr, 0, v });
		else
			hp_hll(how[k], 0, 2, 2, (int[]){ arr, v });
		SC_CHECK(hp_refs(src[k], sseq[k]) == 1, "after Erase, %s %d: the source has %d reference(s), want 1", how[k], k,
			 hp_refs(src[k], sseq[k]));
	}
	SC_CHECK(hp_numof(arr) == 4, "after Erase: %d element(s), want 4", hp_numof(arr));
	int el[4];
	for (int k = 0; k < 4 && hp_numof(arr) == 4; k++) {
		char w[48];
		snprintf(w, sizeof w, "SC18 %s %d after Erase", how[k], k);
		el[k] = hp_elem(arr, lands[k]);
		sc_compare(w, el[k], src[k], NULL);
		if (el[k] != src[k] && sc_page(el[k]))
			sc_independent(w, el[k], src[k]);
	}
	/* then Fill#0 assigns every element in place */
	if (hp_numof(arr) == 4) {
		int f = alloc_struct(sno);
		sc_fill_base(f, 2900);
		uint32_t fseq = heap[f].seq;
		int out[2] = { -9, -9 };
		oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(f) }, out, 2);
		SC_CHECK(out[0] == 4, "after Erase, Fill#0 returned %d", out[0]);
		for (int k = 0; k < 4; k++) {
			char w[48];
			snprintf(w, sizeof w, "SC18 Fill#0 after Erase, element %d", lands[k]);
			SC_CHECK(hp_elem(arr, lands[k]) == el[k], "%s: replaced (%d, was %d)", w, hp_elem(arr, lands[k]), el[k]);
			sc_compare(w, hp_elem(arr, lands[k]), f, NULL);
		}
		heap_unref(f);
		while (hp_alive(f, fseq))
			heap_unref(f);
	}
	for (int k = 0; k < 4; k++) {
		heap_unref(src[k]);
		while (hp_alive(src[k], sseq[k]))
			heap_unref(src[k]);
	}
	heap_unref(arr);
	SC_CHECK(live_slots() == live0, "%zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC18 stores into an array that Erase emptied: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- guards --- */

static void sc20_wrap_elements(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	size_t live0 = live_slots();
	int arr = hp_array(AIN_ARRAY, -1), s = alloc_struct(sno);
	uint32_t seq = heap[s].seq;
	hp_hll("PushBack", 0, 0x10002, 2, (int[]){ arr, s });
	hp_hll("Insert", 0, 0x10002, 3, (int[]){ arr, 0, s });
	SC_CHECK(hp_numof(arr) == 2 && hp_elem(arr, 0) == s && hp_elem(arr, 1) == s && hp_refs(s, seq) == 3,
		 "wrap elements: %d element(s) %d,%d, %d reference(s); want the object twice and 3 references", hp_numof(arr),
		 hp_elem(arr, 0), hp_elem(arr, 1), hp_refs(s, seq));
	heap_unref(arr);
	SC_CHECK(hp_refs(s, seq) == 1, "after the wrap array went the object has %d reference(s)", hp_refs(s, seq));
	while (hp_alive(s, seq))
		heap_unref(s);
	SC_CHECK(live_slots() == live0, "%zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC20 wrap elements stay shared: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc21_iface_elements(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	size_t live0 = live_slots();
	int arr = hp_array(AIN_ARRAY, -1), s = alloc_struct(sno);
	uint32_t seq = heap[s].seq;
	hp_hll("PushBack", 0, 0x10003, 3, (int[]){ arr, s, 2 });
	SC_CHECK(hp_numof(arr) == 2 && hp_elem(arr, 0) == s && hp_elem(arr, 1) == 2 && hp_refs(s, seq) == 2,
		 "interface element: %d slot(s) %d,%d, %d reference(s); want (object, 2) and 2 references", hp_numof(arr),
		 hp_elem(arr, 0), hp_elem(arr, 1), hp_refs(s, seq));
	heap_unref(arr);
	SC_CHECK(hp_refs(s, seq) == 1, "after the interface array went the object has %d reference(s)", hp_refs(s, seq));
	while (hp_alive(s, seq))
		heap_unref(s);
	SC_CHECK(live_slots() == live0, "%zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC21 interface elements stay shared: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc22_numbers(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	size_t live0 = live_slots();
	int arr = hp_array(AIN_ARRAY_INT, -1), s = alloc_struct(sno);
	uint32_t seq = heap[s].seq;
	hp_hll("PushBack", 0, 1, 2, (int[]){ arr, s });
	hp_hll("Insert", 0, 1, 3, (int[]){ arr, 0, s });
	hp_hll("Fill", 0, 1, 2, (int[]){ arr, s });
	SC_CHECK(hp_numof(arr) == 2 && hp_elem(arr, 0) == s && hp_elem(arr, 1) == s && hp_refs(s, seq) == 1,
		 "numbers: %d element(s), the slot equal to the number has %d reference(s), want 1", hp_numof(arr), hp_refs(s, seq));
	heap_unref(arr);
	while (hp_alive(s, seq))
		heap_unref(s);
	SC_CHECK(live_slots() == live0, "%zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC22 plain values are stored as they are: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc23_strings(void)
{
	xc_ready();
	size_t live0 = live_slots();
	int arr = hp_array(AIN_ARRAY_STRING, -1), t = sf_str("sc-text", -1);
	uint32_t seq = heap[t].seq;
	hp_hll("PushBack", 0, 2, 2, (int[]){ arr, t });
	SC_CHECK(hp_numof(arr) == 1 && hp_elem(arr, 0) == t && hp_refs(t, seq) == 2,
		 "a string: element %d, %d reference(s); this branch keeps the slot shared with 2 references", hp_elem(arr, 0),
		 hp_refs(t, seq));
	heap_unref(arr);
	SC_CHECK(hp_refs(t, seq) == 1, "after the string array went the string has %d reference(s)", hp_refs(t, seq));
	while (hp_alive(t, seq))
		heap_unref(t);
	SC_CHECK(live_slots() == live0, "%zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC23 strings keep this branch's rule: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc24_plain_struct(void)
{
	int sno = sc_pick_plain();
	xc_ready();
	size_t live0 = live_slots();
	int arr = hp_array(AIN_ARRAY_STRUCT, sno), s = alloc_struct(sno);
	sc_fill_flat(s);
	uint32_t seq = heap[s].seq;
	hp_hll("Add", 0, 2, 2, (int[]){ arr, sc_handed(s) });
	int e = hp_elem(arr, 0);
	SC_CHECK(hp_numof(arr) == 1 && e != s && sc_page(e) && HEAP_REF(e) == 1 && hp_refs(s, seq) == 1,
		 "a plain struct: element %d (the source %d), %d reference(s) on the element, %d on the source", e, s,
		 sc_page(e) ? HEAP_REF(e) : -1, hp_refs(s, seq));
	if (e != s && sc_page(e)) {
		sc_compare("SC24", e, s, NULL);
		sc_independent("SC24", e, s);
	}
	heap_unref(arr);
	heap_unref(s);
	while (hp_alive(s, seq))
		heap_unref(s);
	SC_CHECK(live_slots() == live0, "%zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC24 a struct of plain numbers through A_REF: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc25_legacy(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	static const int versions[] = { 13, 11 };
	for (unsigned i = 0; i < 2; i++) {
		int arr = hp_array(AIN_ARRAY_STRUCT, sno), s = alloc_struct(sno);
		uint32_t seq = heap[s].seq;
		heap_ref(s);	/* the reference a value hands over, as HP7 */
		int saved = ain->version;
		ain->version = versions[i];
		hp_hll("PushBack", 0, 2, 2, (int[]){ arr, s });
		hp_hll("Insert", 0, 2, 3, (int[]){ arr, 0, s });
		hp_hll("Add", 0, 2, 2, (int[]){ arr, s });
		ain->version = saved;
		SC_CHECK(hp_numof(arr) == 3 && hp_elem(arr, 0) == s && hp_elem(arr, 1) == s && hp_elem(arr, 2) == s
			 && hp_refs(s, seq) == 5,
			 "v%d: %d element(s) %d,%d,%d, %d reference(s); this branch's v11-v13 rule stores the page itself and gives "
			 "nothing back (5)", versions[i], hp_numof(arr), hp_elem(arr, 0), hp_elem(arr, 1), hp_elem(arr, 2),
			 hp_refs(s, seq));
		heap_unref(arr);
		while (hp_alive(s, seq))
			heap_unref(s);
		/* Fill#0: this branch replaces the element by vm_copy of the value
		 * (copy_page below v14); the old element goes. */
		int objs[1];
		arr = sc_filled_array(sno, 1, objs);
		uint32_t oseq = heap[objs[0]].seq;
		s = alloc_struct(sno);
		sc_fill_base(s, 2000);
		seq = heap[s].seq;
		ain->version = versions[i];
		int out[2] = { -9, -9 };
		int left = oa_hll("Fill", 0, 2, 2, (int[]){ arr, s }, out, 2);
		ain->version = saved;
		int e = hp_elem(arr, 0);
		SC_CHECK(left == 1 && out[0] == 1 && e != s && e != objs[0] && !hp_alive(objs[0], oseq) && sc_page(e)
			 && hp_refs(s, seq) == 1 && sc_same_text(sc_page(e)->values[sc_first(sno, SC_STR)].i,
						      sc_page(s)->values[sc_first(sno, SC_STR)].i),
			 "v%d Fill#0: returned %d, element %d (old %d, value %d), value %d reference(s); this branch's rule "
			 "replaces the element by a copy", versions[i], out[0], e, objs[0], s, hp_refs(s, seq));
		/* Fill#1(0, 1): the same rule */
		oseq = heap[e].seq;
		ain->version = versions[i];
		left = oa_hll("Fill", 1, 2, 4, (int[]){ arr, 0, 1, s }, out, 2);
		ain->version = saved;
		int e2 = hp_elem(arr, 0);
		SC_CHECK(left == 1 && out[0] == 1 && e2 != s && e2 != e && !hp_alive(e, oseq) && sc_page(e2) && hp_refs(s, seq) == 1,
			 "v%d Fill#1(0, 1): returned %d, element %d (old %d, value %d); this branch's rule replaces the element by a copy",
			 versions[i], out[0], e2, e, s);
		/* Realloc#1(2, value): the new tail element is a copy */
		ain->version = versions[i];
		oa_hll("Realloc", 1, 2, 3, (int[]){ arr, 2, s }, out, 2);
		ain->version = saved;
		int e3 = hp_elem(arr, 1);
		SC_CHECK(hp_numof(arr) == 2 && hp_elem(arr, 0) == e2 && e3 != s && sc_page(e3) && hp_refs(s, seq) == 1
			 && sc_same_text(sc_page(e3)->values[sc_first(sno, SC_STR)].i, sc_page(s)->values[sc_first(sno, SC_STR)].i),
			 "v%d Realloc#1(2): %d element(s), %d,%d (value %d); this branch's rule makes the new element a copy",
			 versions[i], hp_numof(arr), hp_elem(arr, 0), e3, s);
		heap_unref(arr);
		while (hp_alive(s, seq))
			heap_unref(s);
		/* Erase to empty: this branch's rule below v14 frees the page */
		arr = sc_filled_array(sno, 1, objs);
		ain->version = versions[i];
		hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
		ain->version = saved;
		SC_CHECK(heap_index_valid(arr) && heap[arr].page == NULL, "v%d Erase to empty: the array has %s; this branch's "
			 "rule leaves no page", versions[i], heap[arr].page ? "a page" : "no page");
		heap_unref(arr);
	}
	printf("SC25 v13 and v11 keep their rule: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc26_bytes(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	int m = sc_first(sno, SC_STR);
	static const char text[] = "a\x82\xa0" "b\x81\x40" "c";
	const int n = sizeof text - 1;
	int arr = hp_array(AIN_ARRAY_STRUCT, sno), s = alloc_struct(sno);
	sc_set_text(sc_page(s), m, text, n);
	uint32_t seq = heap[s].seq;
	hp_hll("Add", 0, 2, 2, (int[]){ arr, sc_handed(s) });
	int e = hp_elem(arr, 0);
	SC_CHECK(sc_page(e) && sc_text_is(sc_page(e)->values[m].i, text, n) && sc_text_is(sc_page(s)->values[m].i, text, n),
		 "the string member's bytes did not come through unchanged");
	heap_unref(arr);
	heap_unref(s);
	while (hp_alive(s, seq))
		heap_unref(s);
	printf("SC26 string member bytes in this charset: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc27_positions(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	int m = sc_first(sno, SC_NUM);
	static const struct { int at, lands; } at[] = { { 0, 0 }, { 2, 2 }, { -5, 0 }, { 99, 2 } };
	for (unsigned i = 0; i < sizeof at / sizeof *at; i++) {
		int objs[2];
		int arr = sc_filled_array(sno, 2, objs);
		int s = alloc_struct(sno);
		sc_page(s)->values[m].i = 9000 + (int)i;
		uint32_t seq = heap[s].seq;
		hp_hll("Insert", 0, 2, 3, (int[]){ arr, at[i].at, sc_handed(s) });
		int e = hp_elem(arr, at[i].lands);
		SC_CHECK(hp_numof(arr) == 3 && sc_page(e) && sc_page(e)->values[m].i == 9000 + (int)i,
			 "Insert at %d: %d element(s), element %d holds %d", at[i].at, hp_numof(arr), at[i].lands,
			 sc_page(e) ? sc_page(e)->values[m].i : -1);
		int k = 0;
		for (int j = 0; j < 3 && hp_numof(arr) == 3; j++) {
			if (j == at[i].lands)
				continue;
			int want = objs[k++];
			SC_CHECK(hp_elem(arr, j) == want, "Insert at %d: element %d moved", at[i].at, j);
		}
		heap_unref(arr);
		heap_unref(s);
		while (hp_alive(s, seq))
			heap_unref(s);
	}
	int arr = hp_array(AIN_ARRAY_STRUCT, sno), s = alloc_struct(sno);
	sc_page(s)->values[m].i = 9100;
	uint32_t seq = heap[s].seq;
	hp_hll("Add", 0, 2, 2, (int[]){ arr, sc_handed(s) });
	SC_CHECK(hp_numof(arr) == 1 && sc_page(hp_elem(arr, 0)) && sc_page(hp_elem(arr, 0))->values[m].i == 9100,
		 "Add into an empty array: %d element(s)", hp_numof(arr));
	heap_unref(arr);
	heap_unref(s);
	while (hp_alive(s, seq))
		heap_unref(s);
	printf("SC27 positions, counts and numbers: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc28_none_untyped(void)
{
	xc_ready();
	size_t live0 = live_slots();
	int arrs[3] = { hp_array(AIN_ARRAY, -1), hp_array(AIN_ARRAY_INT, -1), heap_alloc_slot(VM_PAGE) };
	heap_set_page(arrs[2], NULL);
	for (int i = 0; i < 3; i++) {
		hp_hll("Add", 0, 2, 2, (int[]){ arrs[i], -1 });
		SC_CHECK(hp_numof(arrs[i]) == 1 && hp_elem(arrs[i], 0) == -1, "none into array %d: %d element(s), element %d", i,
			 hp_numof(arrs[i]), hp_elem(arrs[i], 0));
		heap_unref(arrs[i]);
	}
	/* v14 Erase to empty of a number array: this branch's rule frees the
	 * page (only an array of value structs keeps it) */
	union vm_value one = { .i = 1 };
	int ints = heap_alloc_page(alloc_array(1, &one, AIN_ARRAY_INT, -1, false));
	hp_hll("Erase", 0, 1, 3, (int[]){ ints, 0, 1 });
	SC_CHECK(heap_index_valid(ints) && heap[ints].page == NULL, "v14 Erase to empty of a number array: the array has %s; "
		 "this branch's rule leaves no page", heap[ints].page ? "a page" : "no page");
	heap_unref(ints);
	SC_CHECK(live_slots() == live0, "%zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC28 none into an array without an element type: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc29_wrap_box(void)
{
	xc_ready();
	size_t live0 = live_slots();
	int box = heap_alloc_page(alloc_page(STRUCT_PAGE, -1, 1));
	heap[box].page->values[0].i = -1;
	uint32_t seq = heap[box].seq;
	int arr = hp_array(AIN_ARRAY, -1);
	heap_ref(box);	/* handed over */
	hp_hll("PushBack", 0, 2, 2, (int[]){ arr, box });
	SC_CHECK(hp_numof(arr) == 1 && hp_elem(arr, 0) == box && hp_refs(box, seq) == 2,
		 "a wrap box: element %d, %d reference(s); want the box itself and 2", hp_elem(arr, 0), hp_refs(box, seq));
	heap_unref(arr);
	while (hp_alive(box, seq))
		heap_unref(box);
	SC_CHECK(live_slots() == live0, "%zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC29 a wrap box keeps its handling: %s\n", xc_failures ? "FAIL" : "ok");
}

static void sc30_empty(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	size_t live0 = live_slots();
	int arr = hp_array(AIN_ARRAY_STRUCT, sno), s = alloc_struct(sno);
	uint32_t seq = heap[s].seq;
	size_t live1 = live_slots();
	int out[2] = { -9, -9 };
	int n = oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(s) }, out, 2);
	SC_CHECK(n == 1 && out[0] == 0 && hp_numof(arr) == 0, "Fill#0 of an empty array returned %d, %d element(s)", out[0],
		 hp_numof(arr));
	n = oa_hll("Fill", 1, 2, 4, (int[]){ arr, 0, 3, sc_handed(s) }, out, 2);
	SC_CHECK(n == 1 && out[0] == 0 && hp_numof(arr) == 0, "Fill#1(0, 3) of an empty array returned %d, %d element(s)",
		 out[0], hp_numof(arr));
	oa_hll("Realloc", 1, 2, 3, (int[]){ arr, 0, sc_handed(s) }, out, 2);
	SC_CHECK(hp_numof(arr) == 0 && hp_refs(s, seq) == 1 && live_slots() == live1,
		 "Realloc#1(0) of an empty array: %d element(s), source %d reference(s), live %zu -> %zu", hp_numof(arr),
		 hp_refs(s, seq), live1, live_slots());
	heap_unref(arr);
	heap_unref(s);
	while (hp_alive(s, seq))
		heap_unref(s);
	SC_CHECK(live_slots() == live0, "%zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC30 empty arrays: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC16: nested members through an array of structs with wraps --- */

/* T: numbers and wrap<struct> members (not the first), at least one wrap. */
static bool sc16_inner_ok(int t)
{
	if (t < 0 || t >= ain->nr_structures || !sc_quiet_dtor(t))
		return false;
	struct ain_struct *s = &ain->structures[t];
	int w = 0;
	if (s->nr_members <= 0 || s->nr_members > 64)
		return false;
	for (int m = 0; m < s->nr_members; m++) {
		const struct ain_type *mt = &s->members[m].type;
		enum sc_kind k = sc_kind_of(mt);
		if (k == SC_NUM)
			continue;
		if (k == SC_WRAP && m > 0 && mt->array_type && mt->array_type->data == AIN_STRUCT && mt->struc >= 0) {
			w++;
			continue;
		}
		return false;
	}
	return w > 0;
}

/* S: numbers and an array of such a T. */
static int sc16_pick(int *mem)
{
	int best = -1, bm = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		if (s->nr_members <= 0 || s->nr_members > 64 || !s->name || !sc_quiet_dtor(i))
			continue;
		int am = -1;
		bool ok = true;
		for (int m = 0; ok && m < s->nr_members; m++) {
			const struct ain_type *t = &s->members[m].type;
			enum sc_kind k = sc_kind_of(t);
			if (k == SC_NUM)
				continue;
			if (k == SC_ARRAY && t->rank <= 1 && t->array_type && t->array_type->data == AIN_STRUCT
			    && sc16_inner_ok(t->array_type->struc) && am < 0) {
				am = m;
				continue;
			}
			ok = false;
		}
		if (ok && am >= 0 && (best < 0 || s->nr_members < ain->structures[best].nr_members)) {
			best = i;
			bm = am;
		}
	}
	if (best < 0) {
		printf("  ENVIRONMENT: no struct with an array of structs with wraps\n");
		xc_env_fail("struct shape");
	}
	printf("  a struct with an array of structs with wrap members: struct %d (%d members), member %d of struct %d\n", best,
	       ain->structures[best].nr_members, bm, ain->structures[best].members[bm].type.array_type->struc);
	*mem = bm;
	return best;
}

/* The wrapped objects are the inner objects alloc_struct gives each wrap
 * member here; each is one object, shared by the source's element and its
 * copy. */
static void sc16_nested(void)
{
	int am;
	int sno = sc16_pick(&am);
	int tno = ain->structures[sno].members[am].type.array_type->struc;
	xc_ready();
	size_t live0 = live_slots();
	struct ain_struct *ts = &ain->structures[tno];
	int src = alloc_struct(sno);
	sc_fill_base(src, 2000);
	int objs[2], w[2][64], nw = 0;
	uint32_t wseq[2][64];
	for (int i = 0; i < 2; i++) {
		objs[i] = alloc_struct(tno);
		sc_fill_base(objs[i], 3000 + 100 * i);
		nw = 0;
		for (int m = 0; m < ts->nr_members; m++) {
			if (sc_kind_of(&ts->members[m].type) != SC_WRAP)
				continue;
			w[i][nw] = sc_page(objs[i])->values[m].i;
			if (!sc_page(w[i][nw]) || HEAP_REF(w[i][nw]) != 1)
				xc_env_fail("a wrap member without its own inner object");
			wseq[i][nw] = heap[w[i][nw]].seq;
			nw++;
		}
	}
	sc_put_member(src, am, sc_array_of(tno, 2, objs));
	uint32_t sseq = heap[src].seq;
	int arr = hp_array(AIN_ARRAY_STRUCT, sno);
	int v = sc_handed(src);
	unsigned long call0 = sc_calls_total();
	hp_hll("Add", 0, 2, 2, (int[]){ arr, v });
	SC_CHECK(sc_calls_total() == call0, "SC16: %lu function(s) ran during the store", sc_calls_total() - call0);
	int e = hp_elem(arr, 0);
	SC_CHECK(hp_numof(arr) == 1 && hp_refs(src, sseq) == 1, "SC16: %d element(s), the source %d reference(s)", hp_numof(arr),
		 hp_refs(src, sseq));
	SC_CHECK(e != src && sc_page(e), "SC16: the element is %d, the source %d", e, src);
	if (e != src && sc_page(e)) {
		sc_deep_struct("SC16 element", e, src, 0);
		int ea = sc_page(e)->values[am].i;
		struct page *pa = sc_page(ea) ? heap[ea].page : NULL;
		for (int i = 0; pa && i < pa->nr_vars && i < 2; i++)
			SC_CHECK(pa->values[i].i != objs[i], "SC16: element %d of the copied array is the source's object", i);
		for (int i = 0; i < 2; i++)
			for (int k = 0; k < nw; k++)
				SC_CHECK(hp_refs(w[i][k], wseq[i][k]) == 2, "SC16: wrapped object %d of element %d has %d reference(s), "
					 "want 2 (the source's element and its copy)", k, i, hp_refs(w[i][k], wseq[i][k]));
		/* a change to an element of the copy leaves the source's */
		if (pa && pa->nr_vars == 2 && sc_page(pa->values[0].i)) {
			int k = sc_first(tno, SC_NUM);
			sc_page(pa->values[0].i)->values[k].i += 55;
			SC_CHECK(sc_page(objs[0])->values[k].i == 3000 + 7 * k, "SC16: a change to the copy's element changed the source's");
		}
	}
	uint32_t eseq = e > 0 && heap_index_valid(e) ? heap[e].seq : 0;
	heap_unref(src);
	for (int i = 0; e != src && i < 2; i++)
		for (int k = 0; k < nw; k++)
			SC_CHECK(hp_refs(w[i][k], wseq[i][k]) == 1, "SC16: after the source went wrapped object %d of element %d has %d "
				 "reference(s), want 1", k, i, hp_refs(w[i][k], wseq[i][k]));
	heap_unref(arr);
	for (int i = 0; i < 2; i++)
		for (int k = 0; k < nw; k++)
			SC_CHECK(!hp_alive(w[i][k], wseq[i][k]), "SC16: wrapped object %d of element %d outlived both", k, i);
	SC_CHECK(!hp_alive(e, eseq), "SC16: the element outlived its array");
	while (hp_alive(src, sseq))
		heap_unref(src);
	SC_CHECK(live_slots() == live0, "SC16: %zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC16 nested members through an array of structs with wraps: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC19: -1 object members before the numbers --- */

static int sc19_pick(void)
{
	int best = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		if (s->nr_members <= 0 || s->nr_members > 64 || !s->name || !sc_quiet_dtor(i))
			continue;
		bool ok = true, obj = false, num_after = false;
		for (int m = 0; ok && m < s->nr_members; m++) {
			const struct ain_type *t = &s->members[m].type;
			switch (sc_kind_of(t)) {
			case SC_NUM:
				num_after |= obj;
				break;
			case SC_STR:
				break;
			case SC_STRUCT:
				ok = sc_flat(t->struc);
				obj = true;
				break;
			case SC_DG:
				obj = true;
				break;
			case SC_ARRAY:
				ok = t->rank <= 1 && t->array_type && t->array_type->data == AIN_INT;
				break;
			default:
				ok = false;
			}
		}
		if (ok && num_after && (best < 0 || s->nr_members < ain->structures[best].nr_members))
			best = i;
	}
	if (best < 0) {
		printf("  ENVIRONMENT: no struct with a struct or delegate member before a number\n");
		xc_env_fail("struct shape");
	}
	printf("  a struct with an object member before a number: struct %d (%d members)\n", best, ain->structures[best].nr_members);
	return best;
}

static void sc19_lazy_members(void)
{
	int sno = sc19_pick();
	xc_ready();
	size_t live0 = live_slots();
	int src = alloc_struct(sno);
	struct ain_struct *s = &ain->structures[sno];
	for (int m = 0; m < s->nr_members; m++)
		if (sc_kind_of(&s->members[m].type) == SC_NUM)
			sc_page(src)->values[m].i = 3100 + m;
	uint32_t sseq = heap[src].seq;
	int arr = hp_array(AIN_ARRAY_STRUCT, sno);
	hp_hll("Add", 0, 2, 2, (int[]){ arr, sc_handed(src) });
	int e = hp_elem(arr, 0);
	SC_CHECK(hp_numof(arr) == 1 && e != src && sc_page(e) && hp_refs(src, sseq) == 1, "SC19: element %d, the source %d (%d "
		 "reference(s))", e, src, hp_refs(src, sseq));
	for (int m = 0; e != src && sc_page(e) && m < s->nr_members; m++) {
		int a = sc_page(e)->values[m].i;
		switch (sc_kind_of(&s->members[m].type)) {
		case SC_NUM:
			SC_CHECK(a == 3100 + m, "SC19: number member %d is %d, want %d (the members after a -1 member are copied)", m, a,
				 3100 + m);
			break;
		case SC_STR:
			SC_CHECK(sc_default_text(a), "SC19: string member %d is not the default", m);
			break;
		case SC_STRUCT:
			SC_CHECK(a == -1 || (sc_page(a) && a != sc_page(src)->values[m].i), "SC19: struct member %d is slot %d", m, a);
			break;
		case SC_DG:
			SC_CHECK(a == -1 || (sc_page(a) && sc_page(a)->type == DELEGATE_PAGE && delegate_numof(sc_page(a)) == 0),
				 "SC19: delegate member %d is not the default", m);
			break;
		default:
			break;
		}
	}
	heap_unref(src);
	heap_unref(arr);
	while (hp_alive(src, sseq))
		heap_unref(src);
	/* Fill#0: an element whose struct member is an object gets the defaults
	 * into that object when the value's member is -1 (the value's member is
	 * the original's default object; 0x656eb6 assigns into the element's) */
	int ms = -1;
	for (int m = 0; m < s->nr_members && ms < 0; m++)
		if (sc_kind_of(&s->members[m].type) == SC_STRUCT)
			ms = m;
	if (ms >= 0) {
		int c = s->members[ms].type.struc;
		int el = alloc_struct(sno), mobj = alloc_struct(c);
		sc_fill_base(mobj, 1000);
		sc_put_member(el, ms, mobj);
		arr = sc_array_of(sno, 1, (int[]){ el });
		src = alloc_struct(sno);
		sseq = heap[src].seq;
		oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(src) }, (int[2]){ 0 }, 2);
		int k = sc_first(c, SC_NUM);
		SC_CHECK(hp_elem(arr, 0) == el && sc_page(el) && sc_page(el)->values[ms].i == mobj && sc_page(mobj)
			 && sc_page(mobj)->values[k].i == 0,
			 "SC19 Fill#0 with a -1 struct member: the element's member is slot %d (was %d), number %d; want the same "
			 "object with the default 0", sc_page(el) ? sc_page(el)->values[ms].i : -1, mobj,
			 sc_page(mobj) ? sc_page(mobj)->values[k].i : -1);
		heap_unref(src);
		heap_unref(arr);
		while (hp_alive(src, sseq))
			heap_unref(src);
	}
	SC_CHECK(live_slots() == live0, "SC19: %zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC19 a value with -1 object members before its numbers: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC31: a value with only the handed-over reference --- */

static void sc31_temporary(void)
{
	int sno = sc_flat_struct(), dno = sc_pick_dtor();
	xc_ready();
	static const char *how[] = { "Add", "PushBack", "Insert" };
	for (int h = 0; h < 3; h++) {
		size_t live0 = live_slots();
		int target = alloc_struct(sno);
		int v = sc_new(sno, target);	/* its one reference is the handed one */
		uint32_t vseq = heap[v].seq;
		int arr = hp_array(AIN_ARRAY_STRUCT, sno);
		if (h == 2)
			hp_hll(how[h], 0, 2, 3, (int[]){ arr, 0, v });
		else
			hp_hll(how[h], 0, 2, 2, (int[]){ arr, v });
		int e = hp_elem(arr, 0);
		SC_CHECK(hp_numof(arr) == 1 && e != v && sc_page(e) && !hp_alive(v, vseq) && HEAP_REF(e) == 1,
			 "SC31 %s of a temporary: element %d (the value %d, %s), want a new object and the value gone", how[h], e, v,
			 hp_alive(v, vseq) ? "alive" : "gone");
		if (sc_page(e) && e != v) {
			struct page *p = sc_page(e);
			for (int m = 0; m < p->nr_vars; m++) {
				enum sc_kind k = sc_kind_of(&ain->structures[sno].members[m].type);
				char t[16];
				int n = snprintf(t, sizeof t, "m%d-2000", m);
				if (k == SC_NUM)
					SC_CHECK(p->values[m].i == 2000 + 7 * m, "SC31 %s: number member %d is %d", how[h], m, p->values[m].i);
				else if (k == SC_STR)
					SC_CHECK(sc_text_is(p->values[m].i, t, n), "SC31 %s: string member %d is not the value's", how[h], m);
			}
		}
		heap_unref(arr);
		while (hp_alive(v, vseq))
			heap_unref(v);
		heap_unref(target);
		SC_CHECK(live_slots() == live0, "SC31 %s: %zu live slot(s) after, %zu before", how[h], live_slots(), live0);
	}
	/* with a destructor: the temporary's runs in the call, the element's with the array */
	size_t live0 = live_slots();
	int v = alloc_struct(dno), arr = hp_array(AIN_ARRAY_STRUCT, dno);
	uint32_t vseq = heap[v].seq;
	unsigned long d0 = sc_dtor_calls(dno);
	hp_hll("Add", 0, 2, 2, (int[]){ arr, v });
	int e = hp_elem(arr, 0);
	SC_CHECK(e != v && sc_page(e) && !hp_alive(v, vseq) && sc_dtor_calls(dno) - d0 == 1,
		 "SC31 a temporary with a destructor: element %d (value %d %s), the destructor ran %lu time(s) in the call, want 1",
		 e, v, hp_alive(v, vseq) ? "alive" : "gone", sc_dtor_calls(dno) - d0);
	heap_unref(arr);
	SC_CHECK(sc_dtor_calls(dno) - d0 == 2, "SC31: after the array went the destructor ran %lu time(s), want 2",
		 sc_dtor_calls(dno) - d0);
	while (hp_alive(v, vseq))
		heap_unref(v);
	SC_CHECK(live_slots() == live0, "SC31: %zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC31 a temporary value: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC32: the value is an element of the same array --- */

static void sc32_self(void)
{
	int flat = sc_flat_struct(), wrap = sc_wrap_struct();
	xc_ready();
	{
		size_t live0 = live_slots();
		int objs[2];
		int arr = sc_filled_array(flat, 2, objs);
		sc_fill_base(objs[0], 5000);
		int ms = sc_first(flat, SC_STR), mn = sc_first(flat, SC_NUM);
		char keep[16];
		int kn = snprintf(keep, sizeof keep, "m%d-5000", ms);
		int out[2] = { -9, -9 };
		int left = oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(objs[0]) }, out, 2);
		SC_CHECK(left == 1 && out[0] == 2, "SC32 Fill#0 of arr[0]: returned %d", out[0]);
		bool in_place = hp_elem(arr, 0) == objs[0] && hp_elem(arr, 1) == objs[1] && sc_page(objs[0]) && sc_page(objs[1]);
		SC_CHECK(in_place && HEAP_REF(objs[0]) == 1,
			 "SC32 Fill#0 of arr[0]: the elements are %d,%d, want %d,%d (in place), element 0 with %d reference(s)",
			 hp_elem(arr, 0), hp_elem(arr, 1), objs[0], objs[1], sc_page(objs[0]) ? HEAP_REF(objs[0]) : 0);
		if (in_place) {
			SC_CHECK(sc_page(objs[0])->values[mn].i == 5000 + 7 * mn && sc_text_is(sc_page(objs[0])->values[ms].i, keep, kn),
				 "SC32 Fill#0 of arr[0]: element 0 changed");
			SC_CHECK(sc_page(objs[1])->values[mn].i == 5000 + 7 * mn && sc_text_is(sc_page(objs[1])->values[ms].i, keep, kn)
				 && sc_page(objs[1])->values[ms].i != sc_page(objs[0])->values[ms].i,
				 "SC32 Fill#0 of arr[0]: element 1 does not hold its own copy of element 0's content");
		}
		int e0 = hp_elem(arr, 0);
		hp_hll("Add", 0, 2, 2, (int[]){ arr, sc_handed(e0) });
		int e = hp_elem(arr, 2);
		SC_CHECK(hp_numof(arr) == 3 && e != e0 && e != hp_elem(arr, 1) && sc_page(e), "SC32 Add of arr[0]: element 2 is %d", e);
		if (sc_page(e) && sc_page(e0) && e != e0)
			sc_compare("SC32 Add of arr[0]", e, e0, NULL);
		heap_unref(arr);
		SC_CHECK(live_slots() == live0, "SC32: %zu live slot(s) after, %zu before", live_slots(), live0);
	}
	{
		size_t live0 = live_slots();
		int objs[2];
		int arr = sc_filled_array(wrap, 2, objs);
		int mw = sc_first(wrap, SC_WRAP);
		int w0 = sc_page(objs[0])->values[mw].i, w1 = sc_page(objs[1])->values[mw].i;
		uint32_t s0 = w0 > 0 ? heap[w0].seq : 0, s1 = w1 > 0 ? heap[w1].seq : 0;
		int r0 = hp_refs(w0, s0);
		oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(objs[0]) }, (int[2]){ 0 }, 2);
		bool in_place = hp_elem(arr, 0) == objs[0] && hp_elem(arr, 1) == objs[1] && sc_page(objs[0]) && sc_page(objs[1]);
		SC_CHECK(in_place, "SC32 Fill#0 of arr[0] (wrap): elements replaced");
		if (in_place) {
			SC_CHECK(sc_page(objs[0])->values[mw].i == w0 && sc_page(objs[1])->values[mw].i == w0 && hp_refs(w0, s0) == r0 + 1,
				 "SC32 Fill#0 of arr[0] (wrap): wraps %d,%d, want %d twice with %d reference(s) (has %d)",
				 sc_page(objs[0])->values[mw].i, sc_page(objs[1])->values[mw].i, w0, r0 + 1, hp_refs(w0, s0));
			SC_CHECK(w1 == w0 || !hp_alive(w1, s1), "SC32 Fill#0 of arr[0] (wrap): element 1's old wrapped object is still alive");
		}
		heap_unref(arr);
		SC_CHECK(!hp_alive(w0, s0), "SC32 (wrap): the shared object outlived the array");
		SC_CHECK(live_slots() == live0, "SC32 (wrap): %zu live slot(s) after, %zu before", live_slots(), live0);
	}
	printf("SC32 the value is an element of the same array: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC33: a struct of another member count --- */

/* A struct of numbers and strings (one number at least) other than `not`:
 * of another member count than not_n, or (same) of the same. */
static int sc33_pick_other(int not_n, bool same, int not)
{
	int best = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		bool ok = s->nr_members > 0 && s->nr_members <= 64 && (same ? s->nr_members == not_n : s->nr_members != not_n)
			&& i != not && s->name && sc_quiet_dtor(i);
		int nums = 0;
		for (int m = 0; ok && m < s->nr_members; m++) {
			enum sc_kind k = sc_kind_of(&s->members[m].type);
			ok = k == SC_NUM || k == SC_STR;
			nums += k == SC_NUM;
		}
		ok = ok && nums > 0;
		if (ok && (best < 0 || s->nr_members < ain->structures[best].nr_members))
			best = i;
	}
	if (best < 0)
		xc_env_fail("no other struct");
	printf("  a struct of %s member count: struct %d (%d members)\n", same ? "the same" : "another", best,
	       ain->structures[best].nr_members);
	return best;
}

static void sc33_other_struct(void)
{
	int sno = sc_flat_struct();
	int ono = sc33_pick_other(ain->structures[sno].nr_members, false, sno);
	int qno = sc33_pick_other(ain->structures[sno].nr_members, true, sno);
	xc_ready();
	size_t live0 = live_slots();
	int v = alloc_struct(ono);
	sc_fill_base(v, 7000);
	uint32_t vseq = heap[v].seq;
	int arr = hp_array(AIN_ARRAY_STRUCT, sno);
	hp_hll("Add", 0, 2, 2, (int[]){ arr, sc_handed(v) });
	int e = hp_elem(arr, 0);
	struct page *p = sc_page(e);
	SC_CHECK(hp_numof(arr) == 1 && p && p->type == STRUCT_PAGE && p->index == sno && e != v,
		 "SC33 Add of another struct: element %d (%s, struct %d), want a new object of struct %d", e, p ? "a page" : "no page",
		 p ? p->index : -1, sno);
	for (int m = 0; p && p->index == sno && m < p->nr_vars; m++) {
		enum sc_kind k = sc_kind_of(&ain->structures[sno].members[m].type);
		if (k == SC_NUM)
			SC_CHECK(p->values[m].i == 0, "SC33: number member %d is %d, want the default 0", m, p->values[m].i);
		else if (k == SC_STR)
			SC_CHECK(sc_default_text(p->values[m].i), "SC33: string member %d is not the default", m);
	}
	SC_CHECK(hp_refs(v, vseq) == 1 && sc_page(v)->values[sc_first(ono, SC_NUM)].i == 7000 + 7 * sc_first(ono, SC_NUM),
		 "SC33: the value has %d reference(s) or changed", hp_refs(v, vseq));
	heap_unref(arr);
	int objs[2];
	arr = sc_filled_array(sno, 2, objs);
	int out[2] = { -9, -9 };
	oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(v) }, out, 2);
	int mn = sc_first(sno, SC_NUM);
	SC_CHECK(out[0] == 2 && hp_elem(arr, 0) == objs[0] && hp_elem(arr, 1) == objs[1]
		 && sc_page(objs[0])->values[mn].i == 1000 + 7 * mn && sc_page(objs[1])->values[mn].i == 1000 + 7 * mn,
		 "SC33 Fill#0 of another struct: returned %d, elements %d,%d, numbers %d,%d; want 2, unchanged", out[0],
		 hp_elem(arr, 0), hp_elem(arr, 1), sc_page(objs[0]) ? sc_page(objs[0])->values[mn].i : -1,
		 sc_page(objs[1]) ? sc_page(objs[1])->values[mn].i : -1);
	heap_unref(arr);
	heap_unref(v);
	while (hp_alive(v, vseq))
		heap_unref(v);
	/* another struct of the same member count: natively 0x681820 would assign
	 * member by member (it compares the counts only); this VM keeps the older
	 * handling for it (port strategy: the member kinds may differ), the value
	 * itself stored with the array's reference */
	int q = alloc_struct(qno);
	sc_fill_base(q, 7100);
	uint32_t qseq = heap[q].seq;
	arr = hp_array(AIN_ARRAY_STRUCT, sno);
	int hq = sc_handed(q);
	hp_hll("Add", 0, 2, 2, (int[]){ arr, hq });
	SC_CHECK(hp_numof(arr) == 1 && hp_elem(arr, 0) == hq && (hq != q || hp_refs(q, qseq) == 2),
		 "SC33 Add of a struct of the same member count: element %d (handed %d, the value %d with %d reference(s)); "
		 "this VM's older handling stores the handed value", hp_elem(arr, 0), hq, q, hp_refs(q, qseq));
	heap_unref(arr);
	heap_unref(q);
	while (hp_alive(q, qseq))
		heap_unref(q);
	SC_CHECK(live_slots() == live0, "SC33: %zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC33 a struct of another member count: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC34: Copy and AddRange keep this branch's handling (guard) --- */

static int sc_ordinal(const char *name, int nargs, int nth_arg_wrap)
{
	int lib = bu_lib("Array"), seen = 0;
	for (int j = 0; j < ain->libraries[lib].nr_functions; j++) {
		struct ain_hll_function *f = &ain->libraries[lib].functions[j];
		if (strcmp(f->name, name))
			continue;
		if (f->nr_arguments == nargs && (nth_arg_wrap < 0 || f->arguments[nth_arg_wrap].type.data == AIN_WRAP))
			return seen;
		seen++;
	}
	xc_env_fail("overload");
	return -1;
}

static void sc34_copy_guard(void)
{
	int sno = sc_flat_struct();
	int copy_all = sc_ordinal("Copy", 2, 1), add_range = sc_ordinal("AddRange", 2, -1);
	xc_ready();
	size_t live0 = live_slots();
	int d[2], s[2];
	int dst = sc_filled_array(sno, 2, d), src = sc_filled_array(sno, 2, s);
	for (int i = 0; i < 2; i++)
		sc_fill_base(s[i], 8000 + 100 * i);
	uint32_t dseq0 = heap[d[0]].seq;
	int out[2] = { -9, -9 };
	oa_hll("Copy", copy_all, 2, 2, (int[]){ dst, src }, out, 2);
	int e0 = hp_elem(dst, 0), e1 = hp_elem(dst, 1);
	int mn = sc_first(sno, SC_NUM);
	printf("  SC34 Copy: returned %d; elements %d,%d (were %d,%d; source %d,%d); old element 0 %s; numbers %d,%d\n", out[0], e0,
	       e1, d[0], d[1], s[0], s[1], hp_alive(d[0], dseq0) ? "alive" : "gone", sc_page(e0) ? sc_page(e0)->values[mn].i : -1,
	       sc_page(e1) ? sc_page(e1)->values[mn].i : -1);
	SC_CHECK(out[0] == 2 && sc_page(e0) && sc_page(e1) && sc_page(e0)->values[mn].i == 8000 + 7 * mn
		 && sc_page(e1)->values[mn].i == 8100 + 7 * mn && e0 != s[0] && e1 != s[1] && e0 != d[0] && !hp_alive(d[0], dseq0),
		 "SC34 Copy: this branch's rule replaces each element by a copy of the source's");
	heap_unref(dst);
	int d2[1];
	dst = sc_filled_array(sno, 1, d2);
	hp_hll("AddRange", add_range, 2, 2, (int[]){ dst, src });
	printf("  SC34 AddRange: %d element(s): %d,%d,%d (source %d,%d); source element 0 with %d reference(s)\n", hp_numof(dst),
	       hp_elem(dst, 0), hp_elem(dst, 1), hp_elem(dst, 2), s[0], s[1], HEAP_REF(s[0]));
	SC_CHECK(hp_numof(dst) == 3 && hp_elem(dst, 0) == d2[0] && hp_elem(dst, 1) == s[0] && hp_elem(dst, 2) == s[1]
		 && HEAP_REF(s[0]) == 2, "SC34 AddRange: this branch's rule appends the source's objects, shared");
	heap_unref(dst);
	heap_unref(src);
	SC_CHECK(live_slots() == live0, "SC34: %zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC34 Copy and AddRange keep this branch's handling: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC35: array<string> and array<struct> members --- */

static int sc35_pick(bool of_struct)
{
	int best = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		if (s->nr_members <= 0 || s->nr_members > 64 || !s->name || !sc_quiet_dtor(i))
			continue;
		bool ok = true;
		int arrays = 0;
		for (int m = 0; ok && m < s->nr_members; m++) {
			const struct ain_type *t = &s->members[m].type;
			enum sc_kind k = sc_kind_of(t);
			if (k == SC_NUM || k == SC_STR)
				continue;
			ok = k == SC_ARRAY && t->rank <= 1 && t->array_type
				&& (of_struct ? t->array_type->data == AIN_STRUCT && sc_flat(t->array_type->struc)
					      : t->array_type->data == AIN_STRING);
			arrays += ok;
		}
		if (ok && arrays && (best < 0 || s->nr_members < ain->structures[best].nr_members))
			best = i;
	}
	if (best < 0) {
		printf("  ENVIRONMENT: no struct with an %s member\n", of_struct ? "array<struct>" : "array<string>");
		xc_env_fail("struct shape");
	}
	printf("  a struct with %s members: struct %d (%d members)\n", of_struct ? "array<struct>" : "array<string>", best,
	       ain->structures[best].nr_members);
	return best;
}

static void sc35_one(int sno, bool of_struct)
{
	size_t live0 = live_slots();
	int src = alloc_struct(sno);
	sc_fill_base(src, 2000);
	struct ain_struct *s = &ain->structures[sno];
	int am = -1;
	for (int m = 0; m < s->nr_members; m++) {
		const struct ain_type *t = &s->members[m].type;
		if (sc_kind_of(t) != SC_ARRAY)
			continue;
		am = m;
		if (of_struct) {
			int o[2];
			for (int i = 0; i < 2; i++) {
				o[i] = alloc_struct(t->array_type->struc);
				sc_fill_base(o[i], 3000 + 100 * i);
			}
			sc_put_member(src, m, sc_array_of(t->array_type->struc, 2, o));
		} else {
			sc_put_member(src, m, sc_str_array(2, "a"));
		}
	}
	uint32_t sseq = heap[src].seq;
	int arr = hp_array(AIN_ARRAY_STRUCT, sno);
	unsigned long call0 = sc_calls_total();
	hp_hll("Add", 0, 2, 2, (int[]){ arr, sc_handed(src) });
	const char *what = of_struct ? "SC35 array<struct>" : "SC35 array<string>";
	SC_CHECK(sc_calls_total() == call0, "%s: %lu function(s) ran during the store", what, sc_calls_total() - call0);
	int e = hp_elem(arr, 0);
	SC_CHECK(hp_numof(arr) == 1 && e != src && sc_page(e) && hp_refs(src, sseq) == 1, "%s: element %d, the source %d", what, e,
		 src);
	if (e != src && sc_page(e)) {
		sc_deep_struct(what, e, src, 0);
		int ea = sc_page(e)->values[am].i, sa = sc_page(src)->values[am].i;
		struct page *pa = sc_page(ea) ? heap[ea].page : NULL, *ps = heap[sa].page;
		if (pa && ea != sa && pa->nr_vars == 2) {
			if (of_struct) {
				int k = sc_first(heap[pa->values[0].i].page->index, SC_NUM);
				sc_page(pa->values[0].i)->values[k].i += 9;
				SC_CHECK(sc_page(ps->values[0].i)->values[k].i == 3000 + 7 * k,
					 "%s: a change to the copy's element changed the source's", what);
			} else {
				string_push_back(&heap[pa->values[0].i].s, 'x');
				SC_CHECK(sc_text_is(ps->values[0].i, "a0", 2), "%s: a change in place to the copy's string changed the "
					 "source's", what);
			}
		}
	}
	heap_unref(src);
	heap_unref(arr);
	while (hp_alive(src, sseq))
		heap_unref(src);
	SC_CHECK(live_slots() == live0, "%s: %zu live slot(s) after, %zu before", what, live_slots(), live0);
}

static void sc35_arrays(void)
{
	int a = sc35_pick(false), b = sc35_pick(true);
	xc_ready();
	sc35_one(a, false);
	sc35_one(b, true);
	printf("SC35 array<string> and array<struct> members: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC36: twenty Adds --- */

static void sc36_many(void)
{
	int sno = sc_flat_struct();
	xc_ready();
	size_t live0 = live_slots();
	enum { N = 20 };
	int src[N];
	uint32_t seq[N];
	int arr = hp_array(AIN_ARRAY_STRUCT, sno);
	for (int i = 0; i < N; i++) {
		src[i] = alloc_struct(sno);
		sc_fill_base(src[i], 6000 + 10 * i);
		seq[i] = heap[src[i]].seq;
		hp_hll("Add", 0, 2, 2, (int[]){ arr, sc_handed(src[i]) });
	}
	SC_CHECK(hp_numof(arr) == N, "SC36: %d element(s)", hp_numof(arr));
	for (int i = 0; i < N && i < hp_numof(arr); i++) {
		char w[32];
		snprintf(w, sizeof w, "SC36 element %d", i);
		SC_CHECK(hp_refs(src[i], seq[i]) == 1, "%s: the source has %d reference(s)", w, hp_refs(src[i], seq[i]));
		sc_compare(w, hp_elem(arr, i), src[i], NULL);
	}
	for (int i = 0; i < N; i++)
		heap_unref(src[i]);
	heap_unref(arr);
	SC_CHECK(live_slots() == live0, "SC36: %zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC36 twenty Adds: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC37: shapes this VM does not copy keep the older handling (guard) --- */

/* A member kind the original cannot assign (0x656fc8: a long int, type 91,
 * 100 and the function type 27) or this VM does not copy (an option of a
 * reference payload, a nested array of rank > 1 or of delegates, arrays,
 * options or two-slot elements). */
static bool sc37_unsupported(const struct ain_type *t)
{
	switch (t->data) {
	case AIN_LONG_INT: case AIN_ENUM2: case AIN_IFACE_WRAP: case AIN_FUNC_TYPE:
		return true;
	case AIN_OPTION:
		return !ain_option_type_is_value(t);
	case AIN_ARRAY:
		if (t->rank > 1 || !t->array_type)
			return true;
		switch (t->array_type->data) {
		case AIN_DELEGATE: case AIN_ARRAY: case AIN_OPTION:
			return true;
		default:
			return ss_type_slot_count(t->array_type) > 1;
		}
	default:
		return false;
	}
}

/* The struct with the fewest members whose members are numbers, strings
 * and (in_array: arrays of a struct that has) such a member. */
static int sc37_pick(bool in_array)
{
	int best = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		if (s->nr_members <= 0 || s->nr_members > 64 || !s->name || !sc_quiet_dtor(i))
			continue;
		bool ok = true;
		int bad = 0;
		for (int m = 0; ok && m < s->nr_members; m++) {
			const struct ain_type *t = &s->members[m].type;
			enum sc_kind k = sc_kind_of(t);
			if (k == SC_NUM || k == SC_STR || k == SC_VOID)
				continue;
			if (!in_array) {
				ok = sc37_unsupported(t);
				bad += ok;
				continue;
			}
			ok = k == SC_ARRAY && t->rank <= 1 && t->array_type && t->array_type->data == AIN_STRUCT;
			if (ok) {
				struct ain_struct *e = &ain->structures[t->array_type->struc];
				bool has = false;
				for (int j = 0; j < e->nr_members; j++)
					has |= sc37_unsupported(&e->members[j].type);
				bad += has;
				ok = has;
			}
		}
		if (ok && bad && (best < 0 || s->nr_members < ain->structures[best].nr_members))
			best = i;
	}
	if (best < 0) {
		printf("  ENVIRONMENT: no struct with %s a member this VM does not copy\n", in_array ? "an array of structs with" : "");
		xc_env_fail("struct shape");
	}
	printf("  a struct with %sa member this VM does not copy: struct %d (%d members)\n", in_array ? "an array of structs with " : "",
	       best, ain->structures[best].nr_members);
	return best;
}

static void sc37_one(int sno, const char *what)
{
	size_t live0 = live_slots();
	int arr = hp_array(AIN_ARRAY_STRUCT, sno);
	for (int k = 0; k < 2; k++) {	/* twice: the second asks again */
		int s = alloc_struct(sno);
		uint32_t seq = heap[s].seq;
		int h = sc_handed(s);
		hp_hll("Add", 0, 2, 2, (int[]){ arr, h });
		SC_CHECK(hp_numof(arr) == k + 1 && hp_elem(arr, k) == h && (h != s || hp_refs(s, seq) == 2),
			 "%s, Add %d: element %d (handed %d, the value %d with %d reference(s)); the older handling stores the handed "
			 "value", what, k, hp_elem(arr, k), h, s, hp_refs(s, seq));
		heap_unref(s);
	}
	heap_unref(arr);
	SC_CHECK(live_slots() == live0, "%s: %zu live slot(s) after, %zu before", what, live_slots(), live0);
}

/* A struct of numbers and strings with a struct member whose struct has a
 * member this VM does not copy (its other members numbers, strings and
 * voids); -1 if this AIN has none. */
static int sc37_pick_in_struct(void)
{
	int best = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		if (s->nr_members <= 0 || s->nr_members > 64 || !s->name || !sc_quiet_dtor(i))
			continue;
		bool ok = true;
		int n = 0;
		for (int m = 0; ok && m < s->nr_members; m++) {
			const struct ain_type *t = &s->members[m].type;
			enum sc_kind k = sc_kind_of(t);
			if (k == SC_NUM || k == SC_STR)
				continue;
			ok = k == SC_STRUCT && t->struc >= 0 && t->struc < ain->nr_structures;
			if (!ok)
				break;
			struct ain_struct *c = &ain->structures[t->struc];
			int bad = 0;
			for (int j = 0; ok && j < c->nr_members; j++) {
				enum sc_kind ck = sc_kind_of(&c->members[j].type);
				if (ck == SC_NUM || ck == SC_STR || ck == SC_VOID)
					continue;
				ok = sc37_unsupported(&c->members[j].type);
				bad += ok;
			}
			ok = ok && bad;
			n += ok;
		}
		if (ok && n && (best < 0 || s->nr_members < ain->structures[best].nr_members))
			best = i;
	}
	printf("  SC37 a struct member with a member this VM does not copy: %s %d\n", best >= 0 ? "struct" : "no struct of this AIN",
	       best);
	return best;
}

/* One kind of sc37_unsupported at a time. */
static int sc37_kind(const struct ain_type *t)
{
	switch (t->data) {
	case AIN_LONG_INT: return 1;
	case AIN_ENUM2: return 2;
	case AIN_IFACE_WRAP: return 3;
	case AIN_FUNC_TYPE: return 4;
	case AIN_OPTION: return 5;
	case AIN_ARRAY:
		if (t->rank > 1 || !t->array_type)
			return 6;
		switch (t->array_type->data) {
		case AIN_DELEGATE: return 7;
		case AIN_ARRAY: return 8;
		case AIN_OPTION: return 9;
		default: return 10;	/* two-slot elements */
		}
	default:
		return 0;
	}
}

/* The struct with the fewest members whose members are numbers, strings,
 * voids and members of kind `kind` alone; -1 if this AIN has none. */
static int sc37_pick_kind(int kind)
{
	int best = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		if (s->nr_members <= 0 || s->nr_members > 64 || !s->name || !sc_quiet_dtor(i))
			continue;
		bool ok = true;
		int n = 0;
		for (int m = 0; ok && m < s->nr_members; m++) {
			const struct ain_type *t = &s->members[m].type;
			enum sc_kind k = sc_kind_of(t);
			if (k == SC_NUM || k == SC_STR || k == SC_VOID)
				continue;
			ok = sc37_unsupported(t) && sc37_kind(t) == kind;
			n += ok;
		}
		if (ok && n && (best < 0 || s->nr_members < ain->structures[best].nr_members))
			best = i;
	}
	return best;
}

static void sc37_unsupported_shapes(void)
{
	int a = sc37_pick(false), b = sc37_pick(true), c = sc37_pick_in_struct();
	/* evidence for the record: void members that are not the trailing
	 * slots of a multi-slot member (the copy skips those slots) */
	int stray = 0;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		for (int m = 0; m < s->nr_members; m++) {
			int slots = ss_type_slot_count(&s->members[m].type);
			if (s->members[m].type.data == AIN_VOID)
				stray++;
			else if (slots > 1)
				m += slots - 1;
		}
	}
	printf("  SC37 void members outside a multi-slot member's slots in this AIN: %d\n", stray);
	int per[11];
	for (int k = 1; k <= 10; k++) {
		per[k] = sc37_pick_kind(k);
		printf("  SC37 kind %d: %s %d\n", k, per[k] >= 0 ? "struct" : "no struct of this AIN", per[k]);
	}
	xc_ready();
	sc37_one(a, "SC37 a member this VM does not copy");
	sc37_one(b, "SC37 an array of structs with such a member");
	if (c >= 0)
		sc37_one(c, "SC37 a struct member with such a member");
	for (int k = 1; k <= 10; k++) {
		char w[48];
		snprintf(w, sizeof w, "SC37 member kind %d", k);
		if (per[k] >= 0)
			sc37_one(per[k], w);
	}
	printf("SC37 shapes this VM does not copy keep the older handling: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC38: an array member whose page is of another kind than declared --- */

/* An array<string> member holding a page this VM made without knowing the
 * element type: a page of numbers with 0 in every cell (an older resize)
 * and a generic page holding strings. The copy is a new page with the same
 * values, each one its teardown releases retained once more (port strategy:
 * shared, as before; natively the array knows its element type). */
static void sc38_other_pages(void)
{
	int sno = sc35_pick(false);
	xc_ready();
	struct ain_struct *s = &ain->structures[sno];
	int am = -1;
	for (int m = 0; m < s->nr_members && am < 0; m++)
		if (sc_kind_of(&s->members[m].type) == SC_ARRAY)
			am = m;
	{
		/* a slot without a page (as variable_initval makes an array): the
		 * copy is a slot of its own without a page */
		size_t live0 = live_slots();
		int src = alloc_struct(sno);
		int a = heap_alloc_slot(VM_PAGE);
		heap_set_page(a, NULL);
		sc_put_member(src, am, a);
		uint32_t seq = heap[src].seq;
		int arr = hp_array(AIN_ARRAY_STRUCT, sno);
		hp_hll("Add", 0, 2, 2, (int[]){ arr, sc_handed(src) });
		int e = hp_elem(arr, 0);
		int ea = sc_page(e) && e != src ? sc_page(e)->values[am].i : -1;
		SC_CHECK(e != src && ea != a && ea > 1 && heap_index_valid(ea) && heap[ea].type == VM_PAGE && !heap[ea].page
			 && HEAP_REF(ea) == 1, "SC38 a slot without a page: the copy's array is slot %d (the source's %d)", ea, a);
		heap_unref(src);
		heap_unref(arr);
		while (hp_alive(src, seq))
			heap_unref(src);
		SC_CHECK(live_slots() == live0, "SC38 a slot without a page: %zu live slot(s) after, %zu before", live_slots(), live0);
	}
	for (int kind = 0; kind < 2; kind++) {
		size_t live0 = live_slots();
		int src = alloc_struct(sno);
		union vm_value dim = { .i = kind ? 2 : 3 };
		int a = heap_alloc_page(alloc_array(1, &dim, kind ? AIN_ARRAY : AIN_ARRAY_INT, -1, false));
		int strs[2] = { -1, -1 };
		uint32_t sseqs[2] = { 0, 0 };
		for (int i = 0; i < dim.i; i++) {
			int old = heap[a].page->values[i].i;
			if (kind) {
				strs[i] = sf_str(i ? "p1" : "p0", 2);
				sseqs[i] = heap[strs[i]].seq;
				heap[a].page->values[i].i = strs[i];
				if (old > 1 && heap_index_valid(old) && HEAP_REF(old) > 0)
					heap_unref(old);
			} else {
				heap[a].page->values[i].i = 0;
			}
		}
		sc_put_member(src, am, a);
		uint32_t seq = heap[src].seq;
		int arr = hp_array(AIN_ARRAY_STRUCT, sno);
		hp_hll("Add", 0, 2, 2, (int[]){ arr, sc_handed(src) });
		int e = hp_elem(arr, 0);
		const char *what = kind ? "SC38 a generic page of strings" : "SC38 a page of numbers in an array<string>";
		SC_CHECK(e != src && sc_page(e), "%s: element %d, the source %d", what, e, src);
		if (e != src && sc_page(e)) {
			int ea = sc_page(e)->values[am].i;
			struct page *pa = ea > 1 && heap_index_valid(ea) ? heap[ea].page : NULL;
			SC_CHECK(ea != a && pa && pa->a_type == heap[a].page->a_type && pa->nr_vars == dim.i, "%s: the copy's array is slot "
				 "%d (the source's %d)", what, ea, a);
			for (int i = 0; pa && pa->nr_vars == dim.i && i < dim.i; i++)
				SC_CHECK(pa->values[i].i == heap[a].page->values[i].i, "%s: cell %d is %d, the source's %d", what, i,
					 pa->values[i].i, heap[a].page->values[i].i);
			for (int i = 0; kind && i < 2; i++)
				SC_CHECK(hp_refs(strs[i], sseqs[i]) == 2, "%s: string %d has %d reference(s), want 2 (both pages)", what, i,
					 hp_refs(strs[i], sseqs[i]));
		}
		heap_unref(src);
		heap_unref(arr);
		while (hp_alive(src, seq))
			heap_unref(src);
		for (int i = 0; kind && i < 2; i++)
			SC_CHECK(!hp_alive(strs[i], sseqs[i]), "%s: string %d outlived both pages", what, i);
		SC_CHECK(live_slots() == live0, "%s: %zu live slot(s) after, %zu before", what, live_slots(), live0);
	}
	printf("SC38 array members of another page kind: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC39, SC40: the objects only this VM makes for a copy --- */

/* This AIN has no struct whose wrap's inner struct has a destructor that
 * is a bare RETURN, so the cases lend one: the destructor of a struct that
 * has such a destructor (sc_pick_dtor) is set, for the case's run only, as
 * the destructor of the struct whose objects are watched (synthetic; the
 * AIN in memory, restored before the case ends). */
static int sc_lent_fn = -1;

static int sc_lend_dtor(int t)
{
	if (sc_lent_fn < 0)
		sc_lent_fn = ain->structures[sc_pick_dtor()].destructor;
	int was = ain->structures[t].destructor;
	ain->structures[t].destructor = sc_lent_fn;
	return was;
}

static unsigned long sc_lent_calls(void)
{
	return sc_lent_fn > 0 && sc_lent_fn < 40000 ? calls[sc_lent_fn] : 0;
}

static bool sc_is_wrap_struct(const struct ain_type *t)
{
	return t->data == AIN_WRAP && t->array_type && t->array_type->data == AIN_STRUCT && t->struc >= 0
	       && t->struc < ain->nr_structures;
}

/* The first non-first wrap<struct> member of struct t, or -1. */
static int sc_inner_wrap(int t)
{
	struct ain_struct *s = &ain->structures[t];
	for (int m = 1; m < s->nr_members; m++)
		if (sc_is_wrap_struct(&s->members[m].type))
			return m;
	return -1;
}

/* SC39: numbers, strings and non-first wrap<T> members (at least one);
 * depth 1: a T that has a non-first wrap<U> member itself. The struct whose
 * destructor is watched: T (depth 0) or U (depth 1). -1 if none. */
static int sc39_pick_at(int *wm, int depth, int *watched)
{
	int best = -1, bw = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		if (s->nr_members <= 0 || s->nr_members > 64 || !s->name || !sc_quiet_dtor(i))
			continue;
		bool ok = true;
		int w = -1;
		for (int m = 0; ok && m < s->nr_members; m++) {
			const struct ain_type *t = &s->members[m].type;
			enum sc_kind k = sc_kind_of(t);
			if (k == SC_NUM || k == SC_STR)
				continue;
			ok = k == SC_WRAP && m > 0 && sc_is_wrap_struct(t) && sc_quiet_dtor(t->struc);
			if (ok && w < 0 && (depth == 0 || sc_inner_wrap(t->struc) >= 0))
				w = m;
		}
		if (ok && w >= 0 && (best < 0 || s->nr_members < ain->structures[best].nr_members)) {
			best = i;
			bw = w;
		}
	}
	if (best < 0) {
		printf("  SC39 depth %d: no struct of this AIN\n", depth);
		return -1;
	}
	int tno = ain->structures[best].members[bw].type.struc;
	*watched = depth == 0 ? tno : ain->structures[tno].members[sc_inner_wrap(tno)].type.struc;
	printf("  SC39 depth %d: struct %d (%d members), member %d of struct %d, watched struct %d\n", depth, best,
	       ain->structures[best].nr_members, bw, tno, *watched);
	*wm = bw;
	return best;
}

static void sc39_one(int sno, int wm, int watched, int depth)
{
	size_t live0 = live_slots();
	int was = sc_lend_dtor(watched);
	int src = alloc_struct(sno);
	sc_fill_base(src, 2000);
	uint32_t sseq = heap[src].seq;
	int w = sc_page(src)->values[wm].i;
	uint32_t wseq = w > 0 ? heap[w].seq : 0;
	int arr = hp_array(AIN_ARRAY_STRUCT, sno);
	int v = sc_handed(src);
	unsigned long call0 = sc_calls_total(), lent0 = sc_lent_calls();
	hp_hll("Add", 0, 2, 2, (int[]){ arr, v });
	SC_CHECK(sc_calls_total() == call0, "SC39 depth %d: %lu function(s) ran during the store (%lu of them the watched "
		 "destructor)", depth, sc_calls_total() - call0, sc_lent_calls() - lent0);
	int e = hp_elem(arr, 0);
	SC_CHECK(e != src && sc_page(e) && sc_page(e)->values[wm].i == w && hp_refs(w, wseq) == 2,
		 "SC39 depth %d: element %d (the source %d), its wrap %d (the source's %d with %d reference(s))", depth, e, src,
		 sc_page(e) && e != src ? sc_page(e)->values[wm].i : -1, w, hp_refs(w, wseq));
	heap_unref(src);
	heap_unref(arr);
	while (hp_alive(src, sseq))
		heap_unref(src);
	/* the shared inner object (and at depth 1 its own inner one) once */
	SC_CHECK(!hp_alive(w, wseq) && sc_lent_calls() - lent0 == 1, "SC39 depth %d: after both went the wrapped object is %s, "
		 "the watched destructor ran %lu time(s), want once", depth, hp_alive(w, wseq) ? "alive" : "gone",
		 sc_lent_calls() - lent0);
	ain->structures[watched].destructor = was;
	SC_CHECK(live_slots() == live0, "SC39 depth %d: %zu live slot(s) after, %zu before", depth, live_slots(), live0);
}

static void sc39_quiet_inner(void)
{
	int wm[2], watched[2], sno[2];
	for (int d = 0; d < 2; d++)
		sno[d] = sc39_pick_at(&wm[d], d, &watched[d]);
	if (sno[0] < 0)
		xc_env_fail("struct shape");
	xc_ready();
	for (int d = 0; d < 2; d++)
		if (sno[d] >= 0)
			sc39_one(sno[d], wm[d], watched[d], d);
	printf("SC39 a new element's own wrap inner objects go without destructors: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC40 kind 0: numbers, strings and an array<E> member, E with a non-first
 * wrap<T> (0x67ffb0's elements are new objects here, alloc_struct giving
 * them inner objects); kind 1: numbers, strings and a struct member C with
 * such a wrap (the element's C is made for the copy); kind 2: numbers,
 * strings and a struct member (Fill gives it the defaults through a default
 * object made here); kind 3: numbers, strings and an array of one-slot
 * wraps of a struct. The watched struct: T (kinds 0, 1), C (kind 2). -1 if
 * this AIN has none. */
static int sc40_pick(int kind, int *mem, int *watched)
{
	int best = -1, bm = -1;
	for (int i = 0; i < ain->nr_structures; i++) {
		struct ain_struct *s = &ain->structures[i];
		if (s->nr_members <= 0 || s->nr_members > 64 || !s->name || !sc_quiet_dtor(i))
			continue;
		bool ok = true;
		int m0 = -1;
		for (int m = 0; ok && m < s->nr_members; m++) {
			const struct ain_type *t = &s->members[m].type;
			enum sc_kind k = sc_kind_of(t);
			if (k == SC_NUM || k == SC_STR)
				continue;
			const struct ain_type *e = t->array_type;
			switch (kind) {
			case 0:
				ok = k == SC_ARRAY && t->rank <= 1 && e && e->data == AIN_STRUCT && e->struc >= 0
					&& sc_quiet_dtor(e->struc) && sc_inner_wrap(e->struc) >= 0;
				break;
			case 1:
				ok = k == SC_STRUCT && sc_quiet_dtor(t->struc) && sc_inner_wrap(t->struc) >= 0;
				break;
			case 2:
				ok = k == SC_STRUCT && sc_quiet_dtor(t->struc) && sc_flat(t->struc);
				break;
			default:
				ok = k == SC_ARRAY && t->rank <= 1 && e && sc_is_wrap_struct(e) && ss_type_slot_count(e) == 1;
				break;
			}
			if (ok && m0 < 0)
				m0 = m;
		}
		if (ok && m0 >= 0 && (best < 0 || s->nr_members < ain->structures[best].nr_members)) {
			best = i;
			bm = m0;
		}
	}
	*mem = bm;
	*watched = -1;
	if (best >= 0) {
		const struct ain_type *t = &ain->structures[best].members[bm].type;
		int c = kind == 0 ? t->array_type->struc : t->struc;
		*watched = kind == 2 ? c : kind == 3 ? -1 : ain->structures[c].members[sc_inner_wrap(c)].type.struc;
	}
	printf("  SC40 kind %d: %s %d member %d, watched struct %d\n", kind, best >= 0 ? "struct" : "no struct of this AIN", best, bm,
	       *watched);
	return best;
}

static void sc40_quiet_and_wraps(void)
{
	int mem[4], sno[4], watched[4];
	for (int k = 0; k < 4; k++)
		sno[k] = sc40_pick(k, &mem[k], &watched[k]);
	xc_ready();
	/* kinds 0 and 1: Add runs no function */
	for (int k = 0; k < 2; k++) {
		if (sno[k] < 0)
			continue;
		size_t live0 = live_slots();
		int was = sc_lend_dtor(watched[k]);
		int src = alloc_struct(sno[k]);
		const struct ain_type *t = &ain->structures[sno[k]].members[mem[k]].type;
		if (k == 0) {
			int o = alloc_struct(t->array_type->struc);
			sc_put_member(src, mem[k], sc_array_of(t->array_type->struc, 1, &o));
		} else {
			sc_put_member(src, mem[k], alloc_struct(t->struc));
		}
		uint32_t seq = heap[src].seq;
		int arr = hp_array(AIN_ARRAY_STRUCT, sno[k]);
		int v = sc_handed(src);
		unsigned long call0 = sc_calls_total();
		hp_hll("Add", 0, 2, 2, (int[]){ arr, v });
		SC_CHECK(sc_calls_total() == call0 && hp_elem(arr, 0) != src, "SC40 kind %d: %lu function(s) ran during the store, "
			 "element %d (the source %d)", k, sc_calls_total() - call0, hp_elem(arr, 0), src);
		heap_unref(src);
		heap_unref(arr);
		while (hp_alive(src, seq))
			heap_unref(src);
		ain->structures[watched[k]].destructor = was;
		SC_CHECK(live_slots() == live0, "SC40 kind %d: %zu live slot(s) after, %zu before", k, live_slots(), live0);
	}
	/* kind 2: Fill with a -1 member; the element's member object is kept and
	 * the default object made for the assignment goes without its destructor */
	if (sno[2] >= 0) {
		size_t live0 = live_slots();
		int c = watched[2];
		int was = sc_lend_dtor(c);
		int el = alloc_struct(sno[2]), mobj = alloc_struct(c);
		sc_fill_base(mobj, 1000);
		uint32_t mseq = heap[mobj].seq;
		sc_put_member(el, mem[2], mobj);
		int arr = sc_array_of(sno[2], 1, &el);
		int src = alloc_struct(sno[2]);
		uint32_t seq = heap[src].seq;
		unsigned long l0 = sc_lent_calls();
		oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(src) }, (int[2]){ 0 }, 2);
		int k = sc_first(c, SC_NUM);
		SC_CHECK(sc_lent_calls() == l0 && hp_alive(mobj, mseq) && sc_page(el) && sc_page(el)->values[mem[2]].i == mobj
			 && sc_page(mobj)->values[k].i == 0,
			 "SC40 kind 2: Fill#0 ran the member struct's destructor %lu time(s); the member %s", sc_lent_calls() - l0,
			 hp_alive(mobj, mseq) ? "kept" : "gone");
		heap_unref(src);
		heap_unref(arr);
		while (hp_alive(src, seq))
			heap_unref(src);
		ain->structures[c].destructor = was;
		SC_CHECK(live_slots() == live0, "SC40 kind 2: %zu live slot(s) after, %zu before", live_slots(), live0);
	}
	/* kind 3: an array of wraps is a new page sharing each wrapped object */
	if (sno[3] >= 0) {
		size_t live0 = live_slots();
		const struct ain_type *t = &ain->structures[sno[3]].members[mem[3]].type;
		int src = alloc_struct(sno[3]);
		int w[2];
		uint32_t wseq[2];
		union vm_value dim = { .i = 2 };
		int a = heap_alloc_page(alloc_array(1, &dim, AIN_ARRAY, -1, false));
		for (int i = 0; i < 2; i++) {
			w[i] = alloc_struct(sc_flat_struct());
			wseq[i] = heap[w[i]].seq;
			heap[a].page->values[i].i = w[i];
		}
		(void)t;
		sc_put_member(src, mem[3], a);
		uint32_t seq = heap[src].seq;
		int arr = hp_array(AIN_ARRAY_STRUCT, sno[3]);
		hp_hll("Add", 0, 2, 2, (int[]){ arr, sc_handed(src) });
		int e = hp_elem(arr, 0);
		int ea = sc_page(e) && e != src ? sc_page(e)->values[mem[3]].i : -1;
		struct page *pa = ea > 1 && heap_index_valid(ea) ? heap[ea].page : NULL;
		SC_CHECK(ea != a && pa && pa->nr_vars == 2 && pa->values[0].i == w[0] && pa->values[1].i == w[1]
			 && hp_refs(w[0], wseq[0]) == 2 && hp_refs(w[1], wseq[1]) == 2,
			 "SC40 kind 3: the copy's array is slot %d (the source's %d), the wrapped objects with %d and %d reference(s)",
			 ea, a, hp_refs(w[0], wseq[0]), hp_refs(w[1], wseq[1]));
		heap_unref(src);
		heap_unref(arr);
		while (hp_alive(src, seq))
			heap_unref(src);
		SC_CHECK(!hp_alive(w[0], wseq[0]) && !hp_alive(w[1], wseq[1]), "SC40 kind 3: a wrapped object outlived both arrays");
		SC_CHECK(live_slots() == live0, "SC40 kind 3: %zu live slot(s) after, %zu before", live_slots(), live0);
	}
	printf("SC40 more objects only this VM makes, and arrays of wraps: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC41-SC45: member shapes this AIN has none of (synthetic structs) --- */

/* The cases below need structs this AIN does not have: a member of type 87
 * or a ref enum, and structs on a cycle of array members. They run on a
 * copy of the AIN in memory with structs appended (every other table is the
 * AIN's own), made the current AIN for the case only, so that nothing known
 * about the AIN's own structs carries over; restored before the case ends. */
static struct ain *sc_synth_saved;

static int sc_synth_begin(int n)
{
	sc_synth_saved = ain;
	struct ain *copy = xmalloc(sizeof *copy);
	*copy = *ain;
	copy->structures = xcalloc(ain->nr_structures + n, sizeof *copy->structures);
	memcpy(copy->structures, ain->structures, ain->nr_structures * sizeof *copy->structures);
	copy->nr_structures = ain->nr_structures + n;
	ain = copy;
	return sc_synth_saved->nr_structures;
}

static void sc_synth_struct(int no, const char *name, int n, const struct ain_type *types)
{
	struct ain_struct *s = &ain->structures[no];
	memset(s, 0, sizeof *s);
	s->name = xstrdup(name);
	s->constructor = -1;
	s->destructor = -1;
	s->nr_members = n;
	s->members = xcalloc(n, sizeof *s->members);
	for (int i = 0; i < n; i++) {
		s->members[i].name = xstrdup("m");
		s->members[i].type = types[i];
	}
}

static void sc_synth_end(void)
{
	struct ain *copy = ain;
	for (int i = sc_synth_saved->nr_structures; i < copy->nr_structures; i++) {
		struct ain_struct *s = &copy->structures[i];
		for (int m = 0; m < s->nr_members; m++)
			free(s->members[m].name);
		free(s->members);
		free(s->name);
	}
	free(copy->structures);
	ain = sc_synth_saved;
	free(copy);
}

static struct ain_type sc_t_int = { .data = AIN_INT, .struc = -1 };
static struct ain_type sc_t_elem[2];

/* SC41: a struct of a number and a two-slot member of type 87 or ref enum
 * (its second slot a void member): natively shared like a wrap (0x656f7c),
 * but this VM's teardown does not release either type, so the copy cannot
 * keep their references balanced; such a struct keeps the older handling. */
static void sc41_unreleased_refs(void)
{
	int base = sc_synth_begin(2);
	struct ain_type m87[3] = { sc_t_int, { .data = AIN_UNKNOWN_TYPE_87, .struc = -1, .array_type = &sc_t_int },
				   { .data = AIN_VOID, .struc = -1 } };
	struct ain_type mre[3] = { sc_t_int, { .data = AIN_REF_ENUM, .struc = -1 }, { .data = AIN_VOID, .struc = -1 } };
	sc_synth_struct(base, "sc41-type87", 3, m87);
	sc_synth_struct(base + 1, "sc41-ref-enum", 3, mre);
	printf("  SC41 synthetic structs %d (int, type 87, void) and %d (int, ref enum, void)\n", base, base + 1);
	xc_ready();
	sc37_one(base, "SC41 a type 87 member");
	sc37_one(base + 1, "SC41 a ref enum member");
	sc_synth_end();
	printf("SC41 type 87 and ref enum members keep the older handling: %s\n", xc_failures ? "FAIL" : "ok");
}

/* Two structs on a cycle of array members: A {int, array<B>, array<B>} and
 * B {int, array<A>} (A's second array: B met a second time while the check
 * of A is still open). With bad, A {int, array<B>, array<D>, long int} with
 * D {int} instead: the long int is one the original cannot assign
 * (0x656fc8), and D is a struct checked after B's result was taken, before
 * A's ends. Returns A; B is A + 1, D A + 2. */
static int sc_synth_cycle(bool bad)
{
	int a = sc_synth_begin(bad ? 3 : 2), b = a + 1, d = a + 2;
	static struct ain_type d_elem;
	sc_t_elem[0] = (struct ain_type){ .data = AIN_STRUCT, .struc = b };
	sc_t_elem[1] = (struct ain_type){ .data = AIN_STRUCT, .struc = a };
	d_elem = (struct ain_type){ .data = AIN_STRUCT, .struc = d };
	struct ain_type arr_b = { .data = AIN_ARRAY, .struc = b, .rank = 1, .array_type = &sc_t_elem[0] };
	struct ain_type ma_good[3] = { sc_t_int, arr_b, arr_b };
	struct ain_type ma_bad[4] = { sc_t_int, arr_b, { .data = AIN_ARRAY, .struc = d, .rank = 1, .array_type = &d_elem },
				      { .data = AIN_LONG_INT, .struc = -1 } };
	struct ain_type mb[2] = { sc_t_int, { .data = AIN_ARRAY, .struc = a, .rank = 1, .array_type = &sc_t_elem[1] } };
	if (bad) {
		sc_synth_struct(a, "sc-cycle-a-long", 4, ma_bad);
		sc_synth_struct(d, "sc-cycle-d", 1, &sc_t_int);
		printf("  synthetic structs %d (int, array<%d>, array<%d>, long int), %d (int, array<%d>), %d (int)\n", a, b, d,
		       b, a, d);
	} else {
		sc_synth_struct(a, "sc-cycle-a", 3, ma_good);
		printf("  synthetic structs %d (int, array<%d>, array<%d>) and %d (int, array<%d>)\n", a, b, b, b, a);
	}
	sc_synth_struct(b, "sc-cycle-b", 2, mb);
	return a;
}

/* For the record: how many structs of this AIN lie on a cycle of struct
 * members and array<struct> members, and the longest chain of such members
 * (in structs) when there is none. */
static int sc_chain_len(int i, signed char *color, int *len, int *on_cycle)
{
	if (color[i] == 2)
		return len[i];
	if (color[i] == 1) {
		(*on_cycle)++;
		return 0;
	}
	color[i] = 1;
	int best = 0;
	struct ain_struct *s = &ain->structures[i];
	for (int m = 0; m < s->nr_members; m++) {
		const struct ain_type *t = &s->members[m].type;
		int c = -1;
		if (t->data == AIN_STRUCT)
			c = t->struc;
		else if (t->data == AIN_ARRAY && t->array_type && t->array_type->data == AIN_STRUCT)
			c = t->array_type->struc;
		if (c >= 0 && c < ain->nr_structures) {
			int l = sc_chain_len(c, color, len, on_cycle);
			if (l > best)
				best = l;
		}
	}
	color[i] = 2;
	return len[i] = best + 1;
}

static void sc_print_cycles(void)
{
	signed char *color = xcalloc(ain->nr_structures, 1);
	int *len = xcalloc(ain->nr_structures, sizeof *len), on_cycle = 0, longest = 0;
	for (int i = 0; i < ain->nr_structures; i++) {
		int l = sc_chain_len(i, color, len, &on_cycle);
		if (l > longest)
			longest = l;
	}
	printf("  this AIN: %d back edge(s) of struct and array<struct> members (cycles), longest chain %d struct(s)\n",
	       on_cycle, longest);
	free(color);
	free(len);
}

/* SC42, SC43: A {int, array<B>, array<D>, long int}, B {int, array<A>} and
 * D {int}. B reaches A's long int through its array, so neither is copied:
 * two Adds of each keep the older handling, whichever struct is stored
 * first (SC42: A, then B; SC43: B, then A). */
static void sc42_cycle_unsupported(bool a_first, const char *id)
{
	if (a_first)
		sc_print_cycles();
	int a = sc_synth_cycle(true);
	xc_ready();
	char w[64];
	for (int k = 0; k < 2; k++) {
		int sno = (k == 0) == a_first ? a : a + 1;
		snprintf(w, sizeof w, "%s struct %s (stored %s)", id, sno == a ? "A" : "B", k ? "second" : "first");
		sc37_one(sno, w);
	}
	sc_synth_end();
	printf("%s a cycle that reaches a member this VM does not copy keeps the older handling: %s\n", id,
	       xc_failures ? "FAIL" : "ok");
}

static void sc42_a_first(void) { sc42_cycle_unsupported(true, "SC42"); }
static void sc43_b_first(void) { sc42_cycle_unsupported(false, "SC43"); }

/* An object of struct sno (member 0 an int, member 1 an array of structs of
 * the same shape) holding number n and, levels > 0, one element that holds
 * n + 1 and so on; the caller owns one reference. */
static int sc_cyc_value(int sno, int n, int levels)
{
	int obj = alloc_struct(sno);
	sc_page(obj)->values[0].i = n;
	if (levels > 0) {
		int en = ain->structures[sno].members[1].type.array_type->struc;
		int inner = sc_cyc_value(en, n + 1, levels - 1);
		union vm_value dim = { .i = 1 };
		int arr = heap_alloc_page(alloc_array(1, &dim, AIN_ARRAY_STRUCT, en, false));
		heap[arr].page->values[0].i = inner;
		int old = sc_page(obj)->values[1].i;
		sc_page(obj)->values[1].i = arr;
		if (old > 0 && heap_index_valid(old) && HEAP_REF(old) > 0)
			heap_unref(old);
	}
	return obj;
}

/* e is a copy of src at every level: its own object of the same struct with
 * the same number, its own array page with as many elements, each its own
 * copy in turn. */
static void sc_cyc_check(const char *what, int level, int e, int src)
{
	struct page *pe = sc_page(e), *ps = sc_page(src);
	SC_CHECK(pe && ps && e != src && pe->type == STRUCT_PAGE && pe->index == ps->index && pe->values[0].i == ps->values[0].i,
		 "%s, level %d: element %d (struct %d, number %d), source %d (struct %d, number %d); want a new object of "
		 "the same struct with the same number", what, level, e, pe ? pe->index : -1, pe ? pe->values[0].i : -1, src,
		 ps ? ps->index : -1, ps ? ps->values[0].i : -1);
	if (!pe || !ps || e == src)
		return;
	int ae = pe->values[1].i, as = ps->values[1].i, ne = hp_numof(ae), ns = hp_numof(as);
	SC_CHECK(ae != as && ne == ns && ne >= 0, "%s, level %d: the array is slot %d with %d element(s), the source's %d "
		 "with %d; want its own page with as many", what, level, ae, ne, as, ns);
	if (ae == as || ne != ns)
		return;
	for (int i = 0; i < ns; i++)
		sc_cyc_check(what, level + 1, hp_elem(ae, i), hp_elem(as, i));
}

/* Add of a value of struct sno, three levels deep: the element is a copy at
 * every level, the source keeps only its owner's reference, nothing is left
 * after both go. */
static void sc_cyc_store(const char *what, int sno, int n)
{
	size_t live0 = live_slots();
	int arr = hp_array(AIN_ARRAY_STRUCT, sno);
	int s = sc_cyc_value(sno, n, 3);
	uint32_t seq = heap[s].seq;
	int h = sc_handed(s);
	hp_hll("Add", 0, 2, 2, (int[]){ arr, h });
	SC_CHECK(hp_numof(arr) == 1 && hp_refs(s, seq) == 1, "%s: %d element(s), the source %d (handed %d) has %d "
		 "reference(s); want 1 element and only the owner's reference", what, hp_numof(arr), s, h, hp_refs(s, seq));
	if (hp_numof(arr) == 1)
		sc_cyc_check(what, 0, hp_elem(arr, 0), s);
	heap_unref(s);
	heap_unref(arr);
	SC_CHECK(live_slots() == live0, "%s: %zu live slot(s) after, %zu before", what, live_slots(), live0);
}

/* SC44: A {int, array<B>, array<B>} and B {int, array<A>}, every member one
 * the copy assigns: a value of either is copied, each nested element a new
 * object (0x656f45 -> 0x67ffb0 -> 0x6801d0 -> 0x656d60 at every level). A
 * first, then B. */
static void sc44_cycle(void)
{
	int a = sc_synth_cycle(false);
	xc_ready();
	sc_cyc_store("SC44 struct A", a, 10);
	sc_cyc_store("SC44 struct B", a + 1, 20);
	sc_synth_end();
	printf("SC44 a cycle of array members is copied: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC45: C {int, array<C>}, an array of itself: a value is copied at every
 * level. */
static void sc45_self(void)
{
	int c = sc_synth_begin(1);
	sc_t_elem[0] = (struct ain_type){ .data = AIN_STRUCT, .struc = c };
	struct ain_type mc[2] = { sc_t_int, { .data = AIN_ARRAY, .struc = c, .rank = 1, .array_type = &sc_t_elem[0] } };
	sc_synth_struct(c, "sc-self", 2, mc);
	printf("  synthetic struct %d (int, array<%d>)\n", c, c);
	xc_ready();
	sc_cyc_store("SC45 an array of itself", c, 30);
	sc_synth_end();
	printf("SC45 a struct holding an array of itself is copied: %s\n", xc_failures ? "FAIL" : "ok");
}

/* --- SC46-SC56, SC59: the delegate entries that point at the value --- */

/* Natively a struct assignment (0x681820, vtable 0x811dbc +4) does more
 * than assign the members (read in the disassembly):
 *   - first (0x68185f -> 0x681f70): every delegate entry for the target is
 *     dropped, as when an object is freed (0x6813c0 calls the same function);
 *     after the early returns for a value that is no live struct or one of
 *     another member count (0x681831..0x681859);
 *   - last (0x6818c8: the value's vtable +8, 0x6818f0), when every member
 *     was assigned, three steps:
 *     (a) each delegate with an entry for the value (the value's list at
 *         +0x2c, taken as a set: once each, by slot) gets, for each entry
 *         for the value, an entry for the target with the same function
 *         and environment (0x6528f0 -> 0x652210: an equal entry is taken
 *         out first, the new one goes to the end, the environment is
 *         retained, the limit of a call in progress is not raised);
 *     (b) each delegate the target reaches (0x682080: its delegate
 *         members, those of its struct members, the elements of its arrays
 *         of delegates; not the structs of an array of structs, nor wraps)
 *         gets the same (0x682330 -> 0x6528f0) and then loses its entries
 *         for the value (0x652970);
 *     (c) each delegate the value reaches loses its entries for the target
 *         (0x6822d0 -> 0x652970).
 * A struct member and each struct element of an array member are assigned
 * the same way (0x656eb6; 0x67ffb0 -> 0x6801d0 -> 0x656d60), their last
 * step before the outer one. The entries below are worked out from these
 * steps unless said otherwise.
 *
 * Entries are written "<object>.<function>", in the delegate's order. The
 * objects: s the value, e the element made from it, x, y and z objects of
 * their own, h a holder whose delegate member stands for an outside event.
 * The functions 1 to 4 are four functions of the game whose body is a bare
 * RETURN (1 an empty void(int), the one a call reaches, see below), 5 a
 * lambda (its entries carry an environment). The structs are synthetic (as SC41-SC45):
 *   OBJ {int}, HOLD {int, delegate}, S {int, string},
 *   T {int, string, delegate}, O {int, T, delegate}, O2 {int, delegate, T},
 *   A {int, string, array<T>}, G {int, string, array<delegate>},
 *   L {int, string, long int}.
 * A call: the game's parts::detail::CallErasingLayerEvent on its global
 * delegate, whose entries all name function 1 (an empty void(int)); the
 * step hook of delegate-cursor (dc) notes the object of each call. */

static struct {
	int obj, hold, s, t, o, o2, a, g, l;
	int w, ws, rs, is, o3;	/* SC61-SC64 */
	int wd;			/* SC68 */
	int fn[4];		/* fn[0] the handler a call reaches */
	int lambda;
	int global;		/* parts::detail::g_dgPartsErasingLayerEvent */
	int nnames;
	struct { int slot; char name[4]; } names[40];
} sb;

static struct ain_type sb_t_str = { .data = AIN_STRING, .struc = -1 };
static struct ain_type sb_t_long = { .data = AIN_LONG_INT, .struc = -1 };
static struct ain_type sb_t_dg, sb_t_elem_t, sb_t_elem_dg, sb_t_elem_w, sb_t_elem_wdg;

/* An empty function of the game (nargs int arguments, no other variable,
 * void, its body a bare RETURN; not a method, not a lambda): the skip-th. */
static int sb_empty_fn(int nargs, int skip)
{
	for (int f = 0; f < ain->nr_functions; f++) {
		struct ain_function *fn = &ain->functions[f];
		if (fn->nr_args != nargs || fn->nr_vars != nargs || fn->return_type.data != AIN_VOID || fn->is_lambda
		    || !fn->name || strchr(fn->name, '@') || fn->address >= ain->code_size
		    || (get_opcode(fn->address) & ~OPTYPE_MASK) != RETURN)
			continue;
		bool ints = true;
		for (int i = 0; i < nargs; i++)
			ints = ints && fn->vars[i].type.data == AIN_INT;
		if (ints && skip-- == 0)
			return f;
	}
	xc_env_fail("not enough empty functions");
	return -1;
}

static void sb_name(int slot, const char *name)
{
	if (sb.nnames < 40) {
		sb.names[sb.nnames].slot = slot;
		snprintf(sb.names[sb.nnames].name, sizeof sb.names[0].name, "%s", name);
		sb.nnames++;
	}
}

static const char *sb_obj(int slot)
{
	static char buf[8][12];
	static int k;
	for (int i = sb.nnames; i-- > 0;)
		if (sb.names[i].slot == slot)
			return sb.names[i].name;
	char *b = buf[k++ % 8];
	snprintf(b, sizeof buf[0], "#%d", slot);
	return b;
}

static int sb_fnid(int fn)
{
	for (int k = 0; k < 4; k++)
		if (fn == sb.fn[k])
			return k + 1;
	return fn == sb.lambda ? 5 : 0;
}

/* The entries of the delegate in slot dg ("" when it has none or no page). */
static const char *sb_entries(int dg)
{
	static char buf[4][256];
	static int k;
	char *b = buf[k++ % 4];
	int n = 0;
	b[0] = '\0';
	struct page *p = dg > 1 && heap_index_valid(dg) && heap[dg].type == VM_PAGE ? heap[dg].page : NULL;
	if (!p)
		return b;
	if (p->type != DELEGATE_PAGE) {
		snprintf(b, sizeof buf[0], "(not a delegate)");
		return b;
	}
	for (int i = 0; i + 2 < p->nr_vars && n < 220; i += 3)
		n += snprintf(b + n, sizeof buf[0] - n, "%s%s.%d", n ? " " : "", sb_obj(p->values[i].i), sb_fnid(p->values[i + 1].i));
	return b;
}

#define SB_IS(what, dg, want) do { const char *g_ = sb_entries(dg); \
	SC_CHECK(!strcmp(g_, want), "%s: [%s], want [%s]", what, g_, want); } while (0)

/* Variable var of the page in slot page (a struct member, a global). */
static int sb_var(int page, int var)
{
	struct page *p = page > 0 && heap_index_valid(page) && heap[page].type == VM_PAGE ? heap[page].page : NULL;
	return p && var < p->nr_vars ? p->values[var].i : -1;
}

/* page.var += obj.fn with the VM's own DG_PLUSA (0x652710), the source a
 * delegate as DG_NEW_FROM_METHOD makes one (environment 0 for a function
 * that is no lambda) or with the environment env. */
static void sb_plus(int page, int var, int obj, int fn, int env)
{
	int tmp = heap_alloc_page(delegate_new_from_method_env(obj, fn, env));
	int sp = stack_ptr;
	stack_push(page);
	stack_push(var);
	stack_push(tmp);
	execute_instruction(DG_PLUSA);
	stack_ptr = sp;
	heap_unref(tmp);
}

static int sb_new(int sno, const char *name, int number)
{
	int obj = alloc_struct(sno);
	sc_page(obj)->values[0].i = number;
	sb_name(obj, name);
	return obj;
}

/* A_REF(v) stored into arr by how ("Add", "PushBack", or "Insert" at 0);
 * returns the element stored. */
static int sb_store(int arr, int v, const char *how)
{
	int h = sc_handed(v);
	if (!strcmp(how, "Insert"))
		hp_hll("Insert", 0, 2, 3, (int[]){ arr, 0, h });
	else
		hp_hll(how, 0, 2, 2, (int[]){ arr, h });
	int n = hp_numof(arr);
	return !strcmp(how, "Insert") ? hp_elem(arr, 0) : n > 0 ? hp_elem(arr, n - 1) : -1;
}

/* e is a new object of struct sno holding s's number; named name. */
static bool sb_elem(const char *what, int e, int s, int sno, const char *name)
{
	struct page *pe = sc_page(e), *ps = sc_page(s);
	bool ok = e != s && pe && ps && pe->type == STRUCT_PAGE && pe->index == sno && pe->values[0].i == ps->values[0].i;
	SC_CHECK(ok, "%s: the element is %s (slot %d, the value %d)", what,
		 e == s ? "the value itself" : "not a new object of the struct with the value's number", e, s);
	if (ok)
		sb_name(e, name);
	return ok;
}

/* The global delegate called by the game's CallErasingLayerEvent; returns
 * the objects its handler ran for, in order. action as in delegate-cursor:
 * called when the handler starts, with the number of calls before it. */
static const char *sb_call(void (*action)(int))
{
	static char buf[4][96];
	static int k;
	char *b = buf[k++ % 4];
	dc.fn = sb.fn[0];
	dc.fn_addr = ain->functions[sb.fn[0]].address;
	dc.nseq = 0;
	dc.action = action;
	dc.active = true;
	ci_call("parts::detail::CallErasingLayerEvent", 1, (int[]){ 4711 });
	dc.active = false;
	dc.action = NULL;
	int n = 0;
	b[0] = '\0';
	for (int i = 0; i < dc.nseq && n < 80; i++)
		n += snprintf(b + n, sizeof buf[0] - n, "%s%s", i ? " " : "", sb_obj(dc.seq[i]));
	return b;
}

#define SB_CALLED(what, got, want) do { const char *g_ = (got); \
	SC_CHECK(!strcmp(g_, want), "%s: the call ran for [%s], want [%s]", what, g_, want); } while (0)

static void sb_clear_global(void)
{
	int g = sb_var(global_page_slot, sb.global);
	heap[global_page_slot].page->values[sb.global].i = -1;
	if (g > 1 && heap_index_valid(g) && HEAP_REF(g) > 0)
		heap_unref(g);
}

static void sb_release(int slot, uint32_t seq)
{
	while (hp_alive(slot, seq))
		heap_unref(slot);
}

static void sb_setup(void)
{
	memset(&sb, 0, sizeof sb);
	sb.global = ci_global("parts::detail::g_dgPartsErasingLayerEvent");
	struct ain_type *gt = &ain->globals[sb.global].type;
	struct ain_function_type *dg = gt->data == AIN_DELEGATE && gt->struc >= 0 && gt->struc < ain->nr_delegates
		? &ain->delegates[gt->struc] : NULL;
	if (!dg || dg->nr_arguments != 1 || dg->variables[0].type.data != AIN_INT || dg->return_type.data != AIN_VOID)
		xc_env_fail("the erasing-layer event is not a void(int) delegate");
	/* functions 2 to 4 are only named by entries, never called: any
	 * functions whose body is a bare RETURN */
	sb.fn[0] = sb_empty_fn(1, 0);
	for (int f = 0, k = 1; f < ain->nr_functions && k < 4; f++)
		if (f != sb.fn[0] && !ain->functions[f].is_lambda && ain->functions[f].address < ain->code_size
		    && (get_opcode(ain->functions[f].address) & ~OPTYPE_MASK) == RETURN)
			sb.fn[k++] = f;
	if (sb.fn[3] <= 0)
		xc_env_fail("not enough functions whose body is a bare RETURN");
	sb.lambda = -1;
	for (int f = 0; f < ain->nr_functions && sb.lambda < 0; f++)
		if (ain->functions[f].is_lambda)
			sb.lambda = f;
	if (sb.lambda < 0)
		xc_env_fail("no lambda");
	if (sb_var(global_page_slot, sb.global) > 0)
		xc_env_fail("the erasing-layer event is set");
	int b = sc_synth_begin(15);
	sb.obj = b; sb.hold = b + 1; sb.s = b + 2; sb.t = b + 3; sb.o = b + 4; sb.o2 = b + 5; sb.a = b + 6; sb.g = b + 7;
	sb.l = b + 8;
	sb_t_dg = (struct ain_type){ .data = AIN_DELEGATE, .struc = gt->struc };
	sb_t_elem_t = (struct ain_type){ .data = AIN_STRUCT, .struc = sb.t };
	sb_t_elem_dg = sb_t_dg;
	struct ain_type t_t = { .data = AIN_STRUCT, .struc = sb.t };
	struct ain_type arr_t = { .data = AIN_ARRAY, .struc = sb.t, .rank = 1, .array_type = &sb_t_elem_t };
	struct ain_type arr_dg = { .data = AIN_ARRAY, .struc = gt->struc, .rank = 1, .array_type = &sb_t_elem_dg };
	sc_synth_struct(sb.obj, "sb-obj", 1, &sc_t_int);
	sc_synth_struct(sb.hold, "sb-hold", 2, (struct ain_type[]){ sc_t_int, sb_t_dg });
	sc_synth_struct(sb.s, "sb-s", 2, (struct ain_type[]){ sc_t_int, sb_t_str });
	sc_synth_struct(sb.t, "sb-t", 3, (struct ain_type[]){ sc_t_int, sb_t_str, sb_t_dg });
	sc_synth_struct(sb.o, "sb-o", 3, (struct ain_type[]){ sc_t_int, t_t, sb_t_dg });
	sc_synth_struct(sb.o2, "sb-o2", 3, (struct ain_type[]){ sc_t_int, sb_t_dg, t_t });
	sc_synth_struct(sb.a, "sb-a", 3, (struct ain_type[]){ sc_t_int, sb_t_str, arr_t });
	sc_synth_struct(sb.g, "sb-g", 3, (struct ain_type[]){ sc_t_int, sb_t_str, arr_dg });
	sc_synth_struct(sb.l, "sb-l", 3, (struct ain_type[]){ sc_t_int, sb_t_str, sb_t_long });
	printf("  synthetic structs %d-%d; functions %d %d %d %d, lambda %d\n", b, b + 8, sb.fn[0], sb.fn[1], sb.fn[2],
	       sb.fn[3], sb.lambda);
	/* SC61-SC64: W {int, delegate, delegate}; WS, RS and IS {int, string,
	 * and a wrap<W>, a ref W or an interface (two slots, the second a
	 * void member)}; O3 {int, string, O} */
	sb.w = b + 9; sb.ws = b + 10; sb.rs = b + 11; sb.is = b + 12; sb.o3 = b + 13;
	sb_t_elem_w = (struct ain_type){ .data = AIN_STRUCT, .struc = sb.w };
	struct ain_type wrap_w = { .data = AIN_WRAP, .struc = sb.w, .array_type = &sb_t_elem_w };
	struct ain_type ref_w = { .data = AIN_REF_STRUCT, .struc = sb.w };
	struct ain_type iface = { .data = AIN_IFACE, .struc = -1 }, voidm = { .data = AIN_VOID, .struc = -1 };
	struct ain_type o_t = { .data = AIN_STRUCT, .struc = sb.o };
	sc_synth_struct(sb.w, "sb-w", 3, (struct ain_type[]){ sc_t_int, sb_t_dg, sb_t_dg });
	sc_synth_struct(sb.ws, "sb-ws", 3, (struct ain_type[]){ sc_t_int, sb_t_str, wrap_w });
	sc_synth_struct(sb.rs, "sb-rs", 3, (struct ain_type[]){ sc_t_int, sb_t_str, ref_w });
	sc_synth_struct(sb.is, "sb-is", 4, (struct ain_type[]){ sc_t_int, sb_t_str, iface, voidm });
	sc_synth_struct(sb.o3, "sb-o3", 3, (struct ain_type[]){ sc_t_int, sb_t_str, o_t });
	printf("  synthetic structs %d-%d (W, WS, RS, IS, O3)\n", b + 9, b + 13);
	/* SC68: WD {int, string, array<wrap<delegate>>} */
	sb.wd = b + 14;
	sb_t_elem_wdg = (struct ain_type){ .data = AIN_WRAP, .struc = gt->struc, .array_type = &sb_t_elem_dg };
	struct ain_type arr_wdg = { .data = AIN_ARRAY, .struc = gt->struc, .rank = 1, .array_type = &sb_t_elem_wdg };
	sc_synth_struct(sb.wd, "sb-wd", 3, (struct ain_type[]){ sc_t_int, sb_t_str, arr_wdg });
	printf("  synthetic struct %d (WD)\n", b + 14);
}

static void sb_end(void)
{
	sb_clear_global();
	sc_synth_end();
}

/* SC46: an outside delegate (an event the value was registered with) holds
 * x.1 s.1. After Add, PushBack or Insert: x.1 s.1 e.1, a call runs x, s, e
 * in that order; after the value goes (0x6813c0 -> 0x681f70): x.1 e.1, a
 * call runs x, then e (this is the element); after the element is erased
 * and freed: x.1, and nothing is left. */
static void sc46_outside(void)
{
	sb_setup();
	xc_ready();
	static const char *how[] = { "Add", "PushBack", "Insert" };
	for (int k = 0; k < 3; k++) {
		char w[96];
		sb.nnames = 0;
		size_t live0 = live_slots();
		int x = sb_new(sb.obj, "x", 1), s = sb_new(sb.s, "s", 46);
		uint32_t sseq = heap[s].seq;
		sb_plus(global_page_slot, sb.global, x, sb.fn[0], 0);
		sb_plus(global_page_slot, sb.global, s, sb.fn[0], 0);
		int arr = hp_array(AIN_ARRAY_STRUCT, sb.s);
		int e = sb_store(arr, s, how[k]);
		snprintf(w, sizeof w, "SC46 %s", how[k]);
		sb_elem(w, e, s, sb.s, "e");
		snprintf(w, sizeof w, "SC46 %s: the event", how[k]);
		SB_IS(w, sb_var(global_page_slot, sb.global), "x.1 s.1 e.1");
		SB_CALLED(w, sb_call(NULL), "x s e");
		heap_unref(s);
		snprintf(w, sizeof w, "SC46 %s: the event after the value went", how[k]);
		SB_IS(w, sb_var(global_page_slot, sb.global), "x.1 e.1");
		SB_CALLED(w, sb_call(NULL), "x e");
		hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
		snprintf(w, sizeof w, "SC46 %s: the event after the element was erased", how[k]);
		SB_IS(w, sb_var(global_page_slot, sb.global), "x.1");
		SB_CALLED(w, sb_call(NULL), "x");
		sb_clear_global();
		heap_unref(arr);
		sb_release(s, sseq);
		heap_unref(x);
		SC_CHECK(live_slots() == live0, "SC46 %s: %zu live slot(s) after, %zu before", how[k], live_slots(), live0);
	}
	sb_end();
	printf("SC46 an outside delegate bound to the value calls the element too: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC47: an entry with an environment (a lambda's, 0x6522c0): h holds s.5.
 * After Add: s.5 e.5, the new entry with the same environment, which has
 * one reference more (0x652210 retains it, 0x679f10); after the value goes
 * e.5 and one less; after the element is erased none, and the environment
 * is back to its own reference. */
static void sc47_environment(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int env = sb_new(sb.obj, "v", 1), hold = sb_new(sb.hold, "h", 2), s = sb_new(sb.s, "s", 47);
	uint32_t sseq = heap[s].seq;
	int r0 = HEAP_REF(env);
	sb_plus(hold, 1, s, sb.lambda, env);
	int d = sb_var(hold, 1);
	SC_CHECK(HEAP_REF(env) == r0 + 1, "SC47 precondition: the environment has %d reference(s) with one entry, want %d",
		 HEAP_REF(env), r0 + 1);
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.s);
	int e = sb_store(arr, s, "Add");
	sb_elem("SC47 Add", e, s, sb.s, "e");
	SB_IS("SC47 the delegate after Add", d, "s.5 e.5");
	struct page *p = heap_get_delegate_page(d);
	int got_env = -2;
	for (int i = 0; p && i + 2 < p->nr_vars; i += 3)
		if (p->values[i].i == e)
			got_env = p->values[i + 2].i;
	SC_CHECK(got_env == env, "SC47 the element's entry has environment %d, want the value's %d", got_env, env);
	SC_CHECK(HEAP_REF(env) == r0 + 2, "SC47 after Add the environment has %d reference(s), want %d (one per entry)",
		 HEAP_REF(env), r0 + 2);
	heap_unref(s);
	SB_IS("SC47 the delegate after the value went", d, "e.5");
	SC_CHECK(HEAP_REF(env) == r0 + 1, "SC47 after the value went the environment has %d reference(s), want %d",
		 HEAP_REF(env), r0 + 1);
	hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
	SB_IS("SC47 the delegate after the element was erased", d, "");
	SC_CHECK(HEAP_REF(env) == r0, "SC47 after the element was erased the environment has %d reference(s), want %d",
		 HEAP_REF(env), r0);
	heap_unref(arr);
	sb_release(s, sseq);
	heap_unref(hold);
	heap_unref(env);
	SC_CHECK(live_slots() == live0, "SC47: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC47 an entry's environment goes with the element's entry: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC48: the value's own delegate member D (T member 2) bound to itself.
 * D = s.1, h = s.2: after Add the element's D is its own delegate holding
 * e.1 (copied as s.1 by 0x652710, then (a) and (b)), the value's D s.1
 * again after (c), h s.2 e.2; after the value goes e.1 and e.2.
 * With other objects' entries, D = z.3 s.1 y.4: the element's D z.3 y.4 e.1
 * (the entries for others copied in their order, its own at the end), the
 * value's D unchanged; when z goes its entry leaves the element's D too. */
static void sc48_member(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int hold = sb_new(sb.hold, "h", 2), s = sb_new(sb.t, "s", 48);
	uint32_t sseq = heap[s].seq;
	sb_plus(s, 2, s, sb.fn[0], 0);
	sb_plus(hold, 1, s, sb.fn[1], 0);
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.t);
	int e = sb_store(arr, s, "Add");
	if (sb_elem("SC48 Add", e, s, sb.t, "e")) {
		SC_CHECK(sb_var(e, 2) != sb_var(s, 2), "SC48 the element's delegate member is the value's (slot %d)", sb_var(s, 2));
		SB_IS("SC48 the element's delegate member", sb_var(e, 2), "e.1");
	}
	SB_IS("SC48 the value's delegate member", sb_var(s, 2), "s.1");
	SB_IS("SC48 the outside delegate", sb_var(hold, 1), "s.2 e.2");
	heap_unref(s);
	if (e != s)
		SB_IS("SC48 the element's delegate member after the value went", sb_var(e, 2), "e.1");
	SB_IS("SC48 the outside delegate after the value went", sb_var(hold, 1), "e.2");
	hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
	SB_IS("SC48 the outside delegate after the element was erased", sb_var(hold, 1), "");
	heap_unref(arr);
	sb_release(s, sseq);

	int z = sb_new(sb.obj, "z", 3), y = sb_new(sb.obj, "y", 4);
	uint32_t zseq = heap[z].seq;
	s = sb_new(sb.t, "s", 148);
	sseq = heap[s].seq;
	sb_plus(s, 2, z, sb.fn[2], 0);
	sb_plus(s, 2, s, sb.fn[0], 0);
	sb_plus(s, 2, y, sb.fn[3], 0);
	arr = hp_array(AIN_ARRAY_STRUCT, sb.t);
	e = sb_store(arr, s, "Add");
	bool ok = sb_elem("SC48 Add (other entries)", e, s, sb.t, "e");
	if (ok)
		SB_IS("SC48 the element's delegate member (other entries)", sb_var(e, 2), "z.3 y.4 e.1");
	SB_IS("SC48 the value's delegate member (other entries)", sb_var(s, 2), "z.3 s.1 y.4");
	heap_unref(s);
	if (ok)
		SB_IS("SC48 the element's delegate member after the value went", sb_var(e, 2), "z.3 y.4 e.1");
	heap_unref(z);
	SC_CHECK(!hp_alive(z, zseq), "SC48 z outlived its last reference");
	if (ok)
		SB_IS("SC48 the element's delegate member after z went", sb_var(e, 2), "y.4 e.1");
	heap_unref(arr);
	sb_release(s, sseq);
	heap_unref(y);
	heap_unref(hold);
	SC_CHECK(live_slots() == live0, "SC48: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC48 a delegate member bound to its own object: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC49: a struct member (O member 1, a T object m) bound outside, h = m.1,
 * and to itself, m's D = m.2. After Add the element's member object n is
 * its own (made for the copy), h m.1 n.1 (0x656eb6: the member's own
 * assignment), n's D n.2, m's D m.2; after the value (and with it m) goes
 * h n.1; after the element is erased none. */
static void sc49_nested(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int hold = sb_new(sb.hold, "h", 2), s = sb_new(sb.o, "s", 49);
	uint32_t sseq = heap[s].seq;
	int m = sb_new(sb.t, "m", 491);
	sc_put_member(s, 1, m);
	sb_plus(m, 2, m, sb.fn[1], 0);
	sb_plus(hold, 1, m, sb.fn[0], 0);
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.o);
	int e = sb_store(arr, s, "Add");
	if (sb_elem("SC49 Add", e, s, sb.o, "e")) {
		int n = sb_var(e, 1);
		bool own = n != m && sc_page(n) && heap[n].page->type == STRUCT_PAGE && heap[n].page->index == sb.t;
		SC_CHECK(own, "SC49 the element's struct member is slot %d (the value's %d), want an object of its own", n, m);
		if (own) {
			sb_name(n, "n");
			SB_IS("SC49 the element's member's delegate", sb_var(n, 2), "n.2");
		}
	}
	SB_IS("SC49 the value's member's delegate", sb_var(m, 2), "m.2");
	SB_IS("SC49 the outside delegate", sb_var(hold, 1), "m.1 n.1");
	heap_unref(s);
	SB_IS("SC49 the outside delegate after the value went", sb_var(hold, 1), "n.1");
	hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
	SB_IS("SC49 the outside delegate after the element was erased", sb_var(hold, 1), "");
	heap_unref(arr);
	sb_release(s, sseq);
	heap_unref(hold);
	SC_CHECK(live_slots() == live0, "SC49: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC49 a struct member bound outside: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC50: an array member's struct elements (A member 2 = [i, k], T), k bound
 * outside, h = k.1. After Add the element's array holds its own [j, l] and
 * h k.1 l.1 (0x6801d0 -> 0x656d60 -> 0x681820 for each element); after the
 * value goes h l.1; after the element is erased none. */
static void sc50_array_member(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int hold = sb_new(sb.hold, "h", 2), s = sb_new(sb.a, "s", 50);
	uint32_t sseq = heap[s].seq;
	int objs[2] = { sb_new(sb.t, "i", 501), sb_new(sb.t, "k", 502) };
	sc_put_member(s, 2, sc_array_of(sb.t, 2, objs));
	sb_plus(hold, 1, objs[1], sb.fn[0], 0);
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.a);
	int e = sb_store(arr, s, "Add");
	if (sb_elem("SC50 Add", e, s, sb.a, "e")) {
		int ea = sb_var(e, 2), j = hp_elem(ea, 0), l = hp_elem(ea, 1);
		bool own = ea != sb_var(s, 2) && hp_numof(ea) == 2 && j != objs[0] && l != objs[1] && sc_page(j) && sc_page(l);
		SC_CHECK(own, "SC50 the element's array is slot %d with %d element(s) %d,%d (the value's %d,%d); want its own two",
			 ea, hp_numof(ea), j, l, objs[0], objs[1]);
		if (own) {
			sb_name(j, "j");
			sb_name(l, "l");
		}
	}
	SB_IS("SC50 the outside delegate", sb_var(hold, 1), "k.1 l.1");
	heap_unref(s);
	SB_IS("SC50 the outside delegate after the value went", sb_var(hold, 1), "l.1");
	hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
	SB_IS("SC50 the outside delegate after the element was erased", sb_var(hold, 1), "");
	heap_unref(arr);
	sb_release(s, sseq);
	heap_unref(hold);
	SC_CHECK(live_slots() == live0, "SC50: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC50 an array member's struct element bound outside: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC51: arrays in the value.
 * A struct with an array of delegates (G member 2 = [d], d = s.1; h = s.2):
 * natively (b) and (c) walk that array (0x682080, element type 63) and the
 * result would be SC48's; this VM keeps the older handling for such a
 * struct (it copies none whose array elements are delegates), so Add stores
 * the value itself and nothing changes: d s.1, h s.2 (a guard of that).
 * A struct of an array of structs (A member 2 = [i]) whose element's D is
 * bound to the outer value and to itself, s.1 i.2: the walk does not enter
 * an array of structs, so after Add the copy j has s.1 j.2 e.1 and i has
 * s.1 i.2 e.1 (two entries each: the element's own last step re-points
 * i.2, the outer (a) adds e.1, neither (b) nor (c) reaches them); after the
 * value goes j has j.2 e.1. */
static void sc51_arrays(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int hold = sb_new(sb.hold, "h", 2), s = sb_new(sb.g, "s", 51);
	uint32_t sseq = heap[s].seq;
	int d = heap_alloc_page(delegate_new_from_method_env(s, sb.fn[0], 0));
	union vm_value dim = { .i = 1 };
	struct page *ap = alloc_array(1, &dim, AIN_ARRAY, -1, false);
	ap->values[0].i = d;
	sc_put_member(s, 2, heap_alloc_page(ap));
	sb_plus(hold, 1, s, sb.fn[1], 0);
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.g);
	int e = sb_store(arr, s, "Add");
	SC_CHECK(e == s, "SC51 an array of delegates: the element is slot %d, want the value %d itself (this VM keeps the older "
		 "handling for such a struct)", e, s);
	SB_IS("SC51 an array of delegates: the value's delegate element", d, "s.1");
	SB_IS("SC51 an array of delegates: the outside delegate", sb_var(hold, 1), "s.2");
	heap_unref(arr);
	sb_release(s, sseq);
	SB_IS("SC51 an array of delegates: the outside delegate after both went", sb_var(hold, 1), "");

	sb.nnames = 0;
	sb_name(hold, "h");
	s = sb_new(sb.a, "s", 151);
	sseq = heap[s].seq;
	int i0 = sb_new(sb.t, "i", 511);
	sc_put_member(s, 2, sc_array_of(sb.t, 1, &i0));
	sb_plus(i0, 2, s, sb.fn[0], 0);
	sb_plus(i0, 2, i0, sb.fn[1], 0);
	arr = hp_array(AIN_ARRAY_STRUCT, sb.a);
	e = sb_store(arr, s, "Add");
	int j = -1;
	if (sb_elem("SC51 an array of structs", e, s, sb.a, "e")) {
		j = hp_elem(sb_var(e, 2), 0);
		if (j != i0 && sc_page(j))
			sb_name(j, "j");
		else
			SC_CHECK(false, "SC51 an array of structs: the element's array holds %d (the value's %d), want its own", j, i0);
	}
	if (j > 0 && j != i0)
		SB_IS("SC51 an array of structs: the copy's delegate", sb_var(j, 2), "s.1 j.2 e.1");
	SB_IS("SC51 an array of structs: the value's element's delegate", sb_var(i0, 2), "s.1 i.2 e.1");
	heap_unref(s);
	if (j > 0 && j != i0 && sc_page(j))
		SB_IS("SC51 an array of structs: the copy's delegate after the value went", sb_var(j, 2), "j.2 e.1");
	heap_unref(arr);
	sb_release(s, sseq);
	heap_unref(hold);
	SC_CHECK(live_slots() == live0, "SC51: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC51 arrays of delegates and of structs in the value: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC52: Fill (0x648120 -> 0x646d70, each element of the range in index
 * order) assigns in place, so the first step drops the element's own
 * entries.
 * Elements a, b; h = a.1 s.1 y.1, h2 = a.2: after Fill#0 with s, h is
 * s.1 y.1 a.1 b.1 (a's entry dropped, then one for a and one for b from
 * s's) and h2 empty; after the value goes h y.1 a.1 b.1.
 * None (-1) and a struct of another member count: no step at all
 * (0x681833, 0x681859 return before 0x68185f), h a.1 b.1 stays.
 * The value an element of the array (A_REF(arr[0]), which this VM shares):
 * a's entries stay as they are and b gets a's, h = a.1 y.1 -> a.1 y.1 b.1.
 * Natively the value is A_REF's copy, holding a's entries; a's are dropped
 * and given back at the end (y.1 a.1 b.1). This VM skips both steps when
 * the value is the element itself (port strategy: a's entries unchanged). */
static void sc52_fill(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int hold = sb_new(sb.hold, "h", 2), hold2 = sb_new(sb.hold, "H", 3), y = sb_new(sb.obj, "y", 4);
	for (int round = 0; round < 4; round++) {
		int objs[2] = { sb_new(sb.s, "a", 521), sb_new(sb.s, "b", 522) };
		int arr = sc_array_of(sb.s, 2, objs);
		int s = round == 2 ? sb_new(sb.obj, "q", 523) : sb_new(sb.s, "s", 52);
		uint32_t sseq = heap[s].seq;
		static const char *what[] = { "SC52 Fill#0", "SC52 Fill#0 with none", "SC52 Fill#0 with another struct",
					      "SC52 Fill#0 with element 0" };
		char w[96];
		if (round == 0) {
			sb_plus(hold, 1, objs[0], sb.fn[0], 0);
			sb_plus(hold, 1, s, sb.fn[0], 0);
			sb_plus(hold, 1, y, sb.fn[0], 0);
			sb_plus(hold2, 1, objs[0], sb.fn[1], 0);
		} else if (round == 3) {
			sb_plus(hold, 1, objs[0], sb.fn[0], 0);
			sb_plus(hold, 1, y, sb.fn[0], 0);
		} else {
			sb_plus(hold, 1, objs[0], sb.fn[0], 0);
			sb_plus(hold, 1, objs[1], sb.fn[0], 0);
		}
		int v = round == 1 ? -1 : sc_handed(round == 3 ? objs[0] : s);
		int out[2] = { -9, -9 };
		oa_hll("Fill", 0, 2, 2, (int[]){ arr, v }, out, 2);
		SC_CHECK(out[0] == 2 && hp_numof(arr) == 2 && hp_elem(arr, 0) == objs[0] && hp_elem(arr, 1) == objs[1],
			 "%s: returned %d, elements %d,%d; want 2 and the elements' own objects %d,%d (assigned in place)", what[round],
			 out[0], hp_elem(arr, 0), hp_elem(arr, 1), objs[0], objs[1]);
		snprintf(w, sizeof w, "%s: the outside delegate", what[round]);
		if (round == 0) {
			SB_IS(w, sb_var(hold, 1), "s.1 y.1 a.1 b.1");
			SB_IS("SC52 Fill#0: a's other outside delegate", sb_var(hold2, 1), "");
			heap_unref(s);
			SB_IS("SC52 Fill#0: the outside delegate after the value went", sb_var(hold, 1), "y.1 a.1 b.1");
		} else if (round == 3) {
			SB_IS(w, sb_var(hold, 1), "a.1 y.1 b.1");
			heap_unref(s);
		} else {
			SB_IS(w, sb_var(hold, 1), "a.1 b.1");
			heap_unref(s);
		}
		heap_unref(arr);
		sb_release(s, sseq);
		SB_IS("SC52 the outside delegate after the elements went", sb_var(hold, 1), round == 0 ? "y.1" : round == 3 ? "y.1" : "");
		/* the next round starts from empty delegates */
		sb_clear_global();
		int d = sb_var(hold, 1), d2 = sb_var(hold2, 1);
		sc_page(hold)->values[1].i = -1;
		sc_page(hold2)->values[1].i = -1;
		if (d > 1 && heap_index_valid(d) && HEAP_REF(d) > 0)
			heap_unref(d);
		if (d2 > 1 && heap_index_valid(d2) && HEAP_REF(d2) > 0)
			heap_unref(d2);
	}
	heap_unref(y);
	heap_unref(hold);
	heap_unref(hold2);
	SC_CHECK(live_slots() == live0, "SC52: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC52 Fill drops the element's entries and gives it the value's: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC53: the order of the new entries.
 * An outside delegate with two entries for the value and one between,
 * s.1 z.3 s.2 -> s.1 z.3 s.2 e.1 e.2 (0x6528f0 walks the entries there at
 * the start, in order, each new one at the end); s.2 s.1 -> s.2 s.1 e.2 e.1;
 * after the value goes z.3 e.1 e.2 and e.2 e.1.
 * The value's own delegate member, s.1 z.3 s.2: the element's is
 * z.3 e.1 e.2, the value's unchanged.
 * An entry for the element that is there already: Fill#1(0, 1) over a with
 * h = a.1 s.1 s.2 gives s.1 s.2 a.1 a.2: the first step (0x681f70) drops
 * a.1, the last adds a.1 and a.2 at the end (0x652210 takes an equal entry
 * out before it appends, which does not show here: none is left by then);
 * after the value goes a.1 a.2. */
static void sc53_order(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int hold = sb_new(sb.hold, "h", 2), hold2 = sb_new(sb.hold, "H", 3), z = sb_new(sb.obj, "z", 4);
	int s = sb_new(sb.s, "s", 53);
	uint32_t sseq = heap[s].seq;
	sb_plus(hold, 1, s, sb.fn[0], 0);
	sb_plus(hold, 1, z, sb.fn[2], 0);
	sb_plus(hold, 1, s, sb.fn[1], 0);
	sb_plus(hold2, 1, s, sb.fn[1], 0);
	sb_plus(hold2, 1, s, sb.fn[0], 0);
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.s);
	int e = sb_store(arr, s, "Add");
	sb_elem("SC53 Add", e, s, sb.s, "e");
	SB_IS("SC53 the outside delegate s.1 z.3 s.2", sb_var(hold, 1), "s.1 z.3 s.2 e.1 e.2");
	SB_IS("SC53 the outside delegate s.2 s.1", sb_var(hold2, 1), "s.2 s.1 e.2 e.1");
	heap_unref(s);
	SB_IS("SC53 the outside delegate s.1 z.3 s.2 after the value went", sb_var(hold, 1), "z.3 e.1 e.2");
	SB_IS("SC53 the outside delegate s.2 s.1 after the value went", sb_var(hold2, 1), "e.2 e.1");
	heap_unref(arr);
	sb_release(s, sseq);

	s = sb_new(sb.t, "s", 153);
	sseq = heap[s].seq;
	sb_plus(s, 2, s, sb.fn[0], 0);
	sb_plus(s, 2, z, sb.fn[2], 0);
	sb_plus(s, 2, s, sb.fn[1], 0);
	arr = hp_array(AIN_ARRAY_STRUCT, sb.t);
	e = sb_store(arr, s, "Add");
	if (sb_elem("SC53 Add (a delegate member)", e, s, sb.t, "e"))
		SB_IS("SC53 the element's delegate member", sb_var(e, 2), "z.3 e.1 e.2");
	SB_IS("SC53 the value's delegate member", sb_var(s, 2), "s.1 z.3 s.2");
	heap_unref(arr);
	sb_release(s, sseq);

	int objs[2] = { sb_new(sb.s, "a", 531), sb_new(sb.s, "b", 532) };
	arr = sc_array_of(sb.s, 2, objs);
	s = sb_new(sb.s, "s", 253);
	sseq = heap[s].seq;
	int d = sb_var(hold, 1);
	sc_page(hold)->values[1].i = -1;
	if (d > 1 && heap_index_valid(d) && HEAP_REF(d) > 0)
		heap_unref(d);
	sb_plus(hold, 1, objs[0], sb.fn[0], 0);
	sb_plus(hold, 1, s, sb.fn[0], 0);
	sb_plus(hold, 1, s, sb.fn[1], 0);
	int out[2] = { -9, -9 };
	oa_hll("Fill", 1, 2, 4, (int[]){ arr, 0, 1, sc_handed(s) }, out, 2);
	SC_CHECK(out[0] == 1 && hp_elem(arr, 0) == objs[0], "SC53 Fill#1(0, 1): returned %d, element 0 is %d (want 1 and %d)",
		 out[0], hp_elem(arr, 0), objs[0]);
	SB_IS("SC53 Fill#1(0, 1) over a with a.1 s.1 s.2", sb_var(hold, 1), "s.1 s.2 a.1 a.2");
	heap_unref(s);
	SB_IS("SC53 Fill#1(0, 1): after the value went", sb_var(hold, 1), "a.1 a.2");
	heap_unref(arr);
	sb_release(s, sseq);
	heap_unref(z);
	heap_unref(hold);
	heap_unref(hold2);
	SC_CHECK(live_slots() == live0, "SC53: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC53 the order of the entries made for the element: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC54: a store while the delegate is being called. The event holds
 * s.1 y.1; s's handler (the first call) stores s: the new entry e.1 goes
 * to the end and this call does not reach it (0x652210 leaves the limit),
 * so this call runs s, y; the event then holds s.1 y.1 e.1 and the next
 * call runs s, y, e. The same when s's handler stores y: s, y, then
 * s, y, and y's copy. This branch's expectation: natively the value is
 * A_REF's temporary copy, whose entry (added after the limit) is removed
 * when it goes before the handler returns; the removal lowers the limit
 * (0x652330), so natively that first call runs s only. */
static int sb_act_arr, sb_act_val, sb_acted;
static void sb_act_store(int n)
{
	if (n == 0)
		sb_acted = sb_store(sb_act_arr, sb_act_val, "Add");
}

static void sc54_during_call(void)
{
	sb_setup();
	xc_ready();
	for (int k = 0; k < 2; k++) {
		sb.nnames = 0;
		size_t live0 = live_slots();
		int s = sb_new(sb.s, "s", 54), y = sb_new(sb.s, "y", 154);
		uint32_t sseq = heap[s].seq, yseq = heap[y].seq;
		sb_plus(global_page_slot, sb.global, s, sb.fn[0], 0);
		sb_plus(global_page_slot, sb.global, y, sb.fn[0], 0);
		int arr = hp_array(AIN_ARRAY_STRUCT, sb.s);
		sb_act_arr = arr;
		sb_act_val = k == 0 ? s : y;
		sb_acted = -1;
		const char *first = sb_call(sb_act_store);
		const char *w = k == 0 ? "SC54 s's handler stores s" : "SC54 s's handler stores y";
		SB_CALLED(w, first, "s y");
		sb_elem(w, sb_acted, sb_act_val, sb.s, "e");
		SB_IS(w, sb_var(global_page_slot, sb.global), k == 0 ? "s.1 y.1 e.1" : "s.1 y.1 e.1");
		SB_CALLED(w, sb_call(NULL), "s y e");
		sb_clear_global();
		heap_unref(arr);
		sb_release(s, sseq);
		sb_release(y, yseq);
		SC_CHECK(live_slots() == live0, "%s: %zu live slot(s) after, %zu before", w, live_slots(), live0);
	}
	sb_end();
	printf("SC54 a store from a handler of the delegate: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC55: edges of the three steps, worked out (not seen run natively).
 * A delegate member of the outer value bound to its struct member: O
 * {int, m, D} with D = m.1. m's own last step adds n.1 to every delegate
 * with an entry for m, the value's D among them, and the outer (b) and (c)
 * touch only entries for the outer objects, so after Add both the element's
 * D and the value's are m.1 n.1; after the value (and m) goes the
 * element's is n.1. The same for O2 {int, D, m} (D copied before m's step).
 * Fill where the value's D holds an entry for the element a (T: s's D a.1,
 * a's D a.2): the first step drops every entry for a, the value's too, so
 * after Fill#0 the value's D and a's are both empty (natively (c) would
 * drop it as well).
 * A store while the value's own D is being called, D = s.1 z.3 (the call
 * driven as DG_CALLBEGIN and DG_CALL keep it, delegate_call_begin and
 * delegate_call_next): s.1 runs and stores s; (a) appends e.1 to D (the
 * limit stays), (c) takes it out again (the limit goes down by one,
 * 0x652330), so this call ends after s.1: z.3 is not called. D is s.1 z.3
 * again and the next call runs both; the element's D is z.3 e.1. */
static void sc55_edges(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	for (int k = 0; k < 2; k++) {
		int sno = k == 0 ? sb.o : sb.o2, mm = k == 0 ? 1 : 2, dm = k == 0 ? 2 : 1;
		const char *w = k == 0 ? "SC55 O {int, m, D}" : "SC55 O2 {int, D, m}";
		char w2[96];
		sb.nnames = 0;
		int s = sb_new(sno, "s", 55), m = sb_new(sb.t, "m", 551);
		uint32_t sseq = heap[s].seq;
		sc_put_member(s, mm, m);
		sb_plus(s, dm, m, sb.fn[0], 0);
		int arr = hp_array(AIN_ARRAY_STRUCT, sno);
		int e = sb_store(arr, s, "Add");
		if (sb_elem(w, e, s, sno, "e")) {
			int n = sb_var(e, mm);
			if (n != m && sc_page(n))
				sb_name(n, "n");
			snprintf(w2, sizeof w2, "%s: the element's D", w);
			SB_IS(w2, sb_var(e, dm), "m.1 n.1");
		}
		snprintf(w2, sizeof w2, "%s: the value's D", w);
		SB_IS(w2, sb_var(s, dm), "m.1 n.1");
		heap_unref(s);
		if (e != s && sc_page(e)) {
			snprintf(w2, sizeof w2, "%s: the element's D after the value went", w);
			SB_IS(w2, sb_var(e, dm), "n.1");
		}
		heap_unref(arr);
		sb_release(s, sseq);
	}

	sb.nnames = 0;
	int a = sb_new(sb.t, "a", 552);
	int arr = sc_array_of(sb.t, 1, &a);
	int s = sb_new(sb.t, "s", 155);
	uint32_t sseq = heap[s].seq;
	sb_plus(s, 2, a, sb.fn[0], 0);
	sb_plus(a, 2, a, sb.fn[1], 0);
	int out[2] = { -9, -9 };
	oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(s) }, out, 2);
	SC_CHECK(out[0] == 1 && hp_elem(arr, 0) == a, "SC55 Fill#0: returned %d, element %d (want 1 and %d)", out[0],
		 hp_elem(arr, 0), a);
	SB_IS("SC55 Fill#0: the value's D (it held a.1)", sb_var(s, 2), "");
	SB_IS("SC55 Fill#0: a's D", sb_var(a, 2), "");
	heap_unref(s);
	heap_unref(arr);
	sb_release(s, sseq);

	sb.nnames = 0;
	int z = sb_new(sb.obj, "z", 3);
	s = sb_new(sb.t, "s", 255);
	sseq = heap[s].seq;
	sb_plus(s, 2, s, sb.fn[0], 0);
	sb_plus(s, 2, z, sb.fn[2], 0);
	arr = hp_array(AIN_ARRAY_STRUCT, sb.t);
	int dgs = sb_var(s, 2), e = -1, o_, f_, en_, served = 0;
	char seq[96] = "";
	int n = 0;
	delegate_call_begin(heap_get_delegate_page(dgs));
	while (served < 8 && delegate_call_next(heap_get_delegate_page(dgs), &o_, &f_, &en_)) {
		n += snprintf(seq + n, sizeof seq - n, "%s%s.%d", n ? " " : "", sb_obj(o_), sb_fnid(f_));
		if (served++ == 0)
			e = sb_store(arr, s, "Add");
	}
	SC_CHECK(!strcmp(seq, "s.1"), "SC55 a store while the value's D is called: the call served [%s], want [s.1]", seq);
	bool ok = sb_elem("SC55 a store while the value's D is called", e, s, sb.t, "e");
	SB_IS("SC55 the value's D after the call", sb_var(s, 2), "s.1 z.3");
	if (ok)
		SB_IS("SC55 the element's D", sb_var(e, 2), "z.3 e.1");
	n = 0;
	seq[0] = '\0';
	served = 0;
	delegate_call_begin(heap_get_delegate_page(dgs));
	while (served++ < 8 && delegate_call_next(heap_get_delegate_page(dgs), &o_, &f_, &en_))
		n += snprintf(seq + n, sizeof seq - n, "%s%s.%d", n ? " " : "", sb_obj(o_), sb_fnid(f_));
	SC_CHECK(!strcmp(seq, "s.1 z.3"), "SC55 the next call of the value's D served [%s], want [s.1 z.3]", seq);
	heap_unref(arr);
	sb_release(s, sseq);
	heap_unref(z);
	SC_CHECK(live_slots() == live0, "SC55: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC55 entries across levels, Fill of an element the value is bound to, a store during the value's call: %s\n",
	       xc_failures ? "FAIL" : "ok");
}

/* SC56 (guard): shapes this change leaves alone. A struct this VM does not
 * copy (L, a long int member: older handling, the element is the value):
 * the event s.1 stays s.1 and a call runs s, before and after the owner
 * lets go (the array holds the value); after the element is erased the
 * event is empty. Version 13: PushBack stores the value itself (HP7) and
 * the outside delegate keeps s.1. */
static void sc56_guards(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int s = sb_new(sb.l, "s", 56);
	uint32_t sseq = heap[s].seq;
	sb_plus(global_page_slot, sb.global, s, sb.fn[0], 0);
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.l);
	int e = sb_store(arr, s, "Add");
	SC_CHECK(e == s, "SC56 a long int member: the element is %d, want the value %d itself (older handling)", e, s);
	SB_IS("SC56 a long int member: the event", sb_var(global_page_slot, sb.global), "s.1");
	SB_CALLED("SC56 a long int member", sb_call(NULL), "s");
	heap_unref(s);
	SB_CALLED("SC56 a long int member, the owner gone", sb_call(NULL), "s");
	hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
	SB_IS("SC56 a long int member: the event after the element was erased", sb_var(global_page_slot, sb.global), "");
	sb_clear_global();
	heap_unref(arr);
	sb_release(s, sseq);

	int hold = sb_new(sb.hold, "h", 2);
	s = sb_new(sb.s, "s", 156);
	sseq = heap[s].seq;
	sb_plus(hold, 1, s, sb.fn[0], 0);
	arr = hp_array(AIN_ARRAY_STRUCT, sb.s);
	heap_ref(s);	/* the reference a value hands over, as SC25 */
	int saved = ain->version;
	ain->version = 13;
	hp_hll("PushBack", 0, 2, 2, (int[]){ arr, s });
	ain->version = saved;
	SC_CHECK(hp_numof(arr) == 1 && hp_elem(arr, 0) == s, "SC56 v13 PushBack: %d element(s), first %d; want the value %d",
		 hp_numof(arr), hp_elem(arr, 0), s);
	SB_IS("SC56 v13 PushBack: the outside delegate", sb_var(hold, 1), "s.1");
	heap_unref(arr);
	sb_release(s, sseq);
	heap_unref(hold);
	SC_CHECK(live_slots() == live0, "SC56: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC56 a struct this VM does not copy and v13 keep their entries: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC57: the game's own shape, EndEventCallbackCollection@Add (CreateEndTask
 * stores through it): .STRUCTREF m_func; .LOCALREF func; A_REF; NEW
 * EndEventCallback; CALLHLL Array PushBack 2. The constructor binds its
 * Update into the parts end-update event (AFL_Parts_AddEndUpdateEvent(
 * this.Update)); the new object is stored by value and goes when the call
 * returns. Natively the element gets the binding (0x646e38 -> 0x681820 ->
 * 0x6818f0 (a)), so after Add the event's record holds e.1 (1: Update), the
 * game's CallEndUpdateEvent runs the element's Update, which calls the
 * delegate given to Add once (for x), empties the element's m_event and
 * takes its record out; another frame calls nothing; the next Add drops
 * the finished element (EraseAll of IsEnd). The parts globals as
 * controller-id sets them, the end-update list a typed empty array as the
 * game's start-up leaves it. */
static void sc57_end_event(void)
{
	ci_setup();
	probe_step_budget = 50000000UL;
	int coll_st = hp_struct("EndEventCallbackCollection"), cb_st = hp_struct("EndEventCallback");
	int m_func = hp_member(coll_st, "m_func"), m_event = hp_member(cb_st, "m_event");
	int add = hp_fn("EndEventCallbackCollection@Add", 1), update = hp_fn("EndEventCallback@Update", 2);
	int lg = ci_global("parts::detail::g_dgPartsEndUpdateEvent");
	struct ain_type *lt = &ain->globals[lg].type;
	if (lt->data != AIN_ARRAY || !lt->array_type || lt->array_type->data != AIN_STRUCT)
		xc_env_fail("the end-update event is not an array of structs");
	int ev_st = lt->array_type->struc, ev_m = hp_member(ev_st, "Event");
	int hv = sb_empty_fn(0, 0), plain = sc_pick_plain();
	struct page *lp = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRUCT, 0);
	lp->array.rank = 1;
	lp->array.struct_type = ev_st;
	int old = heap[global_page_slot].page->values[lg].i;
	int list = heap_alloc_page(lp);
	heap[global_page_slot].page->values[lg].i = list;
	if (old > 1 && heap_index_valid(old) && HEAP_REF(old) > 0)
		heap_unref(old);
	memset(&sb, 0, sizeof sb);
	sb.fn[0] = update;
	sb.fn[1] = sb.fn[2] = sb.fn[3] = -1;
	sb.lambda = -1;
	xc_ready();
	int coll = alloc_struct(coll_st), x = alloc_struct(plain);
	sb_name(x, "x");
	int func = heap_alloc_page(delegate_new_from_method_env(x, hv, 0));
	heap_ref(func);		/* the frame takes the argument's reference */
	call_method_args(add, coll, 1, &func);
	int arr = sb_var(coll, m_func), e = hp_elem(arr, 0);
	bool ok = hp_numof(arr) == 1 && sc_page(e) && heap[e].page->type == STRUCT_PAGE && heap[e].page->index == cb_st;
	SC_CHECK(ok, "SC57 after Add m_func holds %d element(s), first %d; want one EndEventCallback", hp_numof(arr), e);
	if (ok)
		sb_name(e, "e");
	SC_CHECK(hp_numof(list) == 1, "SC57 after Add the end-update list holds %d record(s), want 1", hp_numof(list));
	SB_IS("SC57 the end-update event after Add (1: EndEventCallback@Update)", sb_var(hp_elem(list, 0), ev_m), "e.1");
	dc.fn = hv;
	dc.fn_addr = ain->functions[hv].address;
	dc.nseq = 0;
	dc.action = NULL;
	dc.active = true;
	ci_call("parts::detail::CallEndUpdateEvent", 2, (int[]){ 16, 16 });
	dc.active = false;
	SC_CHECK(dc.nseq == 1 && dc.seq[0] == x, "SC57 a frame's end-update events ran the delegate given to Add %d time(s)%s%s; "
		 "want once, for x", dc.nseq, dc.nseq ? ", first for " : "", dc.nseq ? sb_obj(dc.seq[0]) : "");
	if (ok)
		SB_IS("SC57 the element's m_event after the frame", sb_var(e, m_event), "");
	SC_CHECK(hp_numof(list) == 0, "SC57 after the frame the end-update list holds %d record(s), want 0 (Update took its own "
		 "out)", hp_numof(list));
	dc.nseq = 0;
	dc.active = true;
	ci_call("parts::detail::CallEndUpdateEvent", 2, (int[]){ 16, 16 });
	dc.active = false;
	SC_CHECK(dc.nseq == 0, "SC57 the next frame ran the delegate %d more time(s), want 0", dc.nseq);
	heap_ref(func);
	call_method_args(add, coll, 1, &func);
	arr = sb_var(coll, m_func);
	SC_CHECK(hp_numof(arr) == 1 && hp_elem(arr, 0) != e, "SC57 the next Add left %d element(s), first %d; want 1, not the "
		 "finished %d (EraseAll of IsEnd)", hp_numof(arr), hp_elem(arr, 0), e);
	heap_unref(coll);
	heap_unref(func);
	heap_unref(x);
	printf("SC57 the game's EndEventCallbackCollection@Add and a frame's end-update events: %s\n",
	       xc_failures ? "FAIL" : "ok");
}

/* SC58: EmplaceBack after Erase emptied an array of value structs.
 * EmplaceBack (0x6476e0) reads the length (0x64773d) and calls Realloc
 * with one more (0x647758, vtable +0x50), whose new element is made with
 * flag 1 and so constructed (0x67f4d0); Erase did not write the element
 * type (0x67ec50). So the element is a new CASFont, its constructor run
 * once. */
static void sc58_emplace_after_erase(void)
{
	lr_setup();
	int st = bt_struct("CASFont"), ctor = ain->structures[st].constructor;
	if (ctor <= 0 || ctor >= 40000)
		xc_env_fail("CASFont has no constructor");
	xc_ready();
	size_t live0 = live_slots();
	int objs[1] = { alloc_struct(st) };
	int arr = sc_array_of(st, 1, objs);
	hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
	if (hp_numof(arr) != 0)
		xc_env_fail("Erase did not empty the array");
	unsigned long c0 = calls[ctor];
	int r = bt_emplace_back(arr, 2), e = hp_elem(arr, 0);
	bool obj = sc_page(e) && heap[e].page->type == STRUCT_PAGE && heap[e].page->index == st;
	SC_CHECK(hp_numof(arr) == 1 && obj && r == e, "SC58 EmplaceBack after Erase: %d element(s), first %d (%s), returned %d; "
		 "want one new CASFont, returned", hp_numof(arr), e, obj ? "a CASFont" : "not a CASFont", r);
	SC_CHECK(calls[ctor] - c0 == 1, "SC58 EmplaceBack after Erase ran the constructor %lu time(s), want 1", calls[ctor] - c0);
	if (r > 1 && r == e && obj)
		heap_unref(r);	/* the result's reference */
	heap_unref(arr);
	SC_CHECK(live_slots() == live0, "SC58: %zu live slot(s) after, %zu before", live_slots(), live0);
	printf("SC58 EmplaceBack after Erase emptied the array: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC59: AddRange and Concat into an array slot without a page make its
 * page from the source's; a struct value stored afterwards is copied (a
 * new object of the struct, the same number), whatever struct the page
 * (here one from the page cache, last used for an array of another struct)
 * last held. */
static void sc59_range_into_none(void)
{
	sb_setup();
	int add_range = sc_ordinal("AddRange", 2, -1), concat = sc_ordinal("Concat", 2, -1);
	xc_ready();
	for (int k = 0; k < 2; k++) {
		const char *fn = k ? "Concat" : "AddRange";
		char w[64];
		snprintf(w, sizeof w, "SC59 %s", fn);
		sb.nnames = 0;
		size_t live0 = live_slots();
		int p0 = sb_new(sb.s, "p", 590);
		int src = sc_array_of(sb.s, 1, &p0);
		int s = sb_new(sb.s, "s", 59);
		uint32_t sseq = heap[s].seq;
		struct page *stale = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRUCT, 1);
		uintptr_t stale_at = (uintptr_t)stale;
		stale->array.rank = 1;
		stale->array.struct_type = sb.obj;
		free_page(stale);
		int dst = heap_alloc_page(NULL);
		hp_hll(fn, k ? concat : add_range, 2, 2, (int[]){ dst, src });
		struct page *dp = heap[dst].page;
		printf("  %s into no page: %d element(s), struct type %d (%s)\n", w, hp_numof(dst), dp ? dp->array.struct_type : -9,
		       (uintptr_t)dp == stale_at ? "the cached page" : "another page");
		SC_CHECK(hp_numof(dst) == 1 && hp_elem(dst, 0) == p0, "%s: the destination holds %d element(s), first %d; want the "
			 "source's %d", w, hp_numof(dst), hp_elem(dst, 0), p0);
		int e = sb_store(dst, s, "Add");
		SC_CHECK(hp_numof(dst) == 2, "%s, then Add: %d element(s), want 2", w, hp_numof(dst));
		sb_elem(w, e, s, sb.s, "e");
		heap_unref(dst);
		heap_unref(src);
		sb_release(s, sseq);
		SC_CHECK(live_slots() == live0, "%s: %zu live slot(s) after, %zu before", w, live_slots(), live0);
	}
	/* guard: before v14 the page made here keeps what the cached page held */
	{
		size_t live0 = live_slots();
		int p0 = sb_new(sb.s, "p", 591);
		int src = sc_array_of(sb.s, 1, &p0);
		struct page *stale = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRUCT, 1);
		uintptr_t stale_at = (uintptr_t)stale;
		stale->array.rank = 1;
		stale->array.struct_type = sb.obj;
		free_page(stale);
		int dst = heap_alloc_page(NULL);
		int saved = ain->version;
		ain->version = 13;
		hp_hll("AddRange", add_range, 2, 2, (int[]){ dst, src });
		ain->version = saved;
		struct page *dp = heap[dst].page;
		bool cached = (uintptr_t)dp == stale_at;
		printf("  SC59 v13 AddRange into no page: struct type %d (%s)\n", dp ? dp->array.struct_type : -9,
		       cached ? "the cached page" : "another page, not checked");
		if (cached)
			SC_CHECK(dp->array.struct_type == sb.obj, "SC59 v13 AddRange into no page: struct type %d, want the cached "
				 "page's %d (unchanged before v14)", dp->array.struct_type, sb.obj);
		heap_unref(dst);
		heap_unref(src);
		SC_CHECK(live_slots() == live0, "SC59 v13: %zu live slot(s) after, %zu before", live_slots(), live0);
	}
	sb_end();
	printf("SC59 a store after AddRange or Concat into no page: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC60: struct members, edges worked out from the steps.
 * (1) The value's struct member m (O member 1, a T) has a delegate member
 *     bound to the value itself, m's D = s.1. After Add, at the member's
 *     own level nothing points at m; at the element's level (a) gives m's D
 *     and the copy n's D (which the member's copy put into the value's list)
 *     e.1, (b) walks the element into n (0x682080, struct member 13) and
 *     points n's D at the element, (c) walks the value into m and takes
 *     e.1 out again: n's D e.1, m's D s.1; after the value goes n's D e.1.
 * (2) Fill#0 into an element whose member object am (bound outside, h =
 *     am.1 sm.2) stays the element's: the member's own assignment drops the
 *     entries for am first (0x656eb6 -> 0x681820 -> 0x681f70) and then
 *     gives it the value's member's (sm.2): h sm.2 am.2, the member object
 *     the same; after the value goes am.2.
 * (3) Fill#0 of a value whose struct member is the default object (-1
 *     here) into an element whose member object bm is bound outside (h2 =
 *     bm.1): the default object is assigned to bm, whose entries go first:
 *     h2 empty, the member object the same. */
static void sc60_member_edges(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int s = sb_new(sb.o, "s", 60), m = sb_new(sb.t, "m", 601);
	uint32_t sseq = heap[s].seq;
	sc_put_member(s, 1, m);
	sb_plus(m, 2, s, sb.fn[0], 0);
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.o);
	int e = sb_store(arr, s, "Add");
	int n = -1;
	if (sb_elem("SC60 Add", e, s, sb.o, "e")) {
		n = sb_var(e, 1);
		bool own = n != m && sc_page(n) && heap[n].page->type == STRUCT_PAGE && heap[n].page->index == sb.t;
		SC_CHECK(own, "SC60 the element's struct member is slot %d (the value's %d), want an object of its own", n, m);
		if (own) {
			sb_name(n, "n");
			SB_IS("SC60 the element's member's delegate (bound to the value)", sb_var(n, 2), "e.1");
		} else {
			n = -1;
		}
	}
	SB_IS("SC60 the value's member's delegate", sb_var(m, 2), "s.1");
	heap_unref(s);
	if (n > 0)
		SB_IS("SC60 the element's member's delegate after the value went", sb_var(n, 2), "e.1");
	heap_unref(arr);
	sb_release(s, sseq);

	sb.nnames = 0;
	int hold = sb_new(sb.hold, "h", 2);
	int a = sb_new(sb.o, "a", 602), am = sb_new(sb.t, "am", 603);
	sc_put_member(a, 1, am);
	arr = sc_array_of(sb.o, 1, &a);
	s = sb_new(sb.o, "s", 160);
	sseq = heap[s].seq;
	int sm = sb_new(sb.t, "sm", 604);
	sc_put_member(s, 1, sm);
	sb_plus(hold, 1, am, sb.fn[0], 0);
	sb_plus(hold, 1, sm, sb.fn[1], 0);
	int out[2] = { -9, -9 };
	oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(s) }, out, 2);
	SC_CHECK(out[0] == 1 && hp_elem(arr, 0) == a && sb_var(a, 1) == am,
		 "SC60 Fill#0: returned %d, element %d, its member %d; want 1, %d and %d (assigned in place)", out[0],
		 hp_elem(arr, 0), sb_var(a, 1), a, am);
	SB_IS("SC60 Fill#0 into an existing member object: the outside delegate", sb_var(hold, 1), "sm.2 am.2");
	heap_unref(s);
	sb_release(s, sseq);
	SB_IS("SC60 Fill#0 into an existing member object: after the value went", sb_var(hold, 1), "am.2");
	heap_unref(arr);
	SB_IS("SC60 Fill#0 into an existing member object: after the element went", sb_var(hold, 1), "");
	heap_unref(hold);

	sb.nnames = 0;
	hold = sb_new(sb.hold, "h2", 2);
	a = sb_new(sb.o, "a", 605);
	int bm = sb_new(sb.t, "bm", 606);
	sc_put_member(a, 1, bm);
	arr = sc_array_of(sb.o, 1, &a);
	s = sb_new(sb.o, "s", 260);
	sseq = heap[s].seq;
	SC_CHECK(sb_var(s, 1) == -1, "SC60 the value's struct member is %d, want -1 (the default object)", sb_var(s, 1));
	sb_plus(hold, 1, bm, sb.fn[0], 0);
	oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(s) }, out, 2);
	SC_CHECK(out[0] == 1 && hp_elem(arr, 0) == a && sb_var(a, 1) == bm,
		 "SC60 Fill#0 of the default member: returned %d, element %d, its member %d; want 1, %d and %d", out[0],
		 hp_elem(arr, 0), sb_var(a, 1), a, bm);
	SB_IS("SC60 Fill#0 of the default member: the outside delegate", sb_var(hold, 1), "");
	heap_unref(s);
	sb_release(s, sseq);
	heap_unref(arr);
	heap_unref(hold);
	SC_CHECK(live_slots() == live0, "SC60: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC60 a member bound to the value, Fill into a member object, Fill of a default member: %s\n",
	       xc_failures ? "FAIL" : "ok");
}

/* The entries a call of the delegate in slot dg serves, driven as DG_CALLBEGIN
 * and DG_CALL drive it (delegate_call_begin, delegate_call_next). */
static const char *sb_served(int dg)
{
	static char buf[4][128];
	static int k;
	char *b = buf[k++ % 4];
	int n = 0, o_, f_, en_, served = 0;
	b[0] = '\0';
	struct page *p = heap_get_delegate_page(dg);
	if (!p)
		return b;
	delegate_call_begin(p);
	while (served++ < 12 && delegate_call_next(heap_get_delegate_page(dg), &o_, &f_, &en_))
		n += snprintf(b + n, sizeof buf[0] - n, "%s%s.%d", n ? " " : "", sb_obj(o_), sb_fnid(f_));
	return b;
}

#define SB_SERVES(what, dg, want) do { const char *g_ = sb_served(dg); \
	SC_CHECK(!strcmp(g_, want), "%s: a call served [%s], want [%s]", what, g_, want); } while (0)

/* page.var -= obj.fn with the VM's own DG_MINUSA. */
static void sb_minus(int page, int var, int obj, int fn)
{
	int tmp = heap_alloc_page(delegate_new_from_method_env(obj, fn, 0));
	int sp = stack_ptr;
	stack_push(page);
	stack_push(var);
	stack_push(tmp);
	execute_instruction(DG_MINUSA);
	stack_ptr = sp;
	heap_unref(tmp);
}

/* W object w as the shared member of obj (a wrap, a ref or an interface,
 * member 2 of WS, RS or IS; an interface's vtable offset 0), with a
 * reference of its own; what the member held (a wrap's own new object) is
 * released as sc_put_member does. */
static void sb_put_shared(int obj, int w)
{
	heap_ref(w);
	if (sc_page(obj)->index == sb.is)
		sc_page(obj)->values[3].i = 0;
	sc_put_member(obj, 2, w);
}

/* SC61: a member that shares an object (wrap<W>, ref W, an interface; W
 * {int, D, D2}) whose delegate is bound to the value: the walk of the last
 * step (0x682080) goes into struct members only, not into what a wrap, a
 * reference or an interface holds (0x6820b5..0x6820ca: types 13, 63 and 79;
 * any other member type returns at 0x682261), and the member is shared
 * (0x656f7c). W.D = s.1, W.D2 = w.2 (W bound to itself).
 * Add: the element shares W (one reference more); (a) gives W.D s.1 e.1,
 * neither (b) nor (c) reaches W, W.D2 stays w.2; a call serves s.1, e.1;
 * after the value goes W.D is e.1 and a call serves e; after the element
 * is erased W.D is empty and W has its own reference back.
 * Fill#0 of a value sharing the same W into an element a that holds W
 * too, W.D = a.1 s.1: the first step drops a.1 (only the entries for the
 * element: 0x681f70 does not walk), the members share W again, (a) adds
 * a.1: W.D s.1 a.1, W.D2 w.2, W's references unchanged; after the value
 * goes W.D a.1. */
static void sc61_shared_members(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	static const char *kind[] = { "a wrap<W> member", "a ref W member", "an interface member" };
	for (int k = 0; k < 3; k++) {
		int sno = k == 0 ? sb.ws : k == 1 ? sb.rs : sb.is;
		char w[128];
		sb.nnames = 0;
		int wo = sb_new(sb.w, "w", 610 + k);
		uint32_t wseq = heap[wo].seq;
		int s = sb_new(sno, "s", 61);
		uint32_t sseq = heap[s].seq;
		sb_put_shared(s, wo);
		sb_plus(wo, 1, s, sb.fn[0], 0);
		sb_plus(wo, 2, wo, sb.fn[1], 0);
		int r0 = HEAP_REF(wo);
		int arr = hp_array(AIN_ARRAY_STRUCT, sno);
		int e = sb_store(arr, s, "Add");
		snprintf(w, sizeof w, "SC61 %s: Add", kind[k]);
		if (sb_elem(w, e, s, sno, "e"))
			SC_CHECK(sb_var(e, 2) == wo && HEAP_REF(wo) == r0 + 1, "%s: the element's member is %d with %d reference(s); "
				 "want the value's %d shared, %d reference(s)", w, sb_var(e, 2), HEAP_REF(wo), wo, r0 + 1);
		snprintf(w, sizeof w, "SC61 %s: W's delegate bound to the value", kind[k]);
		SB_IS(w, sb_var(wo, 1), "s.1 e.1");
		SB_SERVES(w, sb_var(wo, 1), "s.1 e.1");
		snprintf(w, sizeof w, "SC61 %s: W's delegate bound to W", kind[k]);
		SB_IS(w, sb_var(wo, 2), "w.2");
		sb_release(s, sseq);
		snprintf(w, sizeof w, "SC61 %s: W's delegate after the value went", kind[k]);
		SB_IS(w, sb_var(wo, 1), "e.1");
		SB_SERVES(w, sb_var(wo, 1), "e.1");
		snprintf(w, sizeof w, "SC61 %s: W's delegate bound to W after the value went", kind[k]);
		SB_IS(w, sb_var(wo, 2), "w.2");
		hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
		snprintf(w, sizeof w, "SC61 %s: after the element was erased", kind[k]);
		SB_IS(w, sb_var(wo, 1), "");
		SC_CHECK(hp_refs(wo, wseq) == r0 - 1, "%s: W has %d reference(s), want its own %d", w, hp_refs(wo, wseq), r0 - 1);
		heap_unref(arr);

		/* Fill#0 into an element holding the same W */
		sb.nnames = 0;
		sb_name(wo, "w");
		int a = sb_new(sno, "a", 615);
		sb_put_shared(a, wo);
		arr = sc_array_of(sno, 1, &a);
		s = sb_new(sno, "s", 161);
		sseq = heap[s].seq;
		sb_put_shared(s, wo);
		int d = sb_var(wo, 1);
		sc_page(wo)->values[1].i = -1;
		if (d > 1 && heap_index_valid(d) && HEAP_REF(d) > 0)
			heap_unref(d);
		sb_plus(wo, 1, a, sb.fn[0], 0);
		sb_plus(wo, 1, s, sb.fn[0], 0);
		int r1 = HEAP_REF(wo);
		int out[2] = { -9, -9 };
		oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(s) }, out, 2);
		snprintf(w, sizeof w, "SC61 %s: Fill#0", kind[k]);
		SC_CHECK(out[0] == 1 && hp_elem(arr, 0) == a && sb_var(a, 2) == wo && HEAP_REF(wo) == r1,
			 "%s: returned %d, element %d, its member %d, W %d reference(s); want 1, %d, the shared %d and %d", w, out[0],
			 hp_elem(arr, 0), sb_var(a, 2), HEAP_REF(wo), a, wo, r1);
		snprintf(w, sizeof w, "SC61 %s: Fill#0: W's delegate", kind[k]);
		SB_IS(w, sb_var(wo, 1), "s.1 a.1");
		snprintf(w, sizeof w, "SC61 %s: Fill#0: W's delegate bound to W", kind[k]);
		SB_IS(w, sb_var(wo, 2), "w.2");
		sb_release(s, sseq);
		snprintf(w, sizeof w, "SC61 %s: Fill#0: W's delegate after the value went", kind[k]);
		SB_IS(w, sb_var(wo, 1), "a.1");
		heap_unref(arr);
		snprintf(w, sizeof w, "SC61 %s: Fill#0: after the element went", kind[k]);
		SB_IS(w, sb_var(wo, 1), "");
		SB_IS(w, sb_var(wo, 2), "w.2");
		sb_release(wo, wseq);
	}
	SC_CHECK(live_slots() == live0, "SC61: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC61 a wrap, ref or interface member's object bound to the value is shared, not walked: %s\n",
	       xc_failures ? "FAIL" : "ok");
}

/* SC62 (guard): a value stored by the older handling, which copies the
 * struct here (vm_copy) and takes no step for the entries.
 * Version 13, Fill#0 of a value bound outside (h = s.1) into [a]: the
 * element is replaced by a copy, which has no entry (h s.1); after the
 * value goes h is empty.
 * A struct this VM does not assign member by member (L, a long int member)
 * in v14: Fill#0 replaces the element a by a copy too; h = a.1 s.1 loses
 * a.1 with a and gets nothing for the copy (h s.1); after the value goes h
 * is empty. */
static void sc62_older_copies(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	for (int k = 0; k < 2; k++) {
		const char *w = k == 0 ? "SC62 v13 Fill#0" : "SC62 a long int member, Fill#0";
		int sno = k == 0 ? sb.s : sb.l;
		char w2[128];
		sb.nnames = 0;
		int hold = sb_new(sb.hold, "h", 2);
		int a = sb_new(sno, "a", 620 + k);
		uint32_t aseq = heap[a].seq;
		int arr = sc_array_of(sno, 1, &a);
		int s = sb_new(sno, "s", 62);
		uint32_t sseq = heap[s].seq;
		if (k == 1)
			sb_plus(hold, 1, a, sb.fn[0], 0);
		sb_plus(hold, 1, s, sb.fn[0], 0);
		int out[2] = { -9, -9 };
		if (k == 0) {
			int saved = ain->version;
			ain->version = 13;
			oa_hll("Fill", 0, 2, 2, (int[]){ arr, s }, out, 2);
			ain->version = saved;
		} else {
			oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(s) }, out, 2);
		}
		int e = hp_elem(arr, 0);
		bool copy = out[0] == 1 && e != s && e != a && !hp_alive(a, aseq) && sc_page(e)
			&& heap[e].page->type == STRUCT_PAGE && heap[e].page->index == sno;
		SC_CHECK(copy, "%s: returned %d, element %d (old %d, value %d); want 1 and a copy replacing the element", w, out[0],
			 e, a, s);
		if (copy)
			sb_name(e, "c");
		snprintf(w2, sizeof w2, "%s: the outside delegate", w);
		SB_IS(w2, sb_var(hold, 1), "s.1");
		sb_release(s, sseq);
		snprintf(w2, sizeof w2, "%s: the outside delegate after the value went", w);
		SB_IS(w2, sb_var(hold, 1), "");
		heap_unref(arr);
		heap_unref(hold);
	}
	SC_CHECK(live_slots() == live0, "SC62: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC62 the older handling's copies get no entries: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC63: Fill#0 into an element a (O) whose member object n (T) stays its
 * own (0x656eb6 assigns into it, 0x681820 with its first and last steps).
 * h = n.1 m.1, h2 = n.3 (an entry only n has), n's D = n.2 (n bound to
 * itself), the value's member m with m's D = m.2: the member's first step
 * drops every entry for n (h m.1, h2 empty, n's D empty), its D is
 * assigned m's (m.2), its last step adds n.1 to h and re-points n's D
 * ((a) m.2 n.2, (b) n.2) and (c) leaves m's D m.2. So h m.1 n.1, h2 empty,
 * n's D n.2, m's D m.2, the member object still n; after the value (and
 * m) goes h n.1 and n's D n.2.
 * The value's member the default object (-1 here; natively an object
 * holding no entry): the first step drops every entry for n and n's D
 * becomes the default's (empty): h, h2 and n's D empty. */
static void sc63_fill_member_object(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	for (int k = 0; k < 2; k++) {
		const char *w = k == 0 ? "SC63 Fill#0 into a member object" : "SC63 Fill#0 of the default member";
		char w2[128];
		sb.nnames = 0;
		int hold = sb_new(sb.hold, "h", 2), hold2 = sb_new(sb.hold, "H", 3);
		int a = sb_new(sb.o, "a", 630), n = sb_new(sb.t, "n", 631);
		sc_put_member(a, 1, n);
		int arr = sc_array_of(sb.o, 1, &a);
		int s = sb_new(sb.o, "s", 63 + 100 * k), m = -1;
		uint32_t sseq = heap[s].seq;
		if (k == 0) {
			m = sb_new(sb.t, "m", 632);
			sc_put_member(s, 1, m);
		}
		sb_plus(hold, 1, n, sb.fn[0], 0);
		if (k == 0)
			sb_plus(hold, 1, m, sb.fn[0], 0);
		sb_plus(hold2, 1, n, sb.fn[2], 0);
		sb_plus(n, 2, n, sb.fn[1], 0);
		if (k == 0)
			sb_plus(m, 2, m, sb.fn[1], 0);
		int out[2] = { -9, -9 };
		oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(s) }, out, 2);
		SC_CHECK(out[0] == 1 && hp_elem(arr, 0) == a && sb_var(a, 1) == n,
			 "%s: returned %d, element %d, its member %d; want 1, %d and %d (assigned in place)", w, out[0],
			 hp_elem(arr, 0), sb_var(a, 1), a, n);
		snprintf(w2, sizeof w2, "%s: the outside delegate", w);
		SB_IS(w2, sb_var(hold, 1), k == 0 ? "m.1 n.1" : "");
		snprintf(w2, sizeof w2, "%s: the delegate only n was in", w);
		SB_IS(w2, sb_var(hold2, 1), "");
		snprintf(w2, sizeof w2, "%s: n's own delegate", w);
		SB_IS(w2, sb_var(n, 2), k == 0 ? "n.2" : "");
		if (k == 0) {
			SB_IS("SC63 Fill#0 into a member object: m's own delegate", sb_var(m, 2), "m.2");
			SB_SERVES("SC63 Fill#0 into a member object: n's own delegate", sb_var(n, 2), "n.2");
		}
		sb_release(s, sseq);
		snprintf(w2, sizeof w2, "%s: the outside delegate after the value went", w);
		SB_IS(w2, sb_var(hold, 1), k == 0 ? "n.1" : "");
		snprintf(w2, sizeof w2, "%s: n's own delegate after the value went", w);
		SB_IS(w2, sb_var(n, 2), k == 0 ? "n.2" : "");
		heap_unref(arr);
		snprintf(w2, sizeof w2, "%s: after the element went", w);
		SB_IS(w2, sb_var(hold, 1), "");
		heap_unref(hold);
		heap_unref(hold2);
	}
	SC_CHECK(live_slots() == live0, "SC63: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC63 Fill into a member object drops its own entries and gives it the value's member's: %s\n",
	       xc_failures ? "FAIL" : "ok");
}

/* SC64: three levels, the walk going into struct members at each level
 * (0x6821b3 .. 0x682245). O3 {int, string, c: O}, O {int, m: T, D}: the
 * middle object c is bound in its member's delegate and in its own,
 * m's D = c.1 s.2 and c's D = c.4 (s the outer value).
 * The copy m' of m gets m's D as it is; c's last step ((a) every delegate
 * with an entry for c gets c', (b) the walk of c' into m' re-points m''s
 * D and c''s D, (c) the walk of c into m takes c' out) gives m''s D
 * s.2 c'.1, m's D c.1 s.2, c''s D c'.4, c's D c.4; the outer last step
 * then gives m''s D c'.1 e.2 ((a) e.2, (b) the walk of e into c' and m'
 * drops s.2) and m's D c.1 s.2 ((c) the walk of s into c and m). Calls
 * serve just those entries; after the value goes m''s D is c'.1 e.2 and
 * c''s D c'.4. */
static void sc64_three_levels(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int s = sb_new(sb.o3, "s", 64), c = sb_new(sb.o, "c", 641), m = sb_new(sb.t, "m", 642);
	uint32_t sseq = heap[s].seq;
	sc_put_member(s, 2, c);
	sc_put_member(c, 1, m);
	sb_plus(m, 2, c, sb.fn[0], 0);
	sb_plus(m, 2, s, sb.fn[1], 0);
	sb_plus(c, 2, c, sb.fn[3], 0);
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.o3);
	int e = sb_store(arr, s, "Add");
	int c2 = -1, m2 = -1;
	if (sb_elem("SC64 Add", e, s, sb.o3, "e")) {
		c2 = sb_var(e, 2);
		m2 = sc_page(c2) ? sb_var(c2, 1) : -1;
		bool own = c2 != c && m2 != m && sc_page(c2) && sc_page(m2) && heap[c2].page->index == sb.o
			&& heap[m2].page->index == sb.t;
		SC_CHECK(own, "SC64 the element's members are %d and %d (the value's %d and %d), want objects of their own", c2, m2,
			 c, m);
		if (own) {
			sb_name(c2, "c'");
			sb_name(m2, "m'");
		} else {
			c2 = m2 = -1;
		}
	}
	if (m2 > 0) {
		SB_IS("SC64 the copy's innermost delegate", sb_var(m2, 2), "c'.1 e.2");
		SB_SERVES("SC64 the copy's innermost delegate", sb_var(m2, 2), "c'.1 e.2");
		SB_IS("SC64 the copy's middle delegate", sb_var(c2, 2), "c'.4");
	}
	SB_IS("SC64 the value's innermost delegate", sb_var(m, 2), "c.1 s.2");
	SB_SERVES("SC64 the value's innermost delegate", sb_var(m, 2), "c.1 s.2");
	SB_IS("SC64 the value's middle delegate", sb_var(c, 2), "c.4");
	sb_release(s, sseq);
	if (m2 > 0) {
		SB_IS("SC64 the copy's innermost delegate after the value went", sb_var(m2, 2), "c'.1 e.2");
		SB_IS("SC64 the copy's middle delegate after the value went", sb_var(c2, 2), "c'.4");
	}
	heap_unref(arr);
	SC_CHECK(live_slots() == live0, "SC64: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC64 three levels, the middle bound in its member and in itself: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC65: Realloc#1 with a value: the new tail is assigned the value as Fill
 * assigns (each new element 0x646e38 -> 0x681820), so it gets the value's
 * entries. [a], h = s.1: Realloc#1(2, s) gives [a, t] with t a new
 * object, h s.1 t.1; after the value goes t.1; after the array goes
 * nothing. The struct is one of the AIN's own of numbers and strings
 * (Realloc constructs its new tail, which reads what the engine knows of
 * the AIN's structs). */
static void sc65_realloc_value(void)
{
	int flat = sc_flat_struct();
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int hold = sb_new(sb.hold, "h", 2);
	int a = alloc_struct(flat);
	sb_name(a, "a");
	int arr = sc_array_of(flat, 1, &a);
	int s = alloc_struct(flat);
	sb_name(s, "s");
	uint32_t sseq = heap[s].seq;
	sb_plus(hold, 1, s, sb.fn[0], 0);
	int out[2] = { -9, -9 };
	oa_hll("Realloc", 1, 2, 3, (int[]){ arr, 2, sc_handed(s) }, out, 2);
	int t = hp_elem(arr, 1);
	bool ok = hp_numof(arr) == 2 && hp_elem(arr, 0) == a && t != s && sc_page(t) && heap[t].page->index == flat;
	SC_CHECK(ok, "SC65 Realloc#1(2): %d element(s), %d,%d (value %d); want a and a new object", hp_numof(arr),
		 hp_elem(arr, 0), t, s);
	if (ok)
		sb_name(t, "t");
	SB_IS("SC65 Realloc#1(2): the outside delegate", sb_var(hold, 1), "s.1 t.1");
	sb_release(s, sseq);
	SB_IS("SC65 Realloc#1(2): the outside delegate after the value went", sb_var(hold, 1), "t.1");
	heap_unref(arr);
	SB_IS("SC65 Realloc#1(2): the outside delegate after the array went", sb_var(hold, 1), "");
	heap_unref(hold);
	SC_CHECK(live_slots() == live0, "SC65: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC65 Realloc with a value gives the new tail the value's entries: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC66: the value's list of delegates (here a table that may hold slots
 * that no longer point at the value: a delegate whose entry for it was
 * taken out, or a freed delegate's slot used again) changes nothing but
 * the delegates with entries for the value (natively the list holds slots,
 * each taken out by 0x67acc0, and one that is gone or holds no delegate is
 * passed over; 0x6528f0 only acts on entries for the value).
 * h1 had s.1 taken out (DG_MINUSA): after Add h1 is empty, h2 = s.1 is
 * s.1 e.1; a freed delegate's slot that holds another delegate (z.3) or a
 * T object (its number the value's slot) keeps what it holds. Whether the
 * slots were used again is printed; the checks hold either way. */
static void sc66_stale_list(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int h1 = sb_new(sb.hold, "h", 2), h2 = sb_new(sb.hold, "H", 3), z = sb_new(sb.obj, "z", 3);
	int s = sb_new(sb.s, "s", 66);
	uint32_t sseq = heap[s].seq;
	sb_plus(h1, 1, s, sb.fn[0], 0);
	sb_minus(h1, 1, s, sb.fn[0]);
	sb_plus(h2, 1, s, sb.fn[0], 0);
	int d1 = heap_alloc_page(delegate_new_from_method_env(s, sb.fn[0], 0));
	heap_unref(d1);
	int r1 = heap_alloc_page(delegate_new_from_method_env(z, sb.fn[2], 0));
	int d2 = heap_alloc_page(delegate_new_from_method_env(s, sb.fn[0], 0));
	heap_unref(d2);
	int r2 = sb_new(sb.t, "r", s);
	struct page *rp = sc_page(r2);
	int rv1 = rp->values[1].i, rv2 = rp->values[2].i;
	printf("  SC66 freed delegate slots %d and %d; then a delegate in %d, a T object in %d\n", d1, d2, r1, r2);
	SB_IS("SC66 the delegate whose entry was taken out (before)", sb_var(h1, 1), "");
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.s);
	int e = sb_store(arr, s, "Add");
	sb_elem("SC66 Add", e, s, sb.s, "e");
	SB_IS("SC66 the delegate whose entry was taken out", sb_var(h1, 1), "");
	SB_IS("SC66 the delegate bound to the value", sb_var(h2, 1), "s.1 e.1");
	SB_IS("SC66 the delegate in a freed delegate's slot", r1, "z.3");
	rp = sc_page(r2);
	SC_CHECK(rp && rp->type == STRUCT_PAGE && rp->index == sb.t && rp->nr_vars == 3 && rp->values[0].i == s
		 && rp->values[1].i == rv1 && rp->values[2].i == rv2,
		 "SC66 the T object in a freed delegate's slot changed (%s)", rp ? "its members" : "no page");
	sb_release(s, sseq);
	SB_IS("SC66 the delegate bound to the value after the value went", sb_var(h2, 1), "e.1");
	heap_unref(arr);
	heap_unref(r1);
	heap_unref(r2);
	heap_unref(z);
	heap_unref(h1);
	heap_unref(h2);
	SC_CHECK(live_slots() == live0, "SC66: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC66 slots in the value's list that no longer point at it change nothing: %s\n", xc_failures ? "FAIL" : "ok");
}

/* The environment of obj's entry in the delegate in slot dg (-2 for none). */
static int sb_env_of(int dg, int obj)
{
	struct page *p = heap_get_delegate_page(dg);
	for (int i = 0; p && i + 2 < p->nr_vars; i += 3)
		if (p->values[i].i == obj)
			return p->values[i + 2].i;
	return -2;
}

/* SC67: a delegate member's entries with environments (a lambda's,
 * 0x6522c0). T's D = x.5 s.5, x.5's environment v, s.5's u. Add: the
 * element's D is a copy (0x652710 adds its entries through 0x652210, which
 * retains each environment) and then (a) and (b) give x.5 e.5 (e.5 with u,
 * x.5 with v); the value's D is x.5 s.5 again after (c). v and u have one
 * reference more each (the copy's entries; the steps' retains and releases,
 * 0x652210 and 0x652330, cancel); after the value goes one less each (its
 * D's entries go); after the element is erased they are back.
 * Fill#0 of such a value into an element a whose D is a.5 with environment
 * w: the first step (0x681f70 -> 0x652330) drops a.5 and w is back to its
 * own references; a's D becomes x.5 a.5 (a.5 with u), v and u one more
 * each; after the value and the array go every environment is back. */
static void sc67_member_environments(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int v = sb_new(sb.obj, "v", 1), u = sb_new(sb.obj, "u", 2), w = sb_new(sb.obj, "w", 3), x = sb_new(sb.obj, "x", 4);
	int rv = HEAP_REF(v), ru = HEAP_REF(u), rw = HEAP_REF(w);
	int s = sb_new(sb.t, "s", 67);
	uint32_t sseq = heap[s].seq;
	sb_plus(s, 2, x, sb.lambda, v);
	sb_plus(s, 2, s, sb.lambda, u);
	SC_CHECK(HEAP_REF(v) == rv + 1 && HEAP_REF(u) == ru + 1,
		 "SC67 precondition: the environments have %d and %d reference(s), want %d and %d", HEAP_REF(v), HEAP_REF(u),
		 rv + 1, ru + 1);
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.t);
	int e = sb_store(arr, s, "Add");
	bool ok = sb_elem("SC67 Add", e, s, sb.t, "e");
	if (ok) {
		SC_CHECK(sb_var(e, 2) != sb_var(s, 2), "SC67 the element's delegate member is the value's (slot %d)", sb_var(s, 2));
		SB_IS("SC67 the element's delegate member", sb_var(e, 2), "x.5 e.5");
		SC_CHECK(sb_env_of(sb_var(e, 2), x) == v && sb_env_of(sb_var(e, 2), e) == u,
			 "SC67 the element's entries have environments %d and %d, want %d and %d", sb_env_of(sb_var(e, 2), x),
			 sb_env_of(sb_var(e, 2), e), v, u);
	}
	SB_IS("SC67 the value's delegate member", sb_var(s, 2), "x.5 s.5");
	SC_CHECK(HEAP_REF(v) == rv + 2 && HEAP_REF(u) == ru + 2,
		 "SC67 after Add the environments have %d and %d reference(s), want %d and %d (one per entry)", HEAP_REF(v),
		 HEAP_REF(u), rv + 2, ru + 2);
	heap_unref(s);
	if (ok)
		SB_IS("SC67 the element's delegate member after the value went", sb_var(e, 2), "x.5 e.5");
	SC_CHECK(HEAP_REF(v) == rv + 1 && HEAP_REF(u) == ru + 1,
		 "SC67 after the value went the environments have %d and %d reference(s), want %d and %d", HEAP_REF(v),
		 HEAP_REF(u), rv + 1, ru + 1);
	hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
	SC_CHECK(HEAP_REF(v) == rv && HEAP_REF(u) == ru,
		 "SC67 after the element was erased the environments have %d and %d reference(s), want %d and %d",
		 HEAP_REF(v), HEAP_REF(u), rv, ru);
	heap_unref(arr);
	sb_release(s, sseq);

	s = sb_new(sb.t, "s", 167);
	sseq = heap[s].seq;
	sb_plus(s, 2, x, sb.lambda, v);
	sb_plus(s, 2, s, sb.lambda, u);
	int a = sb_new(sb.t, "a", 267);
	sb_plus(a, 2, a, sb.lambda, w);
	SC_CHECK(HEAP_REF(w) == rw + 1, "SC67 Fill precondition: w has %d reference(s), want %d", HEAP_REF(w), rw + 1);
	arr = sc_array_of(sb.t, 1, &a);
	int out[2] = { -9, -9 };
	oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(s) }, out, 2);
	ok = out[0] == 1 && hp_numof(arr) == 1 && hp_elem(arr, 0) == a;
	SC_CHECK(ok, "SC67 Fill#0: returned %d, element %d; want 1 and a (%d, assigned in place)", out[0], hp_elem(arr, 0), a);
	if (ok) {
		SB_IS("SC67 Fill#0: a's delegate member", sb_var(a, 2), "x.5 a.5");
		SC_CHECK(sb_env_of(sb_var(a, 2), x) == v && sb_env_of(sb_var(a, 2), a) == u,
			 "SC67 Fill#0: a's entries have environments %d and %d, want %d and %d", sb_env_of(sb_var(a, 2), x),
			 sb_env_of(sb_var(a, 2), a), v, u);
	}
	SB_IS("SC67 Fill#0: the value's delegate member", sb_var(s, 2), "x.5 s.5");
	SC_CHECK(HEAP_REF(v) == rv + 2 && HEAP_REF(u) == ru + 2 && HEAP_REF(w) == rw,
		 "SC67 after Fill#0 the environments have %d, %d and %d reference(s), want %d, %d and %d", HEAP_REF(v),
		 HEAP_REF(u), HEAP_REF(w), rv + 2, ru + 2, rw);
	heap_unref(s);
	SC_CHECK(HEAP_REF(v) == rv + 1 && HEAP_REF(u) == ru + 1,
		 "SC67 Fill#0: after the value went the environments have %d and %d reference(s), want %d and %d",
		 HEAP_REF(v), HEAP_REF(u), rv + 1, ru + 1);
	heap_unref(arr);
	SC_CHECK(HEAP_REF(v) == rv && HEAP_REF(u) == ru && HEAP_REF(w) == rw,
		 "SC67 Fill#0: after the array went the environments have %d, %d and %d reference(s), want %d, %d and %d",
		 HEAP_REF(v), HEAP_REF(u), HEAP_REF(w), rv, ru, rw);
	sb_release(s, sseq);
	heap_unref(v);
	heap_unref(u);
	heap_unref(w);
	heap_unref(x);
	SC_CHECK(live_slots() == live0, "SC67: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC67 a delegate member's environments keep their references balanced: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC68: WD {int, string, array<wrap<delegate>>} whose array member holds a
 * delegate d bound to the value, d = s.1. The walk of the last step goes
 * through array levels only (0x653530: a wrap element type is turned into a
 * reference type code by 0x653660, wrap<delegate> into 0x43, not 63), so
 * neither (b) nor (c) reaches d; the array member is copied with its
 * elements shared (0x67ffb0 -> 0x656f7c). After Add the element's array
 * holds d too (one reference more) and (a) gives d s.1 e.1; after the value
 * goes d is e.1; after the element is erased d is empty and has its own
 * reference back. */
static void sc68_wrapped_delegates(void)
{
	sb_setup();
	xc_ready();
	size_t live0 = live_slots();
	int s = sb_new(sb.wd, "s", 68);
	uint32_t sseq = heap[s].seq;
	int d = heap_alloc_page(delegate_new_from_method_env(s, sb.fn[0], 0));
	uint32_t dseq = heap[d].seq;
	int ar = hp_array(AIN_ARRAY, -1);
	hp_hll("PushBack", 0, 0x10002, 2, (int[]){ ar, d });
	sc_put_member(s, 2, ar);
	int r0 = hp_refs(d, dseq);
	SC_CHECK(hp_numof(ar) == 1 && hp_elem(ar, 0) == d && r0 == 2,
		 "SC68 precondition: the value's array holds %d element(s) (%d), the delegate %d reference(s); want d and 2",
		 hp_numof(ar), hp_elem(ar, 0), r0);
	SB_IS("SC68 the wrapped delegate before Add", d, "s.1");
	int arr = hp_array(AIN_ARRAY_STRUCT, sb.wd);
	int e = sb_store(arr, s, "Add");
	bool ok = sb_elem("SC68 Add", e, s, sb.wd, "e");
	if (ok) {
		int ea = sb_var(e, 2);
		SC_CHECK(ea != ar && hp_numof(ea) == 1 && hp_elem(ea, 0) == d,
			 "SC68 the element's array is %d (the value's %d) holding %d element(s) (%d); want its own holding d",
			 ea, ar, hp_numof(ea), hp_elem(ea, 0));
	}
	SB_IS("SC68 the wrapped delegate after Add", d, "s.1 e.1");
	SC_CHECK(hp_refs(d, dseq) == r0 + 1, "SC68 after Add the delegate has %d reference(s), want %d", hp_refs(d, dseq), r0 + 1);
	heap_unref(s);
	SB_IS("SC68 the wrapped delegate after the value went", d, "e.1");
	hp_hll("Erase", 0, 2, 3, (int[]){ arr, 0, 1 });
	SB_IS("SC68 the wrapped delegate after the element was erased", d, "");
	heap_unref(arr);
	sb_release(s, sseq);
	SC_CHECK(hp_refs(d, dseq) == 1, "SC68 at the end the delegate has %d reference(s), want 1 (its own)", hp_refs(d, dseq));
	heap_unref(d);
	SC_CHECK(live_slots() == live0, "SC68: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC68 a wrapped delegate in an array member is shared, not walked: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC69: Fill while a script run by its first step changes the array. The
 * original takes the count once (0x648120: 0x64814f..0x648166) and hands
 * 0x646d70 the array object and the index in each round (0x648170..
 * 0x648187), so each round reads the array again. Elements a and b (S);
 * h = a.5 whose environment is an object of a struct with a destructor,
 * held by that entry alone (and a held by the case too); the destructor's
 * start runs a script (the step hook) when the first step for a drops a.5
 * during Fill#0 with s.
 * The script pushes c (the array's page moves): both elements are assigned,
 * the array is a, b and a copy of c (worked out from 0x648120).
 * The script makes the array one element long (Realloc#0(1)): a is
 * assigned and the round for b is not taken; the script lets the array go:
 * a is assigned and nothing else is touched (both this branch's: what the
 * original's element access does past the end was not read).
 * Each time the destructor ran once during Fill, h is empty and nothing is
 * left. */
static int sb_fill_arr, sb_fill_how, sb_fill_val;
static void sb_act_fill(int n)
{
	if (n != 0)
		return;
	if (sb_fill_how == 0)
		sb_store(sb_fill_arr, sb_fill_val, "PushBack");
	else if (sb_fill_how == 1)
		hp_hll("Realloc", 0, 2, 2, (int[]){ sb_fill_arr, 1 });
	else
		heap_unref(sb_fill_arr);
}

static void sc69_fill_changed(void)
{
	int dno = sc_pick_dtor();
	sb_setup();
	xc_ready();
	int dfn = ain->structures[dno].destructor;
	static const char *what[] = { "SC69 a push by the script", "SC69 Realloc#0(1) by the script",
				      "SC69 the array let go by the script" };
	for (int k = 0; k < 3; k++) {
		sb.nnames = 0;
		size_t live0 = live_slots();
		char w[96];
		int hold = sb_new(sb.hold, "h", 2);
		int objs[2] = { sb_new(sb.s, "a", 691), sb_new(sb.s, "b", 692) };
		int arr = sc_array_of(sb.s, 2, objs);
		uint32_t arrseq = heap[arr].seq;
		heap_ref(objs[0]);
		int s = sb_new(sb.s, "s", 69), c = sb_new(sb.s, "c", 169);
		uint32_t sseq = heap[s].seq, cseq = heap[c].seq;
		int env = alloc_struct(dno);
		uint32_t envseq = heap[env].seq;
		sb_plus(hold, 1, objs[0], sb.lambda, env);
		heap_unref(env);
		SC_CHECK(hp_alive(env, envseq) && HEAP_REF(env) == 1,
			 "%s: precondition: the environment is held by the entry alone (%d reference(s))", what[k],
			 hp_alive(env, envseq) ? HEAP_REF(env) : 0);
		unsigned long d0 = calls[dfn];
		sb_fill_arr = arr;
		sb_fill_how = k;
		sb_fill_val = c;
		dc.fn = dfn;
		dc.fn_addr = ain->functions[dfn].address;
		dc.nseq = 0;
		dc.action = sb_act_fill;
		dc.active = true;
		int out[2] = { -9, -9 };
		oa_hll("Fill", 0, 2, 2, (int[]){ arr, sc_handed(s) }, out, 2);
		dc.active = false;
		dc.action = NULL;
		SC_CHECK(calls[dfn] - d0 == 1 && !hp_alive(env, envseq),
			 "%s: the environment's destructor ran %lu time(s) during Fill, want 1", what[k], calls[dfn] - d0);
		snprintf(w, sizeof w, "%s: the outside delegate", what[k]);
		SB_IS(w, sb_var(hold, 1), "");
		SC_CHECK(sc_page(objs[0]) && sc_page(objs[0])->values[0].i == 69,
			 "%s: a's number is %d, want the value's 69", what[k], sc_page(objs[0]) ? sc_page(objs[0])->values[0].i : -1);
		if (k == 0) {
			int x = hp_elem(arr, 2);
			bool ok = hp_numof(arr) == 3 && hp_elem(arr, 0) == objs[0] && hp_elem(arr, 1) == objs[1];
			SC_CHECK(ok, "%s: %d element(s) %d,%d; want 3, a and b first", what[k], hp_numof(arr), hp_elem(arr, 0),
				 hp_elem(arr, 1));
			if (ok)
				SC_CHECK(sc_page(objs[1])->values[0].i == 69, "%s: b's number is %d, want the value's 69", what[k],
					 sc_page(objs[1])->values[0].i);
			SC_CHECK(x != c && sc_page(x) && heap[x].page->index == sb.s && heap[x].page->values[0].i == 169,
				 "%s: the third element is %d (c %d); want a new object with c's number", what[k], x, c);
		} else if (k == 1) {
			SC_CHECK(hp_numof(arr) == 1 && hp_elem(arr, 0) == objs[0], "%s: %d element(s) (%d); want a alone", what[k],
				 hp_numof(arr), hp_elem(arr, 0));
		} else {
			SC_CHECK(!hp_alive(arr, arrseq), "%s: the array outlived its last reference", what[k]);
		}
		if (hp_alive(arr, arrseq))
			heap_unref(arr);
		heap_unref(objs[0]);
		sb_release(s, sseq);
		sb_release(c, cseq);
		heap_unref(hold);
		SC_CHECK(live_slots() == live0, "%s: %zu live slot(s) after, %zu before", what[k], live_slots(), live0);
	}
	sb_end();
	printf("SC69 Fill reads the array again in each round: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC70: Fill where a destructor run by the release of what an element gave
 * up changes the array. T objects; a's delegate member is x.5 whose
 * environment is an object of a struct with a destructor, held by that
 * entry alone (a held by the case too); the value s has an empty delegate
 * member. Fill#0 assigns s to a: a's delegate member becomes a copy of
 * s's (empty) and the delegate a gave up goes, which lets the environment
 * go; its destructor's start runs a script (the step hook) that pushes c
 * (the array's page moves). The array is then a and a copy of c, a holds
 * s's number, and the array's slot holds its present page after the call
 * (the page the call had read is gone). The round, the push and the count
 * are those of SC69's first part; when the destructor runs within the call
 * is this VM's (here after the last round, natively within it), which a
 * one-element array does not show. */
static int sb_fill2_arr, sb_fill2_how, sb_fill2_val, sb_fill2_len;
static void sb_act_fill2(int n)
{
	if (n != 0)
		return;
	if (sb_fill2_how == 0)
		sb_store(sb_fill2_arr, sb_fill2_val, "PushBack");
	else
		hp_hll("Realloc", 0, 2, 2, (int[]){ sb_fill2_arr, sb_fill2_len });
}

/* Runs Array.Fill with args (nargs slots) while the destructor dfn's start
 * runs sb_act_fill2; returns the destructor's runs during the call. */
static unsigned long sb_fill2_call(int dfn, int nargs, const int *args, int *out)
{
	unsigned long d0 = calls[dfn];
	dc.fn = dfn;
	dc.fn_addr = ain->functions[dfn].address;
	dc.nseq = 0;
	dc.action = sb_act_fill2;
	dc.active = true;
	oa_hll("Fill", nargs == 2 ? 0 : 1, 2, nargs, args, out, 2);
	dc.active = false;
	dc.action = NULL;
	return calls[dfn] - d0;
}

static void sc70_fill_release_changed(void)
{
	int dno = sc_pick_dtor();
	sb_setup();
	xc_ready();
	int dfn = ain->structures[dno].destructor;
	size_t live0 = live_slots();
	int x = sb_new(sb.obj, "x", 4);
	int a = sb_new(sb.t, "a", 701);
	int env = alloc_struct(dno);
	uint32_t envseq = heap[env].seq;
	sb_plus(a, 2, x, sb.lambda, env);
	heap_unref(env);
	SC_CHECK(hp_alive(env, envseq) && HEAP_REF(env) == 1,
		 "SC70 precondition: the environment is held by the entry alone (%d reference(s))",
		 hp_alive(env, envseq) ? HEAP_REF(env) : 0);
	SB_IS("SC70 precondition: a's delegate member", sb_var(a, 2), "x.5");
	int arr = sc_array_of(sb.t, 1, &a);
	uint32_t arrseq = heap[arr].seq;
	heap_ref(a);
	int s = sb_new(sb.t, "s", 70), c = sb_new(sb.t, "c", 170);
	uint32_t sseq = heap[s].seq, cseq = heap[c].seq;
	sb_fill2_arr = arr;
	sb_fill2_how = 0;
	sb_fill2_val = c;
	int out[2] = { -9, -9 };
	unsigned long ran = sb_fill2_call(dfn, 2, (int[]){ arr, sc_handed(s) }, out);
	SC_CHECK(out[0] == 1, "SC70 Fill#0 returned %d, want 1", out[0]);
	SC_CHECK(ran == 1 && !hp_alive(env, envseq),
		 "SC70 the environment's destructor ran %lu time(s) during Fill, want 1", ran);
	SC_CHECK(sc_page(a) && sc_page(a)->values[0].i == 70, "SC70 a's number is %d, want the value's 70",
		 sc_page(a) ? sc_page(a)->values[0].i : -1);
	SB_IS("SC70 a's delegate member", sb_var(a, 2), "");
	bool ok = hp_alive(arr, arrseq) && hp_numof(arr) == 2 && hp_elem(arr, 0) == a;
	SC_CHECK(ok, "SC70 the array holds %d element(s) (%d first); want 2, a first", hp_numof(arr), hp_elem(arr, 0));
	if (ok) {
		int y = hp_elem(arr, 1);
		SC_CHECK(y != c && sc_page(y) && heap[y].page->index == sb.t && heap[y].page->values[0].i == 170,
			 "SC70 the second element is %d (c %d); want a new object with c's number", y, c);
	}
	if (hp_alive(arr, arrseq))
		heap_unref(arr);
	heap_unref(a);
	sb_release(s, sseq);
	sb_release(c, cseq);
	heap_unref(x);
	SC_CHECK(live_slots() == live0, "SC70: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC70 Fill keeps the array's present page when a release moves it: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC71: Fill#1(1, 2) of a three-element array while a script run by the
 * first step for the element at 1 makes the array two elements long
 * (Realloc#0(2)). S objects a, b and c, each held by the case too; h = b.5
 * whose environment is an object of a struct with a destructor, held by
 * that entry alone. The count is taken once (0x648120), so a round for
 * index 2 is due; the array no longer has it: b is assigned, the round for
 * index 2 is not taken and c is not touched (c stays as it was, held by the
 * case alone). Fill returns the count taken at the start. That the round
 * past the end is left out is this branch's expectation (memory safety;
 * natively the element access past the end reports and then reads through
 * a null object, 0x67f950 and 0x646e4e). */
static void sc71_fill_range_shorter(void)
{
	int dno = sc_pick_dtor();
	sb_setup();
	xc_ready();
	int dfn = ain->structures[dno].destructor;
	size_t live0 = live_slots();
	int hold = sb_new(sb.hold, "h", 2);
	int objs[3] = { sb_new(sb.s, "a", 711), sb_new(sb.s, "b", 712), sb_new(sb.s, "c", 713) };
	int arr = sc_array_of(sb.s, 3, objs);
	uint32_t arrseq = heap[arr].seq;
	for (int i = 0; i < 3; i++)
		heap_ref(objs[i]);
	int s = sb_new(sb.s, "s", 71);
	uint32_t sseq = heap[s].seq;
	int env = alloc_struct(dno);
	uint32_t envseq = heap[env].seq;
	sb_plus(hold, 1, objs[1], sb.lambda, env);
	heap_unref(env);
	SC_CHECK(hp_alive(env, envseq) && HEAP_REF(env) == 1,
		 "SC71 precondition: the environment is held by the entry alone (%d reference(s))",
		 hp_alive(env, envseq) ? HEAP_REF(env) : 0);
	sb_fill2_arr = arr;
	sb_fill2_how = 1;
	sb_fill2_len = 2;
	int out[2] = { -9, -9 };
	unsigned long ran = sb_fill2_call(dfn, 4, (int[]){ arr, 1, 2, sc_handed(s) }, out);
	SC_CHECK(out[0] == 2, "SC71 Fill#1(1, 2) returned %d, want 2 (the count taken at the start)", out[0]);
	SC_CHECK(ran == 1 && !hp_alive(env, envseq),
		 "SC71 the environment's destructor ran %lu time(s) during Fill, want 1", ran);
	SB_IS("SC71 the outside delegate", sb_var(hold, 1), "");
	SC_CHECK(hp_alive(arr, arrseq) && hp_numof(arr) == 2 && hp_elem(arr, 0) == objs[0] && hp_elem(arr, 1) == objs[1],
		 "SC71 the array holds %d element(s) %d,%d; want 2, a and b", hp_numof(arr), hp_elem(arr, 0), hp_elem(arr, 1));
	static const int want[3] = { 711, 71, 713 };
	static const char *name[3] = { "a", "b", "c" };
	for (int i = 0; i < 3; i++)
		SC_CHECK(sc_page(objs[i]) && sc_page(objs[i])->values[0].i == want[i], "SC71 %s's number is %d, want %d",
			 name[i], sc_page(objs[i]) ? sc_page(objs[i])->values[0].i : -1, want[i]);
	SC_CHECK(HEAP_REF(objs[2]) == 1, "SC71 c has %d reference(s), want the case's 1", HEAP_REF(objs[2]));
	if (hp_alive(arr, arrseq))
		heap_unref(arr);
	for (int i = 0; i < 3; i++)
		heap_unref(objs[i]);
	sb_release(s, sseq);
	heap_unref(hold);
	SC_CHECK(live_slots() == live0, "SC71: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC71 Fill of a range stops where a shorter array ends: %s\n", xc_failures ? "FAIL" : "ok");
}

/* SC72: Fill#0 of an array whose second element has no object (-1, as an
 * older resize of this VM leaves one, see SC4) while a script run by the
 * first step for the first element pushes c (the array's page moves). S
 * objects; h = a.5 whose environment is an object of a struct with a
 * destructor, held by that entry alone (a held by the case too). The round
 * for index 1 reads the array again and makes the new object in the page
 * the array has then: the array is a, a new object with the value's
 * number, and a copy of c. The -1 element and the new object made for it
 * are this VM's (natively every element has its object); the rounds, the
 * push and the count are worked out from 0x648120 as in SC69. */
static void sc72_fill_made_after_move(void)
{
	int dno = sc_pick_dtor();
	sb_setup();
	xc_ready();
	int dfn = ain->structures[dno].destructor;
	size_t live0 = live_slots();
	int hold = sb_new(sb.hold, "h", 2);
	int a = sb_new(sb.s, "a", 721);
	int objs[2] = { a, -1 };
	int arr = sc_array_of(sb.s, 2, objs);
	uint32_t arrseq = heap[arr].seq;
	heap_ref(a);
	int s = sb_new(sb.s, "s", 72), c = sb_new(sb.s, "c", 172);
	uint32_t sseq = heap[s].seq, cseq = heap[c].seq;
	int env = alloc_struct(dno);
	uint32_t envseq = heap[env].seq;
	sb_plus(hold, 1, a, sb.lambda, env);
	heap_unref(env);
	SC_CHECK(hp_alive(env, envseq) && HEAP_REF(env) == 1,
		 "SC72 precondition: the environment is held by the entry alone (%d reference(s))",
		 hp_alive(env, envseq) ? HEAP_REF(env) : 0);
	sb_fill2_arr = arr;
	sb_fill2_how = 0;
	sb_fill2_val = c;
	int out[2] = { -9, -9 };
	unsigned long ran = sb_fill2_call(dfn, 2, (int[]){ arr, sc_handed(s) }, out);
	SC_CHECK(out[0] == 2, "SC72 Fill#0 returned %d, want 2", out[0]);
	SC_CHECK(ran == 1 && !hp_alive(env, envseq),
		 "SC72 the environment's destructor ran %lu time(s) during Fill, want 1", ran);
	SB_IS("SC72 the outside delegate", sb_var(hold, 1), "");
	SC_CHECK(sc_page(a) && sc_page(a)->values[0].i == 72, "SC72 a's number is %d, want the value's 72",
		 sc_page(a) ? sc_page(a)->values[0].i : -1);
	bool ok = hp_alive(arr, arrseq) && hp_numof(arr) == 3 && hp_elem(arr, 0) == a;
	SC_CHECK(ok, "SC72 the array holds %d element(s) (%d first); want 3, a first", hp_numof(arr), hp_elem(arr, 0));
	if (ok) {
		int y = hp_elem(arr, 1), z = hp_elem(arr, 2);
		SC_CHECK(y != a && y != s && y != c && sc_page(y) && heap[y].page->index == sb.s
			 && heap[y].page->values[0].i == 72 && HEAP_REF(y) == 1,
			 "SC72 the second element is %d; want a new object with the value's number, held by the array alone", y);
		SC_CHECK(z != c && sc_page(z) && heap[z].page->index == sb.s && heap[z].page->values[0].i == 172,
			 "SC72 the third element is %d (c %d); want a new object with c's number", z, c);
	}
	if (hp_alive(arr, arrseq))
		heap_unref(arr);
	heap_unref(a);
	sb_release(s, sseq);
	sb_release(c, cseq);
	heap_unref(hold);
	SC_CHECK(live_slots() == live0, "SC72: %zu live slot(s) after, %zu before", live_slots(), live0);
	sb_end();
	printf("SC72 Fill makes an element's new object in the array's present page: %s\n", xc_failures ? "FAIL" : "ok");
}

static int test_array_struct_copy(void)
{
	static const struct xc_case cases[] = {
		{ "SC1", sc1_add }, { "SC2", sc2_pushback }, { "SC3", sc3_insert }, { "SC4", sc4_fill_all },
		{ "SC5", sc5_fill_range }, { "SC6", sc6_realloc_fill }, { "SC7", sc7_copysub_shape }, { "SC8", sc8_destructors },
		{ "SC9", sc9_string }, { "SC10", sc10_struct }, { "SC11", sc11_array }, { "SC12", sc12_delegate },
		{ "SC13", sc13_wrap }, { "SC14", sc14_iface }, { "SC15", sc15_ref }, { "SC16", sc16_nested },
		{ "SC17", sc17_none }, { "SC18", sc18_after_erase }, { "SC19", sc19_lazy_members },
		{ "SC20", sc20_wrap_elements }, { "SC21", sc21_iface_elements }, { "SC22", sc22_numbers },
		{ "SC23", sc23_strings }, { "SC24", sc24_plain_struct }, { "SC25", sc25_legacy }, { "SC26", sc26_bytes },
		{ "SC27", sc27_positions }, { "SC28", sc28_none_untyped }, { "SC29", sc29_wrap_box }, { "SC30", sc30_empty },
		{ "SC31", sc31_temporary }, { "SC32", sc32_self }, { "SC33", sc33_other_struct }, { "SC34", sc34_copy_guard },
		{ "SC35", sc35_arrays }, { "SC36", sc36_many }, { "SC37", sc37_unsupported_shapes },
		{ "SC38", sc38_other_pages }, { "SC39", sc39_quiet_inner }, { "SC40", sc40_quiet_and_wraps },
		{ "SC41", sc41_unreleased_refs }, { "SC42", sc42_a_first }, { "SC43", sc43_b_first }, { "SC44", sc44_cycle },
		{ "SC45", sc45_self }, { "SC46", sc46_outside }, { "SC47", sc47_environment }, { "SC48", sc48_member },
		{ "SC49", sc49_nested }, { "SC50", sc50_array_member }, { "SC51", sc51_arrays }, { "SC52", sc52_fill },
		{ "SC53", sc53_order }, { "SC54", sc54_during_call }, { "SC55", sc55_edges }, { "SC56", sc56_guards },
		{ "SC57", sc57_end_event }, { "SC58", sc58_emplace_after_erase }, { "SC59", sc59_range_into_none },
		{ "SC60", sc60_member_edges }, { "SC61", sc61_shared_members }, { "SC62", sc62_older_copies },
		{ "SC63", sc63_fill_member_object }, { "SC64", sc64_three_levels }, { "SC65", sc65_realloc_value },
		{ "SC66", sc66_stale_list }, { "SC67", sc67_member_environments }, { "SC68", sc68_wrapped_delegates },
		{ "SC69", sc69_fill_changed },
		{ "SC70", sc70_fill_release_changed }, { "SC71", sc71_fill_range_shorter },
		{ "SC72", sc72_fill_made_after_move },
	};
	return xc_run("ARRAY-STRUCT-COPY", cases, sizeof cases / sizeof *cases, 120);
}
