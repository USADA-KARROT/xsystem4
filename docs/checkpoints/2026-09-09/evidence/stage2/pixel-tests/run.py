#!/usr/bin/env python3
from pathlib import Path
import json,subprocess
here=Path(__file__).resolve().parent
source=here.parent/'source'
base=['cc','-std=gnu11','-D_DEFAULT_SOURCE','-O1','-g','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk',
 '-I'+str(source/'include'),'-I'+str(source/'src/parts'),'-I'+str(source/'subprojects/libsys4/include'),
 '-I/opt/homebrew/include','-I/opt/homebrew/include/SDL2']
results=[]
for name,input_source,expected,sanitizer in [('before',here/'before-input.c',1,False),('after',source/'src/parts/input.c',0,False),('after-sanitizer',source/'src/parts/input.c',0,True)]:
 cmd=base+['-DINPUT_SOURCE="'+str(input_source)+'"','-DACTIVITY_SOURCE="'+str(source/'src/hll/pe_v14_activity.c')+'"']
 if sanitizer:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
 cmd += [str(here/'pixel_contract.c'),str(source/'src/parts/parts.c'),str(here.parent.parent/'wip-build/subprojects/libsys4/libsys4.a')]
 # libsys4 is stage1's already-built library.
 cmd[-1]=str(here.parent.parent/'stage1/wip-build/subprojects/libsys4/libsys4.a')
 cmd += ['-L/opt/homebrew/lib','-lSDL2','-lz','-framework','CoreFoundation','-Wl,-dead_strip','-o',str(here/name)]
 build=subprocess.run(cmd,text=True,capture_output=True)
 (here/(name+'-build.log')).write_text(build.stdout+build.stderr)
 if build.returncode:raise SystemExit(build.stderr)
 run=subprocess.run([str(here/name)],text=True,capture_output=True)
 (here/(name+'.log')).write_text(run.stdout+run.stderr)
 results.append({'case':name,'command':cmd,'exit_code':run.returncode,'stdout':run.stdout,'stderr':run.stderr})
 print(name+': '+run.stdout.strip())
 if run.returncode!=expected:raise SystemExit(run.stderr or 'unexpected status')
(here/'results.json').write_text(json.dumps(results,indent=2)+'\n')
