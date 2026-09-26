#!/usr/bin/env python3
"""Rebuild the read-only AIN inventory using an existing xsystem4/libsys4 checkout."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess

p = argparse.ArgumentParser()
p.add_argument('--source', type=Path, required=True, help='xsystem4 source checkout')
p.add_argument('--lib', type=Path, required=True, help='existing normal libsys4.a')
p.add_argument('--asan-lib', type=Path, help='existing instrumented libsys4.a')
p.add_argument('--ain', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
here = Path(__file__).resolve().parent
results = []
outputs = []
for label, library in [('normal', a.lib), ('asan', a.asan_lib)]:
    if library is None:
        continue
    binary = a.out / ('ain_surface_' + label)
    command = ['clang', '-std=c11', '-O2' if label == 'normal' else '-O0', '-g']
    if label == 'asan':
        command += ['-fsanitize=address,undefined']
    command += ['-I' + str(a.source / 'subprojects/libsys4/include'), str(here / 'ain_surface.c'),
                str(library), '-lz', '-lm', '-framework', 'CoreFoundation', '-o', str(binary)]
    compiled = subprocess.run(command, capture_output=True, text=True, errors='replace')
    record = {'variant': label, 'compile_command': command, 'compile_exit': compiled.returncode,
              'compiler_stderr': compiled.stderr}
    results.append(record)
    if compiled.returncode:
        break
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    run = subprocess.run([str(binary.resolve()), str(a.ain.resolve())], env=env,
                         capture_output=True, text=True, errors='replace')
    record.update(run_exit=run.returncode, stderr=run.stderr)
    if run.returncode:
        break
    data = json.loads(run.stdout)
    outputs.append(data)
    (a.out / (label + '-surface.json')).write_text(json.dumps(data, indent=2) + '\n')
    record['decoded_bytes'] = data['decoded_bytes']
    record['matches_code_size'] = data['decoded_bytes'] == data['code_bytes']
    record['non_sentinel_invalid_addresses'] = data['invalid_function_addresses'] - data['sentinel_function_addresses']
result = {'ain_sha256': hashlib.sha256(a.ain.read_bytes()).hexdigest(),
          'probe_sha256': hashlib.sha256((here/'ain_surface.c').read_bytes()).hexdigest(),
          'runs': results, 'normal_asan_equal': outputs[0] == outputs[1] if len(outputs) == 2 else None,
          'scope': 'Static metadata and bytecode decoding only; does not run a game or a new VM. LeakSanitizer disabled.'}
(a.out / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
ok = len(outputs) == (2 if a.asan_lib else 1) and all(r.get('run_exit') == 0 for r in results)
if len(outputs) == 2:
    ok = ok and result['normal_asan_equal']
print(json.dumps({'ok': ok, 'normal_asan_equal': result['normal_asan_equal'], 'out': str(a.out)}, indent=2))
raise SystemExit(0 if ok else 1)
