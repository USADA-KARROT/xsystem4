"""Summarize a captured run without exposing inherited process environment."""
from pathlib import Path
import argparse, collections, hashlib, json, re

p = argparse.ArgumentParser()
p.add_argument('tag')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
run = root / 'runs' / a.tag
meta = json.loads((run / 'run.json').read_text())
log = (run / 'engine.log').read_text(errors='replace')
messages = [int(n) for n in re.findall(r'^MSG (\d+):', log, re.M)]
calls = collections.defaultdict(lambda: {'enters': 0, 'returns': 0, 'durations_ms': []})
for line in log.splitlines():
    match = re.search(r'STAGE2 (enter|return).*?\bf=(\d+)\b', line)
    if not match:
        continue
    info = calls[match[2]]
    info['enters' if match[1] == 'enter' else 'returns'] += 1
    duration = re.search(r'\belapsed=(\d+)', line)
    if duration:
        info['durations_ms'].append(int(duration[1]))
result = {
    'tag': a.tag,
    'finished': 'finished_at' in meta,
    'exit_code': meta.get('exit_code'),
    'elapsed_seconds': meta.get('elapsed_seconds'),
    'scripted_clicks': meta['environment'].get('XSYS4_AUTO_CLICK_SEQ'),
    'messages': messages,
    'message_count': len(messages),
    'sanitizer_errors': re.findall(r'^.*(?:ERROR: AddressSanitizer|runtime error:|SUMMARY:.*Sanitizer).*$', log, re.M),
    'double_free_warnings': re.findall(r'^.*(?:Double free|double.free).*$', log, re.M),
    'click_targets': [int(n) for n in re.findall(r'S2 click target=(\d+)', log)],
    'calls': dict(calls),
    'source_patch_matches_record': hashlib.sha256((run/'source.patch').read_bytes()).hexdigest() == meta['source_diff_sha256'],
}
(run / 'summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2))
print(json.dumps(result, ensure_ascii=False, indent=2))
