#!/usr/bin/env python3
"""Bounded, unattended GUI run. Isolated home/saves; stops on an error pattern or time limit.

usage: run-gui-bounded.py <binary> <source_dir> <game_dir> <run_dir> <seconds> [engine args...]

source_dir is the cwd (fonts/ and shaders/ are read from it). Existing XSYS4_*/XSYSTEM4_*
variables are dropped so a run is reproducible; use the RUN_* variables instead:
  RUN_HOLD_KEYS=13          hold Return (passes the notice screen)
  RUN_FRAMEBUFFER_SHOTS=1   engine framebuffer PNG every 2 s into <run_dir>/framebuffer/
  RUN_AUTO_CLICK=1200        engine auto-click every N ms at RUN_AUTO_CLICK_X/Y (default 640,400)
  RUN_AUTO_CLICK_SEQ="ms,x,y;..."  click at these absolute times instead (XSYS4_AUTO_CLICK_SEQ)
  RUN_CLICK_TRACE=1         log each v14 click's target ("S2 click target=...", XSYS4_STAGE2_TRACE)
  RUN_SHOTS=start,step,count  framebuffer PNG schedule in ms (default 2000,2000,40;
                            XSYS4_SCREENSHOT_SCHEDULE; needs RUN_FRAMEBUFFER_SHOTS=1)
  RUN_WINDOW_SHOTS=15,35,55 capture only the game window at these seconds into <run_dir>/window-NNs.png
                            (needs find-window next to this script, built from find-window.swift,
                            and Screen Recording permission for the calling process)
  RUN_SAVE_SEED=<dir>       copy the files of <dir> into <run_dir>/saves before starting (the seed
                            itself is never written; refused inside XS4_SRC or XS4_MASTER_GAME)
  RUN_TRACE_SAVE=1          one "SAVE ..." log line per SerializeStruct family call (XSYS4_TRACE_SAVE)
  RUN_STRING_CHARSET=sjis   force the String character rule (XSYS4_STRING_CHARSET; A/B runs)
  RUN_RANDOM_SEED=<n>       XSYS4_RANDOM_SEED: the k-th time-seeded call (Math.SetSeedByCurrentTime,
                            Array.Shuffle with seed < 0, the MT start-up seed) uses n + k instead of
                            the clock. The seeds are repeatable; a route repeats only when the game
                            makes those calls, and draws from Math.Rand/RandF, in the same order
                            (frame timing can change that). Never set by default.
  RUN_TRACE_OUTPUTLINE=1    log every system.OutputLine text as a NOTICE (XSYS4_TRACE_OUTPUTLINE=1;
                            e.g. the battle action order "Id:Speed(boost:N)" lines)
"""
import sys, os, subprocess, signal, time, json, hashlib, datetime, shutil
from pathlib import Path

binary, source, game, run, seconds = (Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]),
                                      Path(sys.argv[4]), int(sys.argv[5]))
extra = sys.argv[6:]
run.mkdir(parents=True, exist_ok=False)
(run / 'home').mkdir(); (run / 'saves').mkdir()
seed = os.environ.get('RUN_SAVE_SEED')
if seed:
    seed_real = os.path.realpath(seed)
    for guard in ('XS4_SRC', 'XS4_MASTER_GAME'):
        root = os.environ.get(guard)
        if root and (seed_real + '/').startswith(os.path.realpath(root) + '/'):
            sys.exit(f'RUN_SAVE_SEED is inside {guard}; refused')
    for f in sorted(Path(seed_real).iterdir()):
        if f.is_file():
            shutil.copy2(f, run / 'saves' / f.name)
env = {k: v for k, v in os.environ.items() if not k.startswith(('XSYS4_', 'XSYSTEM4_', 'RUN_'))}
env.update(XSYSTEM4_HOME=str(run / 'home'), XSYS4_STAGE2_PERF='1')
if os.environ.get('RUN_HOLD_KEYS'):
    env['XSYS4_HOLD_KEYS'] = os.environ['RUN_HOLD_KEYS']
if os.environ.get('RUN_AUTO_CLICK'):
    env['XSYS4_AUTO_CLICK'] = os.environ['RUN_AUTO_CLICK']
    env['XSYS4_AUTO_CLICK_X'] = os.environ.get('RUN_AUTO_CLICK_X', '640')
    env['XSYS4_AUTO_CLICK_Y'] = os.environ.get('RUN_AUTO_CLICK_Y', '400')
if os.environ.get('RUN_AUTO_CLICK_SEQ'):
    env['XSYS4_AUTO_CLICK_SEQ'] = os.environ['RUN_AUTO_CLICK_SEQ']
    for k in ('XSYS4_AUTO_CLICK', 'XSYS4_AUTO_CLICK_X', 'XSYS4_AUTO_CLICK_Y'):
        env.pop(k, None)
if os.environ.get('RUN_CLICK_TRACE'):
    env['XSYS4_STAGE2_TRACE'] = '1'
if os.environ.get('RUN_SHOTS'):
    env['XSYS4_SCREENSHOT_SCHEDULE'] = os.environ['RUN_SHOTS']
if os.environ.get('RUN_TRACE_SAVE'):
    env['XSYS4_TRACE_SAVE'] = '1'
if os.environ.get('RUN_STRING_CHARSET'):
    env['XSYS4_STRING_CHARSET'] = os.environ['RUN_STRING_CHARSET']
