#!/usr/bin/env python3
"""Read-only PE32 evidence extraction. No third-party modules required."""
import collections, datetime, hashlib, json, math, pathlib, re, struct, sys

path = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
d = path.read_bytes()
def u16(o): return struct.unpack_from('<H', d, o)[0]
def u32(o): return struct.unpack_from('<I', d, o)[0]
def cstr(o):
    if o is None: return None
    return d[o:d.find(b'\0', o)].decode('ascii', 'replace')
pe = u32(0x3c); coff = pe+4; opt = coff+20
assert d[pe:pe+4] == b'PE\0\0' and u16(opt) == 0x10b
base = u32(opt+28)
sections=[]
for i in range(u16(coff+2)):
    s = opt+u16(coff+16)+i*40
    n = d[s:s+8].rstrip(b'\0').decode('ascii','replace')
    vs, va, sz, off = struct.unpack_from('<IIII', d, s+8)
    raw = d[off:off+sz]; cnt=collections.Counter(raw)
    entropy=-sum((x/len(raw))*math.log2(x/len(raw)) for x in cnt.values()) if raw else 0
    sections.append(dict(name=n,rva=va,virtual_size=vs,raw_size=sz,file_offset=off,
                         characteristics=u32(s+36),entropy=round(entropy,4)))
def fo(rva):
    for s in sections:
        if s['rva'] <= rva < s['rva']+s['raw_size']:
            return s['file_offset']+rva-s['rva']
    return rva if rva<u32(opt+60) else None
def loc(offset):
    for s in sections:
        if s['file_offset']<=offset<s['file_offset']+s['raw_size']:
            rva=s['rva']+offset-s['file_offset']
            return dict(file_offset=offset,rva=rva,va=base+rva,section=s['name'])
    return dict(file_offset=offset,rva=None,va=None,section=None)
dirs=[dict(index=i,rva=u32(opt+96+8*i),size=u32(opt+100+8*i)) for i in range(min(16,u32(opt+92)))]
imports=[]
io=fo(dirs[1]['rva']) if dirs[1]['rva'] else None
if io:
    while any(d[io:io+20]):
        oft,ts,chain,nrva,ft=struct.unpack_from('<IIIII',d,io)
        names=[]; thunk=fo(oft or ft); j=0
        while thunk is not None and u32(thunk+4*j):
            v=u32(thunk+4*j)
            names.append(dict(name=None if v&0x80000000 else cstr(fo(v)+2),ordinal=v&0xffff if v&0x80000000 else None,iat_va=base+ft+j*4))
            j+=1
        imports.append(dict(dll=cstr(fo(nrva)),functions=names)); io+=20
debug=[]
db=fo(dirs[6]['rva']) if dirs[6]['rva'] else None
if db:
    for i in range(dirs[6]['size']//28):
        x=db+28*i; typ,sz,ar,ptr=struct.unpack_from('<IIII',d,x+12)
        rec=dict(type=typ,size=sz,rva=ar,file_offset=ptr)
        if d[ptr:ptr+4]==b'RSDS': rec.update(codeview='RSDS',guid_hex=d[ptr+4:ptr+20].hex(),age=u32(ptr+20),pdb=cstr(ptr+24))
        debug.append(rec)
targets=['Array','PartsEngine','System','system','Sys43VM','Delegate','IsExist','Erase','GetCGName','Wait','WaitForClick','EndWaitForClick','Update','Input','SetClickCancel','ProcessMessage','PeekMessageA','GetMessageA','timeGetTime','QueryPerformanceCounter']
strings=[]
for name in targets:
    needle=name.encode()+b'\0'; start=0
    while True:
        off=d.find(needle,start)
        if off<0: break
        start=off+1
        if off and 32<=d[off-1]<=126: continue
        record=dict(name=name,**loc(off),references=[])
        if record['va'] is not None:
            raw=struct.pack('<I',record['va']); st=0
            while True:
                pos=d.find(raw,st)
                if pos<0: break
                st=pos+1
                record['references'].append(dict(**loc(pos),context_hex=d[max(pos-12,0):pos+24].hex()))
        strings.append(record)
rtti=[]
for m in re.finditer(rb'\.\?A[UV][ -~]{1,200}?@@\x00',d):
    name=m.group()[:-1].decode('ascii')
    if any(x.lower() in name.lower() for x in ['array','parts','system','click','wait','message','vm','ain','delegate']):
        rtti.append(dict(name=name,**loc(m.start())))
result=dict(source=str(path),size=len(d),sha256=hashlib.sha256(d).hexdigest(),machine=hex(u16(coff)),image_base=base,entry_rva=u32(opt+16),link_timestamp_raw=u32(coff+4),link_timestamp_utc=datetime.datetime.fromtimestamp(u32(coff+4),datetime.timezone.utc).isoformat(),coff_symbol_count=u32(coff+12),sections=sections,directories=dirs,imports=imports,debug=debug,target_strings=strings,target_rtti=rtti,overlay_bytes=len(d)-max(s['file_offset']+s['raw_size'] for s in sections))
out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({k:result[k] for k in ['size','sha256','image_base','entry_rva','link_timestamp_utc','coff_symbol_count','sections','debug','overlay_bytes']},indent=2))
print('IMPORT DLLS',[(x['dll'],len(x['functions'])) for x in imports])
for x in strings:
    print(x['name'],hex(x['va'] or 0),'refs',[(r['section'],hex(r['va'] or 0)) for r in x['references']])
print('TARGET RTTI',len(rtti))
