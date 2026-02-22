/* v14 "Delegate" HLL library — delegate operations */

#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "hll.h"

// Delegate self is AIN_REF_DELEGATE -> struct page **
// The delegate page stores (obj, fun, seq) triples.

// [0] Set(self:ref_delegate, func:hll_func) -> void
// Replace delegate with a single function
static void Delegate_Set(struct page **self, int func)
{
	if (!self) return;
	struct page *dg = *self;
	if (dg) {
		dg = delegate_clear(dg);
	}
	// Create new delegate with obj=-1 (no object), func=func
	*self = delegate_append(dg, -1, func);
}

// [1] Add(self:ref_delegate, func:hll_func) -> void
static void Delegate_Add(struct page **self, int func)
{
	if (!self) return;
	*self = delegate_append(*self, -1, func);
}

// [2] Numof(self:ref_delegate) -> int
static int Delegate_Numof(struct page **self)
{
	if (!self || !*self) return 0;
	return delegate_numof(*self);
}

// [3] Empty(self:ref_delegate) -> bool
static bool Delegate_Empty(struct page **self)
{
	if (!self || !*self) return true;
	return delegate_numof(*self) == 0;
}

// [4] Equals(self:ref_delegate, src:wrap<delegate>) -> bool
// v14: wrap param arrives as int heap slot index
static bool Delegate_Equals(struct page **self, int src_slot)
{
	struct page *a = (self && *self) ? *self : NULL;
	struct page *src = wrap_get_page(src_slot);
	if (!a && !src) return true;
	if (!a || !src) return false;
	if (a->nr_vars != src->nr_vars) return false;
	for (int i = 0; i < a->nr_vars; i++) {
		if (a->values[i].i != src->values[i].i)
			return false;
	}
	return true;
}

// [5] IsExist(self:ref_delegate, func:hll_func) -> bool
static bool Delegate_IsExist(struct page **self, int func)
{
	if (!self || !*self) return false;
	return delegate_contains(*self, -1, func);
}

// [6] Erase(self:ref_delegate, func:hll_func) -> void
static void Delegate_Erase(struct page **self, int func)
{
	if (!self || !*self) return;
	delegate_erase(*self, -1, func);
}

// [7] Clear(self:ref_delegate) -> void
static void Delegate_Clear(struct page **self)
{
	if (!self || !*self) return;
	*self = delegate_clear(*self);
}

// [8] ToString(self:ref_delegate) -> string
static struct string *Delegate_ToString(struct page **self)
{
	return cstr_to_string("Delegate");
}

HLL_LIBRARY(Delegate,
	    HLL_EXPORT(Set, Delegate_Set),
	    HLL_EXPORT(Add, Delegate_Add),
	    HLL_EXPORT(Numof, Delegate_Numof),
	    HLL_EXPORT(Empty, Delegate_Empty),
	    HLL_EXPORT(Equals, Delegate_Equals),
	    HLL_EXPORT(IsExist, Delegate_IsExist),
	    HLL_EXPORT(Erase, Delegate_Erase),
	    HLL_EXPORT(Clear, Delegate_Clear),
	    HLL_EXPORT(ToString, Delegate_ToString)
	    );