if os.environ.get('RUN_RANDOM_SEED'):
    env['XSYS4_RANDOM_SEED'] = os.environ['RUN_RANDOM_SEED']
if os.environ.get('RUN_TRACE_OUTPUTLINE'):
    env['XSYS4_TRACE_OUTPUTLINE'] = '1'
if os.environ.get('RUN_FRAMEBUFFER_SHOTS'):
    (run / 'framebuffer').mkdir()
    env['XSYS4_SCREENSHOT_DIR'] = str(run / 'framebuffer')
window_shots = sorted(int(x) for x in os.environ.get('RUN_WINDOW_SHOTS', '').split(',') if x.strip())

cmd = [str(binary), '--echo-message', '--save-folder', str(run / 'saves')] + extra + [str(game)]
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
manifest = lambda d: [dict(name=f.name, size=f.stat().st_size, sha256=sha(f), mtime=f.stat().st_mtime)
                      for f in sorted(Path(d).iterdir()) if f.is_file()]
meta = dict(started_at=datetime.datetime.now().astimezone().isoformat(), command=cmd, cwd=str(source),
            binary_sha256=sha(binary), ain_sha256=sha(game / 'dohnadohna.ain'),
            env={k: v for k, v in env.items() if k.startswith(('XSYS4_', 'XSYSTEM4_'))},
            duration_limit_seconds=seconds, window_shots=[],
            save_seed=os.path.realpath(seed) if seed else None, saves_manifest_start=manifest(run / 'saves'))
PATTERNS = [b'error: addresssanitizer', b'runtime error:', b'system.error:', b'vm error',
            b'vm_call_timeout:', b'assertion failed', b'assert(', b'call stack overflow']
LOG_LIMIT = int(os.environ.get('RUN_LOG_LIMIT_MB', '300')) * 1024 * 1024


def capture_window(tag):
    """Capture only the game window. Needs find-window and capture-window (ScreenCaptureKit)
    built next to this script, and Screen Recording permission for the calling app."""
    here = Path(__file__).resolve().parent
    fw, cw = here / 'find-window', here / 'capture-window'
    if not fw.exists() or not cw.exists():
        return f'{tag}: tools missing'
    out = subprocess.run([str(fw), 'xsystem4'], capture_output=True, text=True).stdout
    rows = [r.split('\t') for r in out.splitlines() if r.strip()]
    area = lambda r: eval(r[4].replace('x', '*')) if len(r) >= 5 else 0
    rows = [r for r in rows if len(r) >= 5 and r[3] == 'layer=0' and area(r) > 0]
    if not rows:
        return f'{tag}: no window'
    rows.sort(key=lambda r: -area(r))
    path = run / f'window-{tag}.png'
    res = subprocess.run([str(cw), rows[0][0], str(path)], capture_output=True, text=True)
    return f'{tag}: id={rows[0][0]} size={rows[0][4]} rc={res.returncode} {res.stdout.strip()[:160]}'



with (run / 'engine.log').open('wb') as log:
    proc = subprocess.Popen(cmd, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT,
                            start_new_session=True)
    begin = time.monotonic(); stop_at = None; off = 0; carry = b''
    while True:
        pid, status, usage = os.wait4(proc.pid, os.WNOHANG)
        if pid:
            rc = os.waitstatus_to_exitcode(status)
            break
        el = time.monotonic() - begin
        if stop_at is None:
            while window_shots and el >= window_shots[0]:
                t = window_shots.pop(0)
                meta['window_shots'].append(capture_window(f'{t:03d}s'))
            with (run / 'engine.log').open('rb') as r:
                r.seek(off); new = r.read(); off = r.tell()
            tail = (carry + new).lower(); carry = tail[-512:]
            hit = [p.decode() for p in PATTERNS if p in tail]
            big = off > LOG_LIMIT
            if hit or big or el >= seconds or (run / 'STOP').exists():
                meta['stop_reason'] = ('error_log' if hit else 'log_limit' if big
                                       else 'operator' if (run / 'STOP').exists() else 'duration')
                meta['matched'] = hit
                if hit:
                    meta['window_shots'].append(capture_window('on-error'))
                os.kill(proc.pid, signal.SIGTERM); stop_at = time.monotonic()
        elif time.monotonic() - stop_at > 5:
            os.kill(proc.pid, signal.SIGKILL)
        time.sleep(0.2)
if 'stop_reason' not in meta:
    # the engine exited by itself (e.g. right after a VM error) before a poll saw the log
    with (run / 'engine.log').open('rb') as r:
        r.seek(off); tail = (carry + r.read()).lower()
    meta['matched'] = [p.decode() for p in PATTERNS if p in tail]
    meta['stop_reason'] = 'error_log' if meta['matched'] else 'exited'
meta.update(exit_code=rc, elapsed_seconds=round(time.monotonic() - begin, 3), peak_rss_bytes=usage.ru_maxrss,
            finished_at=datetime.datetime.now().astimezone().isoformat(),
            saves_manifest_end=manifest(run / 'saves'))
(run / 'run.json').write_text(json.dumps(meta, ensure_ascii=False, indent=2))
print(json.dumps({k: meta[k] for k in ['stop_reason', 'matched', 'exit_code', 'elapsed_seconds',
                                       'peak_rss_bytes', 'window_shots']}, ensure_ascii=False))
