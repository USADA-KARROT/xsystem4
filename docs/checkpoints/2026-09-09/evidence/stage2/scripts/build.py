from pathlib import Path
import argparse, hashlib, json, subprocess
p=argparse.ArgumentParser();p.add_argument('variant',choices=['normal','asan','optimized']);a=p.parse_args()
r=Path(__file__).resolve().parents[1];build=r/(a.variant+'-build')
def diff():return subprocess.check_output(['git','-C',str(r/'source'),'diff','HEAD'],text=True).strip()
def manifest():
    source=r/'source'
    files=subprocess.check_output(['git','ls-files','-cz','--others','--exclude-standard'],cwd=source).decode().split('\0')
    return {name:hashlib.sha256((source/name).read_bytes()).hexdigest()
            for name in sorted(set(files)) if name and name != 'subprojects/.wraplock' and (source/name).is_file()}
before=diff()
before_files=manifest()
subprocess.run(['ninja','-C',str(build),'-j4'],check=True)
after=diff()
after_files=manifest()
if before!=after or before_files!=after_files:raise RuntimeError('Source changed while building; rebuild when edits are complete.')
(build/'build-source.patch').write_text(after)
(build/'build-source-manifest.json').write_text(json.dumps(after_files,indent=2))
subprocess.run(['python3',str(r/'scripts/make_bundle.py'),a.variant],check=True)
