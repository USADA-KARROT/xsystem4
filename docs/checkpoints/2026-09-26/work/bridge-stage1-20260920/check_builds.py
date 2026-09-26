from pathlib import Path
import concurrent.futures, hashlib, json, subprocess
here=Path(__file__).resolve().parent
root=here.parents[1]
source=root/'work/stage2/source'
def manifest():
    names=subprocess.check_output(['git','ls-files','-cz','--others','--exclude-standard'],cwd=source).decode().split('\0')
    return {n:hashlib.sha256((source/n).read_bytes()).hexdigest() for n in sorted(set(names)) if n and n!='subprojects/.wraplock' and (source/n).is_file()}
before=manifest()
def build(variant):
    directory=root/('work/stage2/'+variant+'-build')
    command=['ninja','-C',str(directory),'-j4']
    run=subprocess.run(command,capture_output=True,text=True)
    (here/('build-'+variant+'.log')).write_text(run.stdout+run.stderr)
    exe=directory/'src/xsystem4'
    return {'variant':variant,'command':command,'exit':run.returncode,'binary':str(exe),
            'sha256':hashlib.sha256(exe.read_bytes()).hexdigest() if run.returncode==0 else None}
with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
    builds=list(pool.map(build,['optimized','asan']))
after=manifest()
assert before==after, 'Source changed while building'
result={'source_unchanged_during_build':True,'source_sha256':after,'builds':builds,'scope':'Compiled complete engines only; no game launch or GUI validation.'}
(here/'build-results.json').write_text(json.dumps(result,indent=2)+'\n')
for b in builds:print(json.dumps(b))
if any(b['exit'] for b in builds):raise SystemExit(1)
for b in builds:
    directory=root/('work/stage2/'+b['variant']+'-build')
    (directory/'build-source-manifest.json').write_text(json.dumps(after,indent=2)+'\n')
    (directory/'build-source.patch').write_bytes(subprocess.check_output(['git','diff','HEAD'],cwd=source))
