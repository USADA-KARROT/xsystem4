#!/usr/bin/env python3
"""Run only stage2 builds with fresh isolated saves; preserve originals."""
from pathlib import Path
import argparse, datetime, hashlib, json, os, signal, subprocess, time

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser()
p.add_argument('variant', choices=['normal','asan','optimized'])
p.add_argument('tag')
p.add_argument('--seconds', type=int, default=180)
p.add_argument('--until-closed', action='store_true', help='Run until the user closes the game, without a time limit')
p.add_argument('--click-seq')
p.add_argument('--trace-fno')
p.add_argument('--bundle', action='store_true')
p.add_argument('--perf', action='store_true')
p.add_argument('--game-debug', action='store_true')
p.add_argument('--no-frame-capture', action='store_true')
p.add_argument('--trace', action='store_true')
p.add_argument('--trace-after', type=int, default=19000)
p.add_argument('--trace-fnos')
p.add_argument('--trace-count', type=int, default=8)
p.add_argument('--trace-from-msg', type=int)
p.add_argument('--trace-watch-fno', type=int)
p.add_argument('--view-size', choices=['1280x720'])
a=p.parse_args()
if not a.tag.replace('-','').replace('_','').isalnum():
    p.error('tag must contain only letters, digits, underscore or hyphen')
if not 1 <= a.seconds <= 600:
    p.error('duration must be 1..600 seconds')
source=ROOT/'source'
binary=ROOT/(a.variant+'-build')/'src/xsystem4'
if a.bundle:
    binary=ROOT/'apps'/('Xsystem4-stage2-'+a.variant+'.app')/'Contents/MacOS/xsystem4'
game=ROOT/'game'
run=ROOT/'runs'/a.tag
run.mkdir(parents=True, exist_ok=False)
for name in ['home','saves','frames']:(run/name).mkdir()
if not binary.is_file() or not (game/'dohnadohna.ain').is_file():
    p.error('missing isolated binary or game')
# Never inherit scripted clicks, skip/hold states or diagnostic mode settings.
env={k:v for k,v in os.environ.items() if not k.startswith(('XSYS4_','XSYSTEM4_'))}
env['XSYSTEM4_HOME']=str(run/'home')
if not a.no_frame_capture:env['XSYS4_SCREENSHOT_DIR']=str(run/'frames')
if a.perf:env['XSYS4_STAGE2_PERF']='1'
if a.game_debug:env['XSYS4_GAME_DEBUG']='1'
env['XSYS4_STOP_ON_GAME_ERROR']='1'
if a.click_seq:env['XSYS4_AUTO_CLICK_SEQ']=a.click_seq
if a.trace_fno:env['XSYS4_TRACE_FNO']=a.trace_fno
if a.trace:
    env['XSYS4_STAGE2_TRACE']='1'
    env['XSYS4_STAGE2_AFTER_MS']=str(a.trace_after)
    env['XSYS4_STAGE2_PER_FNO']=str(a.trace_count)
    if a.trace_fnos:env['XSYS4_STAGE2_FNOS']=a.trace_fnos
    if a.trace_from_msg is not None:env['XSYS4_STAGE2_FROM_MSG']=str(a.trace_from_msg)
    if a.trace_watch_fno is not None:env['XSYS4_STAGE2_WATCH_FNO']=str(a.trace_watch_fno)
env['ASAN_OPTIONS']='detect_leaks=0:abort_on_error=1:symbolize=1'
env['UBSAN_OPTIONS']='print_stacktrace=1:halt_on_error=1'
game_arg=game
if a.view_size:
    game_arg=game/('stage2-'+a.tag+'.ini')
    game_arg.write_bytes((game/'AliceStart.ini').read_bytes()+b'\r\nViewWidth = 1280\r\nViewHeight = 720\r\n')
cmd=[str(binary),'--echo-message','--save-folder',str(run/'saves'),str(game_arg)]
def git(path,*args):return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()
snapshot=ROOT/(a.variant+'-build')/'build-source.patch'
source_patch=snapshot.read_text() if snapshot.exists() else git(source,'diff','HEAD')
meta={'started_at':datetime.datetime.now().astimezone().isoformat(),'command':cmd,'cwd':str(source),
      'source_sha':git(source,'rev-parse','HEAD'),'source_status':git(source,'status','--short'),
      'source_diff_sha256':hashlib.sha256(source_patch.encode()).hexdigest(),
      'source_patch_from_build_snapshot':snapshot.exists(),
      'libsys4_sha':git(source/'subprojects/libsys4','rev-parse','HEAD'),
      'binary_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
      'environment':{k:v for k,v in env.items() if k.startswith(('XSYS4_','XSYSTEM4_','ASAN_','UBSAN_'))},
      'duration_limit_seconds':None if a.until_closed else a.seconds,'diagnostic_view_size':a.view_size,
      'ini_sha256':hashlib.sha256((game_arg if a.view_size else game/'AliceStart.ini').read_bytes()).hexdigest()}
(run/'source.patch').write_text(source_patch)
manifest=ROOT/(a.variant+'-build')/'build-source-manifest.json'
if manifest.exists():
    contents=manifest.read_bytes()
    (run/'source-manifest.json').write_bytes(contents)
    meta['source_manifest_sha256']=hashlib.sha256(contents).hexdigest()
with (run/'engine.log').open('wb') as log:
    proc=subprocess.Popen(cmd,cwd=source,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
    meta['pid']=proc.pid
    (run/'run.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2))
    print(json.dumps({'pid':proc.pid,'run':str(run)},ensure_ascii=False),flush=True)
    begin=time.monotonic()
    try:
        while proc.poll() is None and (a.until_closed or time.monotonic()-begin < a.seconds):
            time.sleep(0.2)
        if proc.poll() is None:
            meta['stopped_by_runner']=True
            proc.terminate()
            try:proc.wait(timeout=8)
            except subprocess.TimeoutExpired:proc.kill();proc.wait()
    except KeyboardInterrupt:
        meta['stopped_by_operator']=True
        proc.terminate()
        try:proc.wait(timeout=8)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
    meta['exit_code']=proc.returncode
    meta['elapsed_seconds']=round(time.monotonic()-begin,3)
    meta['finished_at']=datetime.datetime.now().astimezone().isoformat()
    (run/'run.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2))
    print(json.dumps({'exit_code':proc.returncode,'elapsed_seconds':meta['elapsed_seconds'],'run':str(run)},ensure_ascii=False))
