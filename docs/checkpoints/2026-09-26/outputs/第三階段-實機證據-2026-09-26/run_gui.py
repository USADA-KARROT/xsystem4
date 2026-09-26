#!/usr/bin/env python3
"""Bounded GUI test; copies current build, isolates saves, records actual source.

wait4 records usage for exactly the game child (Darwin ru_maxrss is bytes).
"""
import argparse, datetime, hashlib, json, os, plistlib, shutil, signal, subprocess, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STAGE2 = ROOT.parent / 'stage2'
p = argparse.ArgumentParser()
p.add_argument('tag')
p.add_argument('--variant', choices=['optimized', 'asan'], default='optimized')
p.add_argument('--seconds', type=int, default=90)
p.add_argument('--trace-fnos')
a = p.parse_args()
if not a.tag.replace('-', '').replace('_', '').isalnum() or not 1 <= a.seconds <= 300:
    p.error('safe unique tag and duration 1..300 required')
source = STAGE2 / 'source'
build = STAGE2 / (a.variant + '-build')
ninja = shutil.which('ninja')
if not ninja and Path('/opt/homebrew/bin/ninja').is_file():
    ninja = '/opt/homebrew/bin/ninja'
if not ninja:
    p.error('ninja not found; the build tool must be available before testing')
subprocess.run([ninja, '-C', str(build), 'src/xsystem4'], check=True)
run = ROOT / 'runs' / a.tag
run.mkdir(parents=True, exist_ok=False)
for name in ['home', 'saves']:
    (run / name).mkdir()
app = ROOT / 'apps' / ('Xsystem4-stage3-' + a.variant + '.app')
binary = app / 'Contents/MacOS/xsystem4'
binary.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(build / 'src/xsystem4', binary)
with (app / 'Contents/Info.plist').open('wb') as f:
    plistlib.dump(dict(CFBundleDisplayName=app.stem, CFBundleName=app.stem,
        CFBundleExecutable='xsystem4', CFBundlePackageType='APPL', CFBundleVersion='3',
        CFBundleIdentifier='com.usadakarrot.xsystem4.stage3.' + a.variant,
        NSHighResolutionCapable=True), f)
env = {k:v for k,v in os.environ.items() if not k.startswith(('XSYS4_', 'XSYSTEM4_'))}
env.update(XSYSTEM4_HOME=str(run / 'home'), XSYS4_STAGE2_PERF='1',
    ASAN_OPTIONS='detect_leaks=0:abort_on_error=1:symbolize=1',
    UBSAN_OPTIONS='print_stacktrace=1:halt_on_error=1')
if a.trace_fnos:
    env.update(XSYS4_STAGE2_TRACE='1', XSYS4_STAGE2_AFTER_MS='0',
        XSYS4_STAGE2_PER_FNO='8', XSYS4_STAGE2_FNOS=a.trace_fnos)
cmd = [str(binary), '--echo-message', '--save-folder', str(run / 'saves'), str(STAGE2 / 'game')]
patch = subprocess.check_output(['git', '-C', str(source), 'diff', 'HEAD'])
(run / 'source.patch').write_bytes(patch)
sha = lambda data: hashlib.sha256(data).hexdigest()
meta = dict(started_at=datetime.datetime.now().astimezone().isoformat(), command=cmd,
    binary_sha256=sha(binary.read_bytes()), source_diff_sha256=sha(patch),
    source_head=subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'], text=True).strip(),
    environment={k:v for k,v in env.items() if k.startswith(('XSYS4_', 'XSYSTEM4_', 'ASAN_', 'UBSAN_'))},
    duration_limit_seconds=a.seconds, frame_capture=False,
    ain_sha256=sha((STAGE2/'game/dohnadohna.ain').read_bytes()),
    ini_sha256=sha((STAGE2/'game/AliceStart.ini').read_bytes()),
    libsys4_head=subprocess.check_output(['git','-C',str(source/'subprojects/libsys4'),'rev-parse','HEAD'], text=True).strip())
def save():
    (run / 'run.json').write_text(json.dumps(meta, ensure_ascii=False, indent=2))
with (run / 'engine.log').open('wb') as log:
    proc = subprocess.Popen(cmd, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT,
        start_new_session=True)
    meta['pid'] = proc.pid
    save()
    print(json.dumps(dict(pid=proc.pid, run=str(run), app=str(app))), flush=True)
    begin = time.monotonic()
    stop_at = None
    log_offset = 0
    carry = b''
    try:
        while True:
            pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
            if pid:
                proc.returncode = os.waitstatus_to_exitcode(status)
                break
            elapsed = time.monotonic() - begin
            if stop_at is None:
                with (run / 'engine.log').open('rb') as reader:
                    reader.seek(log_offset)
                    new_log = reader.read()
                    log_offset = reader.tell()
                tail = (carry + new_log).lower()
                carry = tail[-256:]
                error = any(x in tail for x in [b'error: addresssanitizer', b'runtime error:', b'system.error:', b'vm error', b'vm_call_timeout:', b'assertion failed'])
                if error or elapsed >= a.seconds or (run / 'STOP').exists():
                    meta['stop_reason'] = 'error_log' if error else ('operator' if (run/'STOP').exists() else 'duration')
                    os.kill(proc.pid, signal.SIGTERM)
                    stop_at = time.monotonic()
            elif time.monotonic() - stop_at > 5:
                os.kill(proc.pid, signal.SIGKILL)
            time.sleep(0.2)
    except KeyboardInterrupt:
        os.kill(proc.pid, signal.SIGKILL)
        _, status, usage = os.wait4(proc.pid, 0)
        proc.returncode = os.waitstatus_to_exitcode(status)
        meta['stop_reason'] = 'interrupted'
    meta.update(exit_code=proc.returncode, elapsed_seconds=round(time.monotonic()-begin,3),
        peak_rss_bytes=usage.ru_maxrss, child_user_seconds=usage.ru_utime,
        child_system_seconds=usage.ru_stime, finished_at=datetime.datetime.now().astimezone().isoformat())
    save()
    print(json.dumps(meta, ensure_ascii=False), flush=True)
# SDL may handle SIGTERM and exit 0 even though the runner stopped on an error.
# Preserve the child's exit_code but fail the test invocation in that case.
if meta.get('stop_reason') == 'error_log' or (proc.returncode != 0 and meta.get('stop_reason') not in ['duration', 'operator']):
    raise SystemExit(1)
