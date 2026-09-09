#!/usr/bin/env python3
"""Compile only the production FFI fixture; never launch the game or build it."""
import json
from pathlib import Path
import subprocess

here = Path(__file__).resolve().parent
stage2 = here.parent
stage1 = stage2.parent / "stage1"
source = stage2 / "source"
base = [
    "cc", "-std=c11", "-O1", "-g", "-D_DEFAULT_SOURCE",
    "-isysroot", "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
    "-I" + str(source / "include"),
    "-I" + str(source / "subprojects/libsys4/include"),
    "-I/opt/homebrew/opt/libffi/include",
]
results = []
for name, implementation, expected, sanitizer in [
    ("before", stage1 / "wip-source/src/ffi.c", 1, False),
    ("after", source / "src/ffi.c", 0, False),
    ("after-sanitizer", source / "src/ffi.c", 0, True),
]:
    command = base + ['-DFFI_SOURCE="' + str(implementation) + '"']
    if sanitizer:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    command += [str(here / "nested_context.c"), "-Wl,-dead_strip",
                "-L/opt/homebrew/opt/libffi/lib", "-lffi", "-o", str(here / name)]
    build = subprocess.run(command, text=True, capture_output=True)
    (here / (name + "-build.log")).write_text(build.stdout + build.stderr)
    if build.returncode:
        raise SystemExit(build.stderr)
    run = subprocess.run([str(here / name)], text=True, capture_output=True)
    (here / (name + ".log")).write_text(run.stdout + run.stderr)
    results.append({"case": name, "implementation": str(implementation),
                    "compile_command": command, "exit_code": run.returncode,
                    "expected_exit_code": expected, "stdout": run.stdout,
                    "stderr": run.stderr})
    print(name + ": " + run.stdout.strip())
    if run.returncode != expected:
        raise SystemExit(run.stderr or "Unexpected exit code")
(here / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n")
