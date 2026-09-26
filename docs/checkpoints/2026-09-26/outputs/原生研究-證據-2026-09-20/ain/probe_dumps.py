#!/usr/bin/env python3
"""Read-only bounded AIN dump analysis. Writes only to this script's directory.
No game execution; disassembly syntax comes from existing local dumps.
"""
from pathlib import Path
import re,json,hashlib,collections
OUT=Path(__file__).resolve().parent
BASE=Path('<USER_HOME>/Claude/projects')
MAIN=[6374,20752,20761,27031,36081]
RELATED=[420,422,423,424,432,441,6365,6367,6371,6385,6391,7061,9196,20746,20748,20749,20750,20753,20758,20760,20762,25697,27033,27034,36080,36081]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def signature_key(s):
 # The chosen generic lambda source identifiers have no localized names.
 if '<lambda' in s:return s[s.index('@<lambda')+1:s.index('>(',s.index('@<lambda'))+1]
 return s.split('(')[0]
def load(variant):
 root=BASE/f'dohna-{variant}-dump';fs={}
 for line in (root/'functions.txt').read_text().splitlines():
  m=re.match(r'/\* 0x([0-9a-f]+) \*/\s*(.*)',line)
  if m:fs[int(m.group(1),16)]=m.group(2)
 bykey=collections.defaultdict(list)
 for i,s in fs.items():
  try:bykey[signature_key(s)].append(i)
  except ValueError:pass
 groups={};current=None;stack=[]
 for lineno,line in enumerate((root/'ain_code.txt').read_text().splitlines(),1):
  m=re.match(r'^FUNC (\d+)$',line)
  if m:
   f=int(m.group(1))
   if '<lambda' in fs.get(f,''):stack.append(current)
   else:stack=[]
   current=f;groups.setdefault(f,{'func_line':lineno,'instructions':[]});continue
  if line.startswith('ENDFUNC'):
   current=stack.pop() if stack else None;continue
  if current is not None and line.strip() and not line.startswith((';','EOF')):
   groups[current]['instructions'].append({'line':lineno,'text':line.strip()})
 libraries=[];lib=None
 for line in (root/'libraries.txt').read_text().splitlines():
  m=re.match(r'^--- (.*) ---$',line)
  if m:lib={'name':m.group(1),'declarations':[]};libraries.append(lib)
  elif lib is not None and line.strip().endswith(';'):lib['declarations'].append(line)
 stats={'variant':variant,'dump_dir':str(root),'functions':len(fs),'structures':len(re.findall(r'^// \d+$',(root/'structures.txt').read_text(),re.M)),'library_count':len(libraries),'hll_declarations':sum(len(x['declarations']) for x in libraries),'hashes':{n:sha(root/n) for n in ['functions.txt','libraries.txt','structures.txt','delegates.txt','ain_code.txt']},'libraries':[{'name':x['name'],'count':len(x['declarations'])} for x in libraries]}
 return fs,bykey,groups,libraries,stats
cn,cmap,cg,cl,cs=load('cn');ja,jmap,jg,jl,js=load('jast')
evidence={'scope':'Existing decoded dump metadata and bounded technical bytecode only; not original-EXE semantics. Function-local nested lambdas excluded from parent body then parent resumed. Address operands are not relocation-normalized.','variants':[cs,js],'libraries_identical':cs['hashes']['libraries.txt']==js['hashes']['libraries.txt'],'functions':[],'hll_boundary':{}}
for i in dict.fromkeys(MAIN+RELATED):
 if i not in cg:continue
 key=signature_key(cn[i]); matches=jmap[key];item={'cn_id':i,'cn_signature':cn[i],'deep_read':i in MAIN,'cn_func_line':cg[i]['func_line'],'jast_candidates':[],'cn_instructions':cg[i]['instructions'] if i in MAIN or i in [420,422,423,424,432,441,6371,20750,27034] else None,'cn_instruction_count':len(cg[i]['instructions'])}
 # Opcode order compares shape only, not referenced functions, literals, or branch locations.
 cops=[x['text'].split()[0] for x in cg[i]['instructions'] if not x['text'].endswith(':')]
 for ji in matches:
  if ji not in jg:continue
  jops=[x['text'].split()[0] for x in jg[ji]['instructions'] if not x['text'].endswith(':')]
  item['jast_candidates'].append({'id':ji,'signature':ja[ji],'func_line':jg[ji]['func_line'],'instruction_count':len(jg[ji]['instructions']),'opcode_shape_equal':cops==jops})
 evidence['functions'].append(item)
for lib in cl:
 if lib['name'] in ['system','Array','Delegate','PartsEngine','InputDevice','Input']:
  keywords={'system':['Resume','GetTime','Peek','Sleep'],'Array':['Erase','Free(','PushBack','Numof','IsExist'],'Delegate':['Empty','Numof','Erase'],'PartsEngine':['KeyDown','GetMessage','PopMessage','ReleaseMessage','UpdateComponent','UpdateInputState','GetActiveParts','GetClickNumber']}.get(lib['name'],['Key','Mouse'])
  evidence['hll_boundary'][lib['name']]=[x for x in lib['declarations'] if any(k in x for k in keywords)]
(OUT/'dump-evidence.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n')
types={}
for variant in ['cn','jast']:
 root=BASE/f'dohna-{variant}-dump';text=(root/'structures.txt').read_text();found={}
 for typ in ['CASTimer','CASTimerImp','CASClick','CASJoyClick','task::detail::CObserver','Motion::ExecuterCollection']:
  m=re.search(r'// (\d+)\nstruct '+re.escape(typ)+r' \{.*?\n\};',text,re.S)
  if m:found[typ]={'id':int(m.group(1)),'declaration':m.group(0)}
 types[variant]=found
 evidence.setdefault('observer_delegates',{})[variant]=[l for l in (root/'delegates.txt').read_text().splitlines() if 'DG_Observer' in l]
evidence['selected_types']=types
surface=OUT.parent/'probes/cn-surface.json'
if surface.exists():
 raw=json.loads(surface.read_text())
 evidence['actual_cn_metadata']={k:raw[k] for k in ['ain_version','code_bytes','functions','structures','globals','delegates']}
 evidence['actual_cn_selected_metadata']=raw['selected_functions']
 evidence['actual_cn_probe']='../probes/cn-surface.json'
(OUT/'dump-evidence.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2)+'\n')
print(json.dumps({'variants':[{k:v for k,v in x.items() if k in ['variant','functions','structures','library_count','hll_declarations']} for x in [cs,js]],'libraries_identical':evidence['libraries_identical'],'selected_functions':len(evidence['functions']),'deep_functions':len(MAIN)},indent=2))
