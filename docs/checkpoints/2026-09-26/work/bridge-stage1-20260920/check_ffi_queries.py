from pathlib import Path
import hashlib, json, os, subprocess
here=Path(__file__).resolve().parent
root=here.parents[1]
source=root/'work/stage2/source'
text=(source/'src/hll/Array.c').read_text()
def function(signature):
    start=text.index(signature); end=text.index('{',start)+1; depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]
helpers=['static inline bool array_elem_is_ref(', 'static inline bool array_elem_is_2slot(',
         'static int array_erase_stride(', 'static int Array_Numof(', 'static int Array_Count(']
block=text[text.index('/* Array query overloads.'):text.index('// Copy: copy elements')]
(here/'query-production.inc').write_text('\n'.join(map(function,helpers))+'\n'+block)
base=['clang','-std=c11','-O1','-g','-D_DEFAULT_SOURCE','-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk',
      '-I'+str(source/'include'),'-I'+str(source/'subprojects/libsys4/include'),'-I/opt/homebrew/opt/libffi/include',
      '-DFFI_SOURCE="'+str(source/'src/ffi.c')+'"',str(here/'ffi_queries.c'),'-Wl,-dead_strip',
      '-L/opt/homebrew/opt/libffi/lib','-lffi','-lz','-lm','-framework','CoreFoundation']
results=[]
for label,sanitize in [('normal',False),('sanitizer',True)]:
    binary=here/('ffi-queries-'+label)
    library=root/('work/stage1/wip-asan-build/subprojects/libsys4/libsys4.a' if sanitize else 'work/stage2/optimized-build/subprojects/libsys4/libsys4.a')
    cmd=base+[str(library),'-o',str(binary)]
    if sanitize:cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    build=subprocess.run(cmd,capture_output=True,text=True)
    (here/('ffi-'+label+'-build.log')).write_text(build.stdout+build.stderr)
    if build.returncode:raise SystemExit(build.stderr)
    for mutation in [False,True]:
        run=subprocess.run([str(binary),str(root/'work/stage2/game/dohnadohna.ain')]+(['mutation'] if mutation else []),
            capture_output=True,text=True,errors='replace',env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:abort_on_error=1',UBSAN_OPTIONS='halt_on_error=1'))
        expected=42 if mutation else 0
        ok=run.returncode==expected and (not mutation or 'predicate changed array storage' in run.stderr)
        result={'variant':label,'mutation':mutation,'expected_exit':expected,'exit':run.returncode,'passed':ok,'stdout':run.stdout,'stderr':run.stderr,'compile_command':cmd}
        results.append(result)
        print(json.dumps({k:result[k] for k in ['variant','mutation','exit','passed','stdout']},ensure_ascii=False))
        if not ok:
            print(run.stderr);raise SystemExit(1)
identity={name:hashlib.sha256((source/name).read_bytes()).hexdigest() for name in ['src/ffi.c','src/hll/Array.c']}
(here/'ffi-results.json').write_text(json.dumps({'scope':'Production static linker, hll_call, Array query implementation and real libffi/AIN; controlled VM callback and heap services. LeakSanitizer disabled.','source_sha256':identity,'cases':results},ensure_ascii=False,indent=2)+'\n')
