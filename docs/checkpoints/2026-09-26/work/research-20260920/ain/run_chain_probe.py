#!/usr/bin/env python3
from pathlib import Path
import subprocess,json,hashlib
out=Path(__file__).resolve().parent;workspace=out.parents[2];stage=workspace/'work/stage2';root=stage/'source';lib=stage/'normal-build/subprojects/libsys4/libsys4.a';ain=stage/'game/dohnadohna.ain'
cmd=['/usr/bin/clang','-std=c11','-D_DEFAULT_SOURCE','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk','-I'+str(root/'subprojects/libsys4/include'),str(out/'probe_chain.c'),str(lib),'-lz','-lm','-framework','CoreFoundation','-o',str(out/'probe_chain')]
r=subprocess.run(cmd,capture_output=True,text=True,errors="replace");record={'compile_command':cmd,'compile_exit':r.returncode,'compile_stderr':r.stderr,'ain_sha256':hashlib.sha256(ain.read_bytes()).hexdigest()}
if not r.returncode:
 r=subprocess.run([str(out/'probe_chain'),str(ain)],capture_output=True,text=True,errors="replace");record.update(probe_exit=r.returncode,stderr=r.stderr)
 if not r.returncode:
  j=json.loads(r.stdout);(out/'chain-bytecode.json').write_text(json.dumps(j,indent=2)+'\n');print([(f['id'],hex(f['address']),len(f['sites'])) for f in j['functions']])
(out/'chain-probe-result.json').write_text(json.dumps(record,indent=2)+'\n');print('compile',record['compile_exit'],'probe',record.get('probe_exit'),record['compile_stderr'],record.get('stderr'))
