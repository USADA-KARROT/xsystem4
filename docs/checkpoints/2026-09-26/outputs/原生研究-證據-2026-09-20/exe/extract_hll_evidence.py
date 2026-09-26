#!/usr/bin/env python3
"""Reproduce bounded HLL dispatch/RTTI evidence from the already-existing local dump.
Does not execute or modify the input PE. Addresses apply only to the specified hash.
"""
import hashlib, json, pathlib, struct, subprocess
root=pathlib.Path(__file__).resolve().parent
p=json.loads((root/'dump-pe-evidence.json').read_text())
d=pathlib.Path(p['source']).read_bytes()
assert hashlib.sha256(d).hexdigest()=='211ce63e0e229fa7d77b2af141149681ec37521e7497914473c739569aa101f0'
base=p['image_base']
def offset(va):
    for s in p['sections']:
        if s['rva']<=va-base<s['rva']+s['raw_size']:
            return s['file_offset']+va-base-s['rva']
    raise ValueError(hex(va))
def va(off):
    for s in p['sections']:
        if s['file_offset']<=off<s['file_offset']+s['raw_size']:
            return off-s['file_offset']+s['rva']+base
def refs(addr):
    start=0
    while True:
        off=d.find(struct.pack('<I',addr),start)
        if off<0: return
        start=off+1
        yield off
def u32(va): return struct.unpack_from('<I',d,offset(va))[0]
tables={}
validated_tables={}
for addr,count,label in [(0x644f18,84,'Array'),(0x589714,868,'PartsEngine')]:
    entries=struct.unpack_from('<'+'I'*count,d,offset(addr))
    tables[label]={str(i):hex(v) for i,v in enumerate(entries)}
    (root/(label+'-jump-table.json')).write_text(json.dumps(tables[label],indent=2)+'\n')
    validated=[]
    for i,target in enumerate(entries):
        for si,s in enumerate(p['sections']):
            if s['rva']<=target-base<s['rva']+s['raw_size']:
                file_offset=offset(target)
                validated.append(dict(index=i,va=hex(target),rva=hex(target-base),file_offset=file_offset,section_index=si,section_name=s['name'],section_characteristics=hex(s['characteristics']),section_executable=bool(s['characteristics']&0x20000000),file_backed=True,first_8_bytes=d[file_offset:file_offset+8].hex()))
                break
        else:
            validated.append(dict(index=i,va=hex(target),file_backed=False,section_executable=False))
    validated_tables[label]=dict(table_va=hex(addr),table_file_offset=offset(addr),entry_count=count,entries=validated)
(root/'validated-jump-tables.json').write_text(json.dumps(dict(source=p['source'],sha256=p['sha256'],image_base=hex(base),note='Executable section membership is checked for every target; this does not by itself prove each target has correct semantic correspondence to an AIN declaration.',tables=validated_tables),indent=2)+'\n')
array_rtti=[]
for r in refs(0x87bb34):
    # Type descriptor pointer must be at +12 in an MSVC complete object locator.
    col=va(r-12)
    fields=struct.unpack_from('<5I',d,r-12)
    if fields[0]!=0 or fields[1] not in (0,0x20) or fields[2]!=0: continue
    for vr in refs(col):
        vt=va(vr+4)
        methods=struct.unpack_from('<25I',d,vr+4)
        array_rtti.append(dict(type_descriptor_va=hex(0x87bb34),rtti_name='.?AVCArrayPage@sys43vm@@',complete_object_locator_va=hex(col),object_offset=fields[1],vtable_va=hex(vt),first_25_words=[hex(x) for x in methods],note='For object_offset=0, words beyond the primary vtable include the next COL and secondary vtable.'))
evidence=dict(source=p['source'],sha256=p['sha256'],address_kind='preferred VA (image base 0x400000)',
    registry=[dict(name='Array',string_va='0x7cedbc',branch_va='0x494747',dispatcher_va='0x644300',table_va='0x644f18',entries=84),dict(name='PartsEngine',string_va='0x7cef20',branch_va='0x4948f7',dispatcher_va='0x57b900',table_va='0x589714',entries=868)],
    selected_ain_index_labels={'Array':{'15':'Erase(index,length)','16':'Erase(predicate)','17':'Erase(source array)','56':'IsExist(value)','57':'IsExist(predicate)'},'PartsEngine':{'22':'GetClickNumber','179':'Parts_SetClickable','180':'Parts_GetPartsClickable','221':'GetButtonCGName(int) -> string','505':'GetMessageWindowCGName(int) -> string'}},
    label_provenance='AIN HLL0 indices independently provided by main research task; names are not present as standalone strings in this dump.',
    array_rtti=array_rtti)
(root/'hll-native-evidence.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n')
ranges=[('hll-array-name',0x494747,0x49475f),('hll-parts-name',0x4948f7,0x49490f),('name-compare',0x41bfa0,0x41bff5),('array-dispatch',0x644300,0x644342),('parts-dispatch',0x57b900,0x57b972),('array-isexist-value',0x644bd5,0x644c04),('array-isexist-predicate',0x64473a,0x644769),('array-find-value-wrapper',0x648a80,0x648ab8),('array-find-value',0x648b00,0x648b94),('array-find-predicate-wrapper',0x648c40,0x648c78),('array-find-predicate',0x648cc0,0x648d73),('array-find-loop',0x646bb0,0x646c04),('array-value-comparator-factory',0x646020,0x6461df),('array-erase-overloads',0x644519,0x6445e1),('array-integer-value-comparison',0x64bcd0,0x64bd05),('array-page-erase-one',0x67f750,0x67f8a8),('parts-getclick',0x57bd7e,0x57bdb6),('parts-clickability',0x57e142,0x57e19c),('parts-clickability-implementation',0x58f830,0x58f953),('parts-button-cgname',0x57eafb,0x57eb80),('parts-message-window-cgname',0x582d88,0x582e20)]
ranges.append(('vm-string-return-construction',0x657cc0,0x657d3b))
for label,start,end in ranges:
    cmd=['objdump','-D','--x86-asm-syntax=intel','--start-address='+hex(start),'--stop-address='+hex(end),p['source']]
    (root/(label+'.asm.txt')).write_text('Command: '+repr(cmd)+'\n'+subprocess.check_output(cmd,text=True))
print('Saved',len(ranges),'bounded excerpts, 2 jump tables and HLL/RTTI metadata.')
