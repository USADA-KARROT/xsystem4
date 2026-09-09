#!/usr/bin/env python3
"""Parse aggregate present telemetry; MSG has line-order bounds, not a timestamp."""
import argparse,json,re
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('runs',nargs='+',type=Path,help='Run directory with engine.log and run.json')
parser.add_argument('--output',type=Path,default=Path(__file__).with_name('perf-windows.json'))
args=parser.parse_args()
all_runs=[]
for run in args.runs:
    metadata=json.loads((run/'run.json').read_text())
    lines=(run/'engine.log').read_text(errors='replace').splitlines()
    windows=[];messages=[];pending=[];latest_msg=None;last_perf=None
    for lineno,line in enumerate(lines,1):
        msg=re.match(r'^MSG (\d+):',line)
        if msg:
            latest_msg=int(msg.group(1))
            event={'line':lineno,'message_index':latest_msg,
                   'previous_perf_end_ms':last_perf['t_ms'] if last_perf else None,
                   'next_perf_end_ms':None,'exact_message_time_ms':None}
            messages.append(event);pending.append(event)
        if 'STAGE2_PERF ' not in line:continue
        record={}
        for key,value in re.findall(r'(\w+)=([^\s]+)',line.split('STAGE2_PERF ',1)[1]):
            try:record[key]=int(value,16) if value.lower().startswith('0x') else float(value) if '.' in value else int(value)
            except ValueError:record[key]=value
        for event in pending:event['next_perf_end_ms']=record['t_ms']
        record.update(line=lineno,window_start_ms_approx=record['t_ms']-record['window_ms'],
            latest_msg_observed_by_line=latest_msg,messages_within_log_window=[e['message_index'] for e in pending],
            phase_by_log_order='contains_MSG' if pending else 'after_first_MSG' if latest_msg is not None else 'before_first_MSG',
            p95_samples_complete=record['samples']==record['presents'])
        flags=record['window_flags']
        record['shown']=bool(flags&4);record['minimized']=bool(flags&64)
        record['input_focus']=bool(flags&512);record['mouse_focus']=bool(flags&1024)
        windows.append(record);last_perf=record;pending=[]
    def summarize(rows):
        duration=sum(r['window_ms'] for r in rows)
        return {'windows':len(rows),'covered_ms':duration,'presents':sum(r['presents'] for r in rows),
            'weighted_avg_present_fps':sum(r['presents'] for r in rows)*1000/duration if duration else None,
            'per_window_p95_interval_ms_range':[min(r['p95_interval_ms'] for r in rows),max(r['p95_interval_ms'] for r in rows)] if rows else None,
            'max_interval_ms':max((r['max_interval_ms'] for r in rows),default=None),
            'max_swap_ms':max((r['max_swap_ms'] for r in rows),default=None),
            'windows_with_interval_over_1000ms':sum(r['max_interval_ms']>1000 for r in rows),
            'p95_incomplete_windows':sum(not r['p95_samples_complete'] for r in rows)}
    phases={key:summarize([r for r in windows if r['phase_by_log_order']==key]) for key in ['before_first_MSG','contains_MSG','after_first_MSG']}
    result={'run':str(run),'metadata':metadata,'summary':summarize(windows),'phases_by_log_order':phases,
            'messages':messages,'windows':windows,'last_reported_t_ms':last_perf['t_ms'] if last_perf else None,
            'warning':'FPS counts completed present calls and may include repeated visual content. MSG has no timestamp; bounds only reflect surrounding PERF log records. Aggregate-window p95 cannot be merged into a whole-run p95. Final period without a later present/report is not measured.'}
    all_runs.append(result)
    print(run.name,json.dumps(result['summary']))
args.output.write_text(json.dumps({'runs':all_runs},ensure_ascii=False,indent=2)+'\n')
