from pathlib import Path
import argparse, plistlib, shutil, hashlib
p=argparse.ArgumentParser()
p.add_argument('variant',choices=['normal','asan','optimized'])
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
name='Xsystem4-stage2-'+a.variant
content=root/'apps'/(name+'.app')/'Contents'
(content/'MacOS').mkdir(parents=True,exist_ok=True)
source=root/(a.variant+'-build')/'src/xsystem4'
binary=content/'MacOS/xsystem4'
shutil.copy2(source,binary)
(content/'Info.plist').write_bytes(plistlib.dumps({
    'CFBundleExecutable':'xsystem4',
    'CFBundleIdentifier':'com.usadakarrot.xsystem4.stage2.'+a.variant,
    'CFBundleName':name,'CFBundleDisplayName':name,
    'CFBundlePackageType':'APPL','CFBundleVersion':'2',
    'NSHighResolutionCapable':True}))
assert hashlib.sha256(source.read_bytes()).digest()==hashlib.sha256(binary.read_bytes()).digest()
print(binary)
