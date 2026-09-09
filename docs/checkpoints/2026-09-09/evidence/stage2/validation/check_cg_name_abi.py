#!/usr/bin/env python3
"""Compile the production CG-name adapter/selector with a small parts fixture.

Uses real libffi calls and the existing sanitized libsys4 string implementation.
No game, graphics, engine entry point, or source-tree mutation is involved.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess

HERE = Path(__file__).resolve().parent
STAGE2 = HERE.parent
SOURCE = STAGE2 / "source"
STAGE1 = STAGE2.parent / "stage1"


def function_text(path, signature):
    text = path.read_text()
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
        end += 1
    return text[start:end]


hll_path = SOURCE / "src/hll/PartsEngine.c"
legacy_path = SOURCE / "src/parts/parts.c"
legacy = function_text(legacy_path, "void PE_GetPartsCGName(int parts_no,")
adapter = function_text(hll_path, "static struct string *PE_v14_GetPartsCGName(")
selector = function_text(hll_path, "static void parts_link_cg_name_getter(")
prelink = function_text(hll_path, "static void PartsEngine_PreLink(void)\n{")
for alias in ("GetPartsCGName", "Parts_GetPartsCGName"):
    assert f'parts_link_cg_name_getter(libno, "{alias}");' in prelink

prefix = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ffi.h>
#include "system4/ain.h"
#include "system4/string.h"

struct parts_cg { struct string *name; };
struct parts { struct parts_cg cg[3]; };
static struct parts fixture_parts;
static bool parts_state_valid(int state) { return state >= 0 && state < 3; }
static struct parts *parts_get(int number) { assert(number == 7); return &fixture_parts; }
static struct parts_cg *parts_get_cg(struct parts *p, int state) { return &p->cg[state]; }

static const char *aliases[] = {"GetPartsCGName", "Parts_GetPartsCGName"};
static struct ain_hll_function declarations[2];
static struct ain_hll_argument arguments[2][3];
static bool declared[2];
struct static_library { int unused; };
static struct static_library lib_PartsEngine;
static void *bindings[2];
static int alias_index(const char *name) {
    for (int i = 0; i < 2; i++) if (!strcmp(name, aliases[i])) return i;
    assert(false); return -1;
}
static struct ain_hll_function *get_fun(int libno, const char *name) {
    assert(libno == 27);
    int i = alias_index(name);
    return declared[i] ? &declarations[i] : NULL;
}
static void static_library_replace(struct static_library *lib, const char *name, void *fn) {
    assert(lib == &lib_PartsEngine);
    bindings[alias_index(name)] = fn;
}
'''

tests = r'''
static void reset_binding(int i, bool modern) {
    declared[i] = true;
    declarations[i] = (struct ain_hll_function){
        .name = (char *)aliases[i],
        .return_type = modern ? AIN_STRING_TYPE : AIN_VOID_TYPE,
        .nr_arguments = modern ? 2 : 3,
        .arguments = arguments[i],
    };
    for (int j = 0; j < 3; j++) arguments[i][j].type = AIN_INT_TYPE;
    if (!modern) arguments[i][1].type = AIN_MKTYPE(AIN_REF_STRING, -1, 0);
    bindings[i] = PE_GetPartsCGName;
}

static struct string *call_modern(int i, int state) {
    int number = 7;
    ffi_type *types[] = {&ffi_type_sint32, &ffi_type_sint32};
    void *args[] = {&number, &state};
    ffi_cif cif;
    assert(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 2, &ffi_type_pointer, types) == FFI_OK);
    struct string *result = NULL;
    ffi_call(&cif, FFI_FN(bindings[i]), &result, args);
    return result;
}

static void call_legacy(int i, int state, struct string **out) {
    int number = 7;
    struct string **out_value = out;
    ffi_type *types[] = {&ffi_type_sint32, &ffi_type_pointer, &ffi_type_sint32};
    void *args[] = {&number, &out_value, &state};
    ffi_cif cif;
    assert(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, 3, &ffi_type_void, types) == FFI_OK);
    ffi_call(&cif, FFI_FN(bindings[i]), NULL, args);
}

static void check_alias(int i) {
    reset_binding(i, false);
    parts_link_cg_name_getter(27, aliases[i]);
    assert(bindings[i] == (void *)PE_GetPartsCGName);
    struct string *out = make_string("previous", 8);
    call_legacy(i, 1, &out);
    assert(out && !strcmp(out->text, "cg/test.ald"));
    assert(out != fixture_parts.cg[0].name && out->ref == 1);
    struct string *unchanged = out;
    call_legacy(i, 2, &out);
    assert(out == unchanged); /* Old no-CG contract leaves caller's string alone. */
    free_string(out);

    reset_binding(i, true);
    parts_link_cg_name_getter(27, aliases[i]);
    assert(bindings[i] == (void *)PE_v14_GetPartsCGName);
    out = call_modern(i, 1);
    assert(out && !strcmp(out->text, "cg/test.ald"));
    assert(out != fixture_parts.cg[0].name && out->ref == 1);
    free_string(out);
    assert(!strcmp(fixture_parts.cg[0].name->text, "cg/test.ald"));
    for (int n = 0; n < 100; n++) {
        unsigned before = EMPTY_STRING.ref;
        out = call_modern(i, 2);
        assert(out == &EMPTY_STRING && !out->size && !out->text[0]);
        assert(EMPTY_STRING.ref == before + 1);
        free_string(out);
        assert(EMPTY_STRING.ref == before);
    }
    out = call_modern(i, 0); /* Invalid legacy state also yields a valid empty return. */
    assert(out == &EMPTY_STRING);
    free_string(out);

    /* Each part of the declaration gate must match; unrelated ABIs stay untouched. */
    for (int mode = 0; mode < 7; mode++) {
        reset_binding(i, true);
        switch (mode) {
        case 0: declarations[i].return_type = AIN_VOID_TYPE; break;
        case 1: declarations[i].nr_arguments = 1; break;
        case 2: declarations[i].nr_arguments = 3; break;
        case 3: arguments[i][0].type.data = AIN_FLOAT; break;
        case 4: arguments[i][1].type.data = AIN_REF_STRING; break;
        case 5: declarations[i].arguments = NULL; break;
        case 6: declared[i] = false; break;
        }
        parts_link_cg_name_getter(27, aliases[i]);
        assert(bindings[i] == (void *)PE_GetPartsCGName);
    }
    printf("%s: legacy ABI, two-int/string ABI, empty ownership, signature gate: PASS\n", aliases[i]);
}

int main(void) {
    fixture_parts.cg[0].name = make_string("cg/test.ald", 11);
    unsigned empty_before = EMPTY_STRING.ref;
    check_alias(0);
    check_alias(1);
    assert(EMPTY_STRING.ref == empty_before);
    free_string(fixture_parts.cg[0].name);
    puts("CG name ABI fixture: all checks passed");
    return 0;
}
'''

fixture = HERE / "cg_name_abi_fixture.c"
fixture.write_text(prefix + legacy + "\n" + adapter + "\n" + selector + tests)
binary = HERE / "cg_name_abi_fixture"
command = [
    "/Library/Developer/CommandLineTools/usr/bin/clang",
    "-std=c11", "-O0", "-g", "-fsanitize=address,undefined",
    "-I" + str(SOURCE / "subprojects/libsys4/include"),
    "-I/opt/homebrew/opt/libffi/include",
    str(fixture),
    str(STAGE1 / "wip-asan-build/subprojects/libsys4/libsys4.a"),
    "-L/opt/homebrew/opt/libffi/lib", "-lffi", "-lz", "-lm",
    "-framework", "CoreFoundation", "-o", str(binary),
]
compiled = subprocess.run(command, capture_output=True, text=True)
assert compiled.returncode == 0, compiled.stdout + compiled.stderr
env = dict(os.environ)
env["ASAN_OPTIONS"] = "halt_on_error=1:abort_on_error=1"
env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
ran = subprocess.run([str(binary)], env=env, capture_output=True, text=True)
result = {
    "source": str(hll_path),
    "source_sha256": hashlib.sha256(hll_path.read_bytes()).hexdigest(),
    "legacy_source_sha256": hashlib.sha256(legacy_path.read_bytes()).hexdigest(),
    "fixture_sha256": hashlib.sha256(fixture.read_bytes()).hexdigest(),
    "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
    "compile_command": command,
    "compile_exit": compiled.returncode,
    "compile_stderr": compiled.stderr,
    "test_exit": ran.returncode,
    "stdout": ran.stdout,
    "stderr": ran.stderr,
}
(HERE / "cg_name_abi_result.json").write_text(json.dumps(result, indent=2) + "\n")
print(ran.stdout, end="")
if ran.stderr:
    print(ran.stderr, end="")
raise SystemExit(ran.returncode)
