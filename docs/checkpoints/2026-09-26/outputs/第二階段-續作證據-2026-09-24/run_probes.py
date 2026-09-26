"""Run bounded real-bytecode cases; expected failures remain failures in the report."""
from pathlib import Path
import hashlib, json, os, subprocess

here = Path(__file__).resolve().parent
root = here.parents[1]
ain = root / 'work/stage2/game/dohnadohna.ain'
cases = []
for variant in ['optimized', 'asan']:
    for mode, expected in [('heap-reuse', 0), ('assignment', 0), ('array-reinit', 0), ('metadata', 0), ('observer', 0), ('timer', 0), ('reentrancy', 0), ('click', 0)]:
        command = [str(here / ('runtime-probe-' + variant)), str(ain), mode]
        run = subprocess.run(command, capture_output=True, text=True, errors='replace', timeout=30,
            env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0:abort_on_error=1',
                     UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))
        prefix = here / (variant + '-' + mode)
        prefix.with_suffix('.out').write_text(run.stdout)
        prefix.with_suffix('.err').write_text(run.stderr)
        record = dict(variant=variant, case=mode, command=command, exit=run.returncode,
                      expected_exit=expected, reproduced_expected_result=run.returncode == expected,
                      lifecycle_passed=(run.returncode == 0),
                      metadata_type_passed=('METADATA TYPE PASS' in run.stdout),
                      sanitizer_error=('ERROR: AddressSanitizer' in run.stderr or 'runtime error:' in run.stderr))
        cases.append(record)
        print(json.dumps(record))
result = dict(ain_sha256=hashlib.sha256(ain.read_bytes()).hexdigest(), cases=cases,
              stage2_fully_passed=all(c['lifecycle_passed'] and not c['sanitizer_error'] for c in cases),
              scope='Original observer Execute and Join callback bytecode; seeded Join capture, not complete Join entry. '
                    'Production VM/heap/page/FFI. Timer native substitution disabled in harness only. '
                    'No GUI/game entry, module startup, save/load, or playable/FPS claim. '
                    'LeakSanitizer disabled; live VM slots measured explicitly.')
(here / 'results.json').write_text(json.dumps(result, indent=2) + '\n')
if not all(c['reproduced_expected_result'] and not c['sanitizer_error'] for c in cases):
    raise SystemExit(1)
