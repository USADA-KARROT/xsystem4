#!/usr/bin/env python3
"""Extract query code without editing production; load real AIN; run bounded libffi tests."""
from pathlib import Path
import hashlib, json, os, re, subprocess

HERE = Path(__file__).resolve().parent
WORK = HERE.parent
SOURCE = WORK / 'stage2/source'
AIN = WORK / 'stage2/game/dohnadohna.ain'
DUMP = Path('<USER_HOME>/Claude/projects/dohna-cn-dump/ain_code.txt')

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def function(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

array_path = SOURCE / 'src/hll/Array.c'
baseline_path = HERE / 'baseline/Array.c'
source = array_path.read_text()
baseline = baseline_path.read_text()
signatures = [
    'static inline bool array_elem_is_ref(', 'static inline bool array_elem_is_2slot(',
    'static int array_erase_stride(', 'static bool array_erase_value_equal(',
    'static int Array_Numof(', 'static int Array_Count(',
]
production = '\n\n'.join(function(source, s) for s in signatures)
query_start = source.index('static bool array_query_callback_shape(')
production += '\n\n' + source[query_start:source.index('// Copy: copy elements', query_start)]
before = '\n\n'.join(function(baseline, 'static int '+name+'(').replace(
    name+'(', name+'_before(', 1) for name in ['Array_Numof','Array_Count','Array_Find'])
# Keep only technical call-site identity; never copy narrative/game text.
lines = DUMP.read_text(errors='replace').splitlines()
overloads = {'Numof#1':21, 'Count#1':23, 'Find#2':46, 'Find#3':48}
sites, unresolved = [], []
for i, line in enumerate(lines):
    m = re.fullmatch(r'\s*CALLHLL Array (Numof#1|Count#1|Find#2|Find#3) (\d+)', line)
    if not m:
        continue
    prev = re.fullmatch(r'\s*PUSH (\d+)', lines[i-1])
    if prev:
        sites.append((i+1, int(prev[1]), int(m[2]), overloads[m[1]]))
    else:
        unresolved.append(i+1)
template = (HERE / 'array_queries_fixture.template.c').read_text()
fixture = template.replace('/* @PRODUCTION@ */', production).replace('/* @BASELINE@ */', before)
fixture = fixture.replace('/* @CALLBACK_SITES@ */', ','.join('{'+','.join(map(str,s))+'}' for s in sites))
cpath = HERE / 'array_queries_fixture.c'
cpath.write_text(fixture)
result = {'scope':'Actual AIN declarations + extracted production query code + libffi; controlled callback stub, no game or VM execution',
          'inputs':{str(p):digest(p) for p in [array_path,baseline_path,AIN,DUMP,HERE/'array_queries_fixture.template.c']},
          'extracted_code_sha256':hashlib.sha256(production.encode()).hexdigest(),
          'fixture_sha256':digest(cpath), 'callback_site_unresolved_lines':unresolved, 'runs':{}}
common = ['/usr/bin/clang','-std=c11','-O0','-g','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk',
          '-I'+str(SOURCE/'include'),'-I'+str(SOURCE/'subprojects/libsys4/include'),
          '-I/opt/homebrew/opt/libffi/include',str(cpath)]
for name, flags, build in [('normal', [], 'normal-build'),
                           ('asan-ubsan',['-fsanitize=address,undefined'],'asan-build')]:
    binary = HERE / ('array_queries_'+name)
    command = common + flags + [str(WORK/'stage2'/build/'subprojects/libsys4/libsys4.a'),
              '-L/opt/homebrew/opt/libffi/lib','-lffi','-lz','-lm','-framework','CoreFoundation','-o',str(binary)]
    compile_result = subprocess.run(command,capture_output=True,text=True,errors='replace')
    record = {'compile_command':command,'compile_returncode':compile_result.returncode,
              'compile_diagnostics':compile_result.stderr}
    result['runs'][name]=record
    if compile_result.returncode == 0:
        env=os.environ.copy()
        env['ASAN_OPTIONS']='detect_leaks=0:halt_on_error=1:abort_on_error=1'
        env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
        run = subprocess.run([str(binary),str(AIN)],capture_output=True,text=True,errors='replace',env=env)
        record.update(returncode=run.returncode, binary_sha256=digest(binary), stderr=run.stderr)
        try: record['evidence']=json.loads(run.stdout)
        except json.JSONDecodeError: record['stdout']=run.stdout
    (HERE/'array-query-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    print(name, 'compile', compile_result.returncode, 'run', record.get('returncode'))
    if compile_result.returncode or record.get('returncode'):
        print(record.get('compile_diagnostics','') + record.get('stderr',''))
        raise SystemExit(1)
