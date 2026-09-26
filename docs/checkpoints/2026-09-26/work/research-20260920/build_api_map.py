from pathlib import Path
import collections, hashlib, html, json, re

ROOT=Path(__file__).resolve().parents[2]
R=ROOT/'work/research-20260920'
OUT=ROOT/'outputs'
data=json.loads((R/'probes/cn-surface.json').read_text())
tables=json.loads((R/'exe/validated-jump-tables.json').read_text())
header=(ROOT/'work/stage2/source/subprojects/libsys4/include/system4/ain.h').read_text()
types={int(v):n.removeprefix('AIN_').lower() for n,v in re.findall(r'\b(AIN_[A-Z_0-9]+)\s*=\s*(\d+)',header)}
def type_name(t):
    name=types.get(t['data'],str(t['data']))
    if 'subtype' in t:name+='<' + type_name(t['subtype']) + '>'
    if t['struct']>=0:name+='[type#'+str(t['struct'])+']'
    return name

rows=[]
studied={'Array':{16,56,57},'PartsEngine':{22,179,180,221,505}}
for lib in data['libraries']:
    counts=collections.Counter(f['name'] for f in lib['functions'])
    mapping={e['index']:e for e in tables['tables'].get(lib['name'],{}).get('entries',[])}
    for f in lib['functions']:
        entry=mapping.get(f['index'])
        rows.append({'library':lib['name'],'library_index':lib['index'],'index':f['index'],'name':f['name'],
                     'signature':type_name(f['return_type'])+' '+f['name']+'('+', '.join(type_name(t) for t in f['arguments'])+')',
                     'static_sites':f['static_call_sites'],'overloaded':counts[f['name']]>1,
                     'va':entry['va'] if entry else None,'rva':entry['rva'] if entry else None,
                     'file_offset':entry['file_offset'] if entry else None,
                     'studied':f['index'] in studied.get(lib['name'],set())})
result={'ain_sha256':hashlib.sha256((ROOT/'work/stage2/game/dohnadohna.ain').read_bytes()).hexdigest(),
        'dump_sha256':tables['sha256'],'address_kind':'Preferred VA for image base 0x400000; not live ASLR addresses.',
        'scope':'AIN declaration + static CALLHLL sites. 952 index mappings are candidates anchored by matching table sizes and selected branch validation; the full 952 API semantics are not verified. Existing dump provenance is not fully established.',
        'rows':rows}
