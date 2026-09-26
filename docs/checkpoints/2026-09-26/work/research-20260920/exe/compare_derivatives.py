#!/usr/bin/env python3
"""Compare only properly mapped, file-backed PE bytes; never conflate RVA and file offset."""
import hashlib,json,pathlib
root=pathlib.Path(__file__).resolve().parent
a=json.loads((root/'dump-pe-evidence.json').read_text())
b=json.loads((root/'final6-pe-evidence.json').read_text())
c=json.loads((root/'pe-evidence.json').read_text())
files={p['source']:pathlib.Path(p['source']).read_bytes() for p in (a,b,c)}
def data(p,va,n):
    d=files[p['source']];rva=va-p['image_base']
    for s in p['sections']:
        if s['rva']<=rva and rva+n<=s['rva']+s['raw_size']:
            off=s['file_offset']+rva-s['rva'];return d[off:off+n]
    return None
ranges=[('Array dispatcher',0x644300,0xc18),('Array jump table',0x644f18,84*4),('PartsEngine dispatcher prologue',0x57b900,0x72),('PartsEngine jump table',0x589714,868*4),('Array search loop',0x646bb0,0x54),('Array erase one',0x67f750,0x158),('PartsEngine clickability implementation',0x58f830,0x123),('PartsEngine GetButtonCGName',0x57eafb,0x3d),('PartsEngine GetMessageWindowCGName',0x582d88,0x3d)]
e=[]
for label,va,n in ranges:
    x=data(a,va,n);y=data(b,va,n)
    e.append(dict(label=label,va=hex(va),bytes=n,equal=x==y if x is not None and y is not None else None,dump_file_backed=x is not None,final6_file_backed=y is not None,dump_range_sha256=hashlib.sha256(x).hexdigest() if x is not None else None,final6_range_sha256=hashlib.sha256(y).hexdigest() if y is not None else None))
sections=[]
for i,(x,y) in enumerate(zip(b['sections'][:7],c['sections'][:7])):
    bx=files[b['source']][x['file_offset']:x['file_offset']+x['raw_size']]
    cy=files[c['source']][y['file_offset']:y['file_offset']+y['raw_size']]
    sections.append(dict(section_index=i,same_rva=x['rva']==y['rva'],same_raw_size=x['raw_size']==y['raw_size'],bytes_equal=bx==cy,final6_sha256=hashlib.sha256(bx).hexdigest(),protected_sha256=hashlib.sha256(cy).hexdigest()))
result=dict(dump=dict(source=a['source'],sha256=a['sha256']),final6=dict(source=b['source'],sha256=b['sha256']),protected=dict(source=c['source'],sha256=c['sha256']),ranges=e,final6_vs_protected_first_seven_raw_sections=sections,limitation='Final6 retains packed sections. Unbacked ranges cannot be compared. Equal protected raw bytes do not prove the SCY plaintext is the output of this exact file.')
(root/'dump-final6-comparison.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print('Compared mapped ranges:',sum(x['equal'] is not None for x in e),'/',len(e))
print('Equal final6/protected raw sections:',sum(x['bytes_equal'] for x in sections),'/',len(sections))
