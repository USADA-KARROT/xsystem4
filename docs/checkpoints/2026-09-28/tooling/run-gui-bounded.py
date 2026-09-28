#!/usr/bin/env python3
"""Bounded, unattended GUI run. Isolated home/saves, stops on error pattern or time.
usage: run-gui-bounded.py <binary> <source_dir(cwd, has fonts/shaders)> <game_dir> <run_dir> <seconds> [extra engine args...]
env passthrough: any XSYS4_* / XSYSTEM4_* already set are dropped; set HOLD via RUN_HOLD_KEYS, e.g. RUN_HOLD_KEYS=13
"""
import sys, os, subprocess, signal, time, json, hashlib, datetime
from pathlib import Path
binary, source, game, run, seconds = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]), int(sys.argv[5])
extra = sys.argv[6:]
run.mkdir(parents=True, exist_ok=False); (run/'home').mkdir(); (run/'saves').mkdir()
env = {k:v for k,v in os.environ.items() if not k.startswith(('XSYS4_','XSYSTEM4_','RUN_'))}
env.update(XSYSTEM4_HOME=str(run/'home'), XSYS4_STAGE2_PERF='1')
if os.environ.get('RUN_HOLD_KEYS'): env['XSYS4_HOLD_KEYS'] = os.environ['RUN_HOLD_KEYS']
cmd = [str(binary), '--echo-message', '--save-folder', str(run/'saves')] + extra + [str(game)]
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
meta = dict(started_at=datetime.datetime.now().astimezone().isoformat(), command=cmd, cwd=str(source),
            binary_sha256=sha(binary), ain_sha256=sha(game/'dohnadohna.ain'), env={k:v for k,v in env.items() if k.startswith(('XSYS4_','XSYSTEM4_'))},
            duration_limit_seconds=seconds)
PATTERNS = [b'error: addresssanitizer', b'runtime error:', b'system.error:', b'vm error', b'vm_call_timeout:', b'assertion failed', b'assert(', b'personality.jaf']
with (run/'engine.log').open('wb') as log:
    proc = subprocess.Popen(cmd, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    begin = time.monotonic(); stop_at=None; off=0; carry=b''
    while True:
        pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
        if pid: rc = os.waitstatus_to_exitcode(status); break
        el = time.monotonic()-begin
        if stop_at is None:
            with (run/'engine.log').open('rb') as r:
                r.seek(off); new=r.read(); off=r.tell()
            tail=(carry+new).lower(); carry=tail[-512:]
            hit=[p.decode() for p in PATTERNS if p in tail]
            if hit or el>=seconds or (run/'STOP').exists():
                meta['stop_reason']='error_log' if hit else ('operator' if (run/'STOP').exists() else 'duration'); meta['matched']=hit
                os.kill(proc.pid, signal.SIGTERM); stop_at=time.monotonic()
        elif time.monotonic()-stop_at>5: os.kill(proc.pid, signal.SIGKILL)
        time.sleep(0.2)
meta.update(exit_code=rc, elapsed_seconds=round(time.monotonic()-begin,3), peak_rss_bytes=usage.ru_maxrss, finished_at=datetime.datetime.now().astimezone().isoformat())
(run/'run.json').write_text(json.dumps(meta, ensure_ascii=False, indent=2))
print(json.dumps({k:meta[k] for k in ['stop_reason','matched','exit_code','elapsed_seconds','peak_rss_bytes']}, ensure_ascii=False))
