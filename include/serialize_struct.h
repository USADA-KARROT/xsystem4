/* Native "serialize_struct" save files (system.SerializeStruct family, AIN v14).
 *
 * Format and semantics follow the original engine (System4 v14, native v9
 * writer, v7/v8/v9 reader). None of these functions raise VM_ERROR/ERROR:
 * failures print a bounded warning and return false (or -1).
 */

#ifndef XSYSTEM4_SERIALIZE_STRUCT_H
#define XSYSTEM4_SERIALIZE_STRUCT_H

#include <stdbool.h>
#include <stddef.h>

struct string;

/* roots: heap slots already validated as struct pages; n >= 1.
 * label: file name as given by the game (diagnostics only). */
bool ss_serialize_file(const char *path, const char *label, const int *roots, int n);
/* In-place load into the given roots. */
bool ss_deserialize_file(const char *path, const char *label, const int *roots, int n);
bool ss_write_comment_file(const char *path, const char *label, const char *bytes, size_t len);
/* -1: failure (caller returns false); 0: valid file without a comment (caller
 * returns true and leaves the output unchanged); 1: *out is a new string with
 * one reference owned by the caller. */
int ss_read_comment_file(const char *path, const char *label, struct string **out);

/* Heap slot of the struct page an element of a GetStructPageList input or a
 * SerializeStruct list refers to, or -1. Unwraps an xsystem4 wrap box. */
int ss_struct_slot(int slot);

/* XSYS4_TRACE_SAVE=1 enables one line per call (at most 200 lines). */
void ss_trace(const char *fmt, ...);

#endif /* XSYSTEM4_SERIALIZE_STRUCT_H */
