/* Declaration-shape selection for statically linked HLL functions.
 *
 * link_static_library() binds AIN declarations to C functions by name only,
 * while the libffi CIF is built from the AIN declaration. When a game declares
 * a function with a different shape than the C prototype (int vs float,
 * wrap<ref> slot vs pointer, array return vs bool + out param), the call reads
 * the wrong registers. This hook lets each library pick the C implementation
 * that matches the declaration.
 *
 * Array has its own selector (array_select_function, called from ffi.c).
 *
 * Contract: every selector returns dflt for shapes it does not recognize.
 * Returning NULL would leave the declaration unlinked and route it to the
 * UNIMPL path, which silently pushes 0.
 */

#include <string.h>

#include "system4/ain.h"

void *fileoperation_select_function(const struct ain_hll_function *f, void *dflt);
void *vsfile_select_function(const struct ain_hll_function *f, void *dflt);
void *pe_layoutbox_select_function(const struct ain_hll_function *f, void *dflt);

void *hll_shape_select_function(const char *lib, const struct ain_hll_function *f, void *dflt)
{
	if (!lib || !f || !f->name || (f->nr_arguments > 0 && !f->arguments))
		return dflt;
	if (!strcmp(lib, "FileOperation"))
		return fileoperation_select_function(f, dflt);
	if (!strcmp(lib, "VSFile"))
		return vsfile_select_function(f, dflt);
	if (!strcmp(lib, "PartsEngine")
	    && (!strcmp(f->name, "SetLayoutBoxReturn") || !strcmp(f->name, "GetLayoutBoxReturnSize")))
		return pe_layoutbox_select_function(f, dflt);
	return dflt;
}
