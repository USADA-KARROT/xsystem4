#!/usr/bin/env python3
"""Compile only production mouse routines with fake SDL clock/events, no UI."""
import json
from pathlib import Path
import subprocess

here=Path(__file__).resolve().parent
source=here.parent / "source"
# The original expiry block was inline in handle_events. Expose its exact
# source text as a fixture entry point; do not reimplement its algorithm.
before=(here/"input-before.c").read_text()
start=before.index("\t// Deferred mouse release: clear held buttons whose hold period expired")
end=before.index("\n\tSDL_Event e;",start)
block=before[start:end]
(here/"input-before-callable.c").write_text(before+
    "\nstatic void release_pending_mouse_buttons(uint32_t now) {\n(void)now;\n"+block+"\n}\n")
base=["cc","-std=c11","-O1","-g","-D_DEFAULT_SOURCE",
      "-isysroot","/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
      "-I"+str(source/"include"),"-I"+str(source/"subprojects/libsys4/include"),
      "-I/opt/homebrew/include","-I/opt/homebrew/include/SDL2"]
results=[]
for name,implementation,expected,sanitizer in [
    ("before",here/"input-before-callable.c",1,False),
    ("after",source/"src/input.c",0,False),
    ("after-sanitizer",source/"src/input.c",0,True)]:
    cmd=base+['-DINPUT_SOURCE="'+str(implementation)+'"']
    if sanitizer:cmd += ["-fsanitize=address,undefined","-fno-omit-frame-pointer"]
    cmd += [str(here/"mouse_deadline.c"),"-Wl,-dead_strip","-o",str(here/name)]
    build=subprocess.run(cmd,text=True,capture_output=True)
    (here/(name+"-build.log")).write_text(build.stdout+build.stderr)
    if build.returncode:raise SystemExit(build.stderr)
    run=subprocess.run([str(here/name)],text=True,capture_output=True)
    (here/(name+".log")).write_text(run.stdout+run.stderr)
    results.append({"case":name,"implementation":str(implementation),"compile_command":cmd,
                    "exit_code":run.returncode,"expected_exit_code":expected,
                    "stdout":run.stdout,"stderr":run.stderr})
    print(name+": "+run.stdout.strip())
    if run.returncode != expected:raise SystemExit(run.stderr or "Unexpected status")
(here/"results.json").write_text(json.dumps(results,ensure_ascii=False,indent=2)+"\n")
