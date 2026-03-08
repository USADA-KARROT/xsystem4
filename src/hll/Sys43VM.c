/* v14 "Sys43VM" HLL library — VM introspection */

#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "hll.h"

// [0] bool GetFunctionNameList(wrap<array<string>> functionNameList)
// v14: AIN_WRAP — functionNameList is heap slot index (1-slot wrap).
// Populates the wrapped array with all function names from the AIN.
static bool Sys43VM_GetFunctionNameList(int list_slot)
{
	int n = ain->nr_functions;
	struct page *array = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, n);
	array->array.rank = 1;
	for (int i = 0; i < n; i++) {
		const char *name = ain->functions[i].name;
		array->values[i].i = heap_alloc_string(cstr_to_string(name ? name : ""));
	}
	// v14: The parameter is ref array<string> — list_slot is the heap slot
	// of the array page itself (not a wrap struct). Replace the page directly.
	if (list_slot > 0 && (size_t)list_slot < heap_size) {
		struct page *old = heap[list_slot].page;
		heap[list_slot].page = array;
		if (old) free_page(old);
	}
	return true;
}

// [1] bool BeginProfiler()
static bool Sys43VM_BeginProfiler(void)
{
	return false;
}

// [2] bool EndProfiler()
static bool Sys43VM_EndProfiler(void)
{
	return false;
}

// [3] int GetActualNumofPage()
static int Sys43VM_GetActualNumofPage(void)
{
	return (int)heap_size;
}

// [4] int GetActualMemorySize()
static int Sys43VM_GetActualMemorySize(void)
{
	return (int)(heap_size * sizeof(struct vm_pointer));
}

HLL_LIBRARY(Sys43VM,
	    HLL_EXPORT(GetFunctionNameList, Sys43VM_GetFunctionNameList),
	    HLL_EXPORT(BeginProfiler, Sys43VM_BeginProfiler),
	    HLL_EXPORT(EndProfiler, Sys43VM_EndProfiler),
	    HLL_EXPORT(GetActualNumofPage, Sys43VM_GetActualNumofPage),
	    HLL_EXPORT(GetActualMemorySize, Sys43VM_GetActualMemorySize)
	    );