(R/'probes/api-map.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
payload=json.dumps(result,ensure_ascii=False).replace('</','<\\/')
template='''<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>多娜多娜原生介面研究地圖</title>
<style>
:root{color-scheme:light;font-family:-apple-system,BlinkMacSystemFont,"PingFang TC",sans-serif;color:#1c3141;background:#f3f6f8}*{box-sizing:border-box}body{margin:0}main{max-width:1450px;padding:36px 32px;margin:auto}.eyebrow{color:#177b6a;letter-spacing:.12em;font-size:12px;font-weight:700}h1{font-size:30px;line-height:1.35}p{line-height:1.75}.stats{display:flex;gap:14px;flex-wrap:wrap;margin:24px 0}.stat{background:#fff;border:1px solid #dde5ea;border-radius:12px;padding:16px 24px;min-width:150px}.stat b{display:block;font-size:28px}.stat span{font-size:13px;color:#536977}.note{padding:16px 20px;background:#fff6db;border-left:4px solid #c39722;font-size:14px}.controls{display:flex;flex-wrap:wrap;align-items:center;gap:15px;background:white;padding:20px;border:1px solid #dde5ea;border-radius:12px;margin-top:24px;position:sticky;top:0;z-index:2}.field{display:grid;gap:5px;font-size:12px}input[type=search]{min-width:300px}input,select{font:inherit;padding:9px;border:1px solid #bdccd5;border-radius:5px}.toggle{font-size:13px}#count{margin:15px 0;color:#536977;font-size:13px}.wrap{overflow:auto;background:#fff;border:1px solid #d9e4e9;border-radius:10px}table{border-collapse:collapse;width:100%;font-size:13px}th,td{padding:12px 13px;border-bottom:1px solid #e4ebef;text-align:left;vertical-align:top}th{background:#e8f1f1;white-space:nowrap}tr:nth-child(even){background:#f8fafb}code{font-family:ui-monospace,SFMono-Regular,monospace;font-size:12px;overflow-wrap:anywhere}.tag{display:inline-block;white-space:nowrap;padding:3px 7px;border-radius:4px;font-size:11px;background:#e9edf0;color:#516171}.known{background:#def3ea;color:#165f49}a{color:#146b98}.footer{font-size:12px;color:#5b6e7b;margin-top:20px;overflow-wrap:anywhere}@media(max-width:650px){main{padding:22px 14px}h1{font-size:25px}.controls{position:static}input[type=search]{min-width:230px}}
</style><main><div class="eyebrow">LOCAL ENGINE RESEARCH · 2026-09-20</div><h1>多娜多娜原生介面研究地圖</h1><p>將真實中文 AIN 宣告、靜態呼叫位置與既有 EXE 傾印檔入口對照，供後續逆向與原生核心設計使用。</p>
<div class="stats"><div class="stat"><b>1,701</b><span>原生服務宣告</span></div><div class="stat"><b>952</b><span>EXE 分派入口對照</span></div><div class="stat"><b>35</b><span>同名但簽名不同的群組</span></div><div class="stat"><b>99</b><span>腳本實際出現的指令種類</span></div></div>
<p class="note"><strong>入口定位不等於語義驗證。</strong>952 格已核對檔案映射及可執行區；僅 8 個所列分支有局部人工判讀。呼叫數是靜態位置數，包含可能未執行的包裝函式。既有傾印檔與目前受保護 EXE 的完整核心一致性仍未證實。本頁不執行遊戲、不連線外部服務。</p>
<div class="controls"><label class="field">搜尋名稱／簽名／位址<input id="search" type="search" placeholder="例如 IsExist、ref_bool、0x644572"></label><label class="field">Library<select id="library"><option value="">全部 36 個程式庫</option></select></label><label class="toggle"><input id="mapped" type="checkbox"> 只看已有 EXE 入口</label><label class="toggle"><input id="overload" type="checkbox"> 只看同名重載</label></div><div id="count" role="status"></div>
<div class="wrap"><table><thead><tr><th>Library / index</th><th>AIN 宣告</th><th>靜態呼叫位置</th><th>EXE preferred VA</th><th>證據程度</th></tr></thead><tbody id="rows"></tbody></table></div>
<p><a href="xsystem4-原版逆向與原生路線研究-2026-09-20.html">閱讀完整研究報告</a></p><div class="footer" id="identity"></div></main>
<script id="dataset" type="application/json">__PAYLOAD__</script><script>
const d=JSON.parse(document.getElementById('dataset').textContent);
const q=document.getElementById('search'),lib=document.getElementById('library'),mapped=document.getElementById('mapped'),overload=document.getElementById('overload');
for(const name of [...new Set(d.rows.map(r=>r.library))]){const o=document.createElement('option');o.value=name;o.textContent=name;lib.append(o);}
function render(){const term=q.value.toLowerCase().trim();const shown=d.rows.filter(r=>(!lib.value||r.library===lib.value)&&(!mapped.checked||r.va)&&(!overload.checked||r.overloaded)&&(!term||[r.library,r.signature,r.va,r.rva,String(r.index)].join(' ').toLowerCase().includes(term)));
document.getElementById('count').textContent=`顯示 ${shown.length.toLocaleString()} / ${d.rows.length.toLocaleString()} 個宣告`;
const frag=document.createDocumentFragment();for(const r of shown){const tr=document.createElement('tr');for(const [i,text] of [`${r.library} #${r.library_index}:${r.index}`,r.signature,String(r.static_sites),r.va||'尚未定位',r.studied?'局部靜態判讀':r.va?'分派表定位':'僅 AIN 宣告'].entries()){const td=document.createElement('td');const el=document.createElement(i===1||i===3?'code':i===4?'span':'span');el.textContent=text;if(i===4)el.className='tag'+(r.studied?' known':'');if(i===3&&r.va)td.title=`RVA ${r.rva}; file offset ${r.file_offset}`;td.append(el);tr.append(td);}frag.append(tr);}document.getElementById('rows').replaceChildren(frag);}
for(const el of [q,lib,mapped,overload])el.addEventListener('input',render);
document.getElementById('identity').textContent=`AIN SHA256: ${d.ain_sha256} · EXE dump SHA256: ${d.dump_sha256} · preferred image base: 0x400000`;
render();
</script></html>'''
(OUT/'原生研究-API地圖-2026-09-20.html').write_text(template.replace('__PAYLOAD__',payload))
assert len(rows)==1701 and sum(r['va'] is not None for r in rows)==952 and sum(r['studied'] for r in rows)==8
print('Built API map: 1701 declarations, 952 mapped, 8 partially studied.')
