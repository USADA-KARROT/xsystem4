/* v14 "Sys43VM" HLL library — VM introspection stubs */

#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "hll.h"

// [0] bool GetFunctionNameList(wrap<array<string>> functionNameList)
static bool Sys43VM_GetFunctionNameList(struct page *list)
{
	return false;
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
