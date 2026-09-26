from pathlib import Path
from markdown_it import MarkdownIt
from urllib.parse import unquote
import hashlib, html, json, re, shutil

ROOT = Path(__file__).resolve().parents[2]
RESEARCH = ROOT / 'work/research-20260920'
OUT = ROOT / 'outputs'
NAME = 'xsystem4-原版逆向與原生路線研究-2026-09-20'
EVIDENCE = OUT / '原生研究-證據-2026-09-20'
EVIDENCE.mkdir(exist_ok=True)
for folder in ('exe', 'ain', 'archive', 'probes'):
    for source in sorted((RESEARCH / folder).rglob('*')):
        if not source.is_file() or source.suffix not in {'.md', '.json', '.py', '.c', '.txt'}:
            continue
        if '.dSYM' in str(source) or source.name == 'sanitizer-verification.json':
            continue
        target = EVIDENCE / source.relative_to(RESEARCH)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

readme = '''# 原生路線研究：證據與重跑工具

日期：2026-09-20。本目錄為本機靜態研究的固定快照，不含完整 EXE、AIN、遊戲資源或玩家存檔。所有程式探針只讀取輸入檔案；不執行遊戲。

- [完整報告](../xsystem4-原版逆向與原生路線研究-2026-09-20.html)
- [可搜尋 API 地圖](../原生研究-API地圖-2026-09-20.html)
- [EXE／原生分派與局部語義](exe/exe-findings.md)
- [AIN／閉包、引用、輸入與 timer 契約](ain/ain-findings.md)
- [本機歷史材料與視覺證據審查](archive/archive-findings.md)
- [現有 Array 綁定缺口獨立審查](archive/binding-review.md)
- [資產身份清單](archive/inventory.json)
- [完整 API 資料](probes/api-map.json)
- [O2 與 ASan/UBSan 驗證](probes/verified/verification.json)

## 主要證據

`exe/` 保留三個 PE 身份、952 格分派表、22 份有界機器碼摘錄及生成工具。可執行區與檔案映射全部驗證；8 個分支局部人工判讀，不代表 952 項 API 都已還原。SCY dump 與現用受保護 EXE 的完整版本一致性未證實。

`ain/` 保留 CN／JAST dump 對照、從原 AIN 解碼的指定函式及探針。JAST 原始 AIN 本輪未重新取得。`probes/` 為全 AIN 結構掃描，包含所有 HLL 宣告與靜態 CALLHLL 位置數，並非執行覆蓋率。

`archive/` 保留現有研究檔案身份、舊測試證據限制與本次原始碼審查。絕對路徑供原電腦追溯；部分舊目錄可能已不存在，相關限制已在子報告標示。

## 重跑 AIN 結構驗證

在本任務根目錄執行；需要本機既有 source 與兩份已建置 libsys4.a。工具不包含這些遊戲／依賴檔案。

```sh
python3 outputs/原生研究-證據-2026-09-20/probes/reproduce_surface.py \\
  --source work/stage2/source \\
  --lib work/stage2/optimized-build/subprojects/libsys4/libsys4.a \\
  --asan-lib work/stage1/wip-asan-build/subprojects/libsys4/libsys4.a \\
  --ain work/stage2/game/dohnadohna.ain \\
  --out work/research-20260920/recheck
```

本輪已成功執行同一工具，normal／ASan 結果完全相同，所有非 sentinel 函式地址都在指令邊界。ASan/UBSan 驗證範圍限於讀取／解碼；LeakSanitizer 關閉。既有名稱 junk warning 與 sentinel 診斷保存在 verification.json，不能据此宣稱新 VM 或遊戲已通過測試。

EXE 的 `inspect_pe.py` 可用命令列指定來源；其餘 EXE／AIN 工具保留本機來源路徑或特定 hash／位址，重跑前須閱讀程式。若在別台電腦重跑，要改為自己的合法檔案位置，不能將本次 VA 套用到不同 hash 的 EXE。工具會寫分析結果到自身目錄，建議複製到新的工作目錄後執行。

## 檔案完整性與報告驗證

`manifest-sha256.json` 記錄本資料包每個檔案的 bytes 與 SHA-256（不含 manifest 自身）。報告／API 地圖均可離線開啟；沒有外部 JavaScript 或資料請求。報告本地連結、資料統計及互動篩選程式以離線檢查驗證；本輪未有可用瀏覽器連線，未完成真實瀏覽器視覺驗收。

本輪沒有改動現有引擎、遊戲或存檔，也沒有向 GitHub 推送。本資料包代表研究成果，不是可玩版本。
'''
(EVIDENCE / 'README.md').write_text(readme.replace('据此', '據此'))

source = OUT / (NAME + '.md')
text = source.read_text()
for old, new in {'两':'兩','条件':'條件','格式与':'格式與','旧工程':'舊工程','本輪没有':'本輪沒有','原Windows':'原 Windows'}.items():
    text = text.replace(old, new)
source.write_text(text)
md = MarkdownIt('commonmark', {'html': False}).enable('table')
body = md.render(text)
toc = []
def heading(match):
    anchor = 'section-' + str(len(toc) + 1)
    label = re.sub('<[^>]+>', '', match.group(1))
    toc.append(f'<a href="#{anchor}">{label}</a>')
    return f'<h2 id="{anchor}">{match.group(1)}</h2>'
body = re.sub(r'<h2>(.*?)</h2>', heading, body)
style = '''
:root{color-scheme:light;--ink:#1c3141;--muted:#536977;--line:#dde5ea;--accent:#177b6a}
*{box-sizing:border-box}body{margin:0;background:#f3f6f8;color:var(--ink);font:16px/1.85 -apple-system,BlinkMacSystemFont,"PingFang TC",sans-serif}
.layout{display:grid;grid-template-columns:235px minmax(0,1060px);gap:32px;max-width:1380px;padding:36px 25px;margin:auto}nav{position:sticky;top:28px;align-self:start;font-size:13px}nav b{color:var(--accent)}nav a{display:block;text-decoration:none;color:var(--muted);padding:8px 0;border-bottom:1px solid var(--line)}main{min-width:0;background:#fff;border:1px solid var(--line);border-radius:14px;padding:35px 40px}.kicker{color:var(--accent);font-size:12px;letter-spacing:.12em;font-weight:700}h1{font-size:30px;line-height:1.4}h2{font-size:23px;border-top:2px solid var(--line);padding-top:22px;margin-top:44px;scroll-margin-top:20px}h3{font-size:19px}a{color:#146b98;text-underline-offset:3px}strong{color:#165f49}table{display:block;overflow:auto;border-collapse:collapse;width:100%;margin:24px 0;font-size:13px;line-height:1.7}th,td{border:1px solid var(--line);padding:12px;min-width:110px;vertical-align:top;text-align:left}th{background:#e8f1f1}tr:nth-child(even){background:#f8fafb}code{font:12px/1.7 ui-monospace,SFMono-Regular,Menlo,monospace;overflow-wrap:anywhere;background:#f0f4f6;border-radius:4px;padding:2px 4px}li{margin:10px 0}.foot{border-top:1px solid var(--line);font-size:12px;color:var(--muted);margin-top:35px;padding-top:15px}
@media(max-width:1050px){.layout{grid-template-columns:1fr;max-width:940px}nav{position:static}nav a{display:inline-block;margin-right:15px}main{padding:28px}}@media(max-width:600px){.layout{padding:12px}main{padding:20px 16px}h1{font-size:25px}body{font-size:15px}h2{font-size:21px}}@media print{nav{display:none}.layout{display:block;padding:0}main{border:0;padding:0}body{background:white}h2{break-after:avoid}tr{break-inside:avoid}}
'''
document = '<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>' + html.escape(NAME) + '</title><style>' + style + '</style><div class="layout"><nav><b>原版研究與原生路線</b>' + ''.join(toc) + '</nav><main><div class="kicker">LOCAL ENGINE RESEARCH · 2026-09-20</div>' + body + '<div class="foot">靜態可行性研究｜本地 API 地圖與證據可從文中連結開啟。未實作新引擎。</div></main></div></html>'
(OUT / (NAME + '.html')).write_text(document)

checked = 0
for document_path, rendered in [(OUT / (NAME + '.html'), document), (EVIDENCE / 'README.md', md.render(readme)), (OUT / '原生研究-API地圖-2026-09-20.html', (OUT / '原生研究-API地圖-2026-09-20.html').read_text())]:
    for target in re.findall(r'href="([^"]+)"', rendered):
        if target.startswith(('http://', 'https://', '#')):
            continue
        assert (document_path.parent / unquote(target)).is_file(), (document_path, target)
        checked += 1
for file in EVIDENCE.rglob('*.json'):
    json.loads(file.read_text())
manifest = {str(p.relative_to(EVIDENCE)): {'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(EVIDENCE.rglob('*')) if p.is_file() and p.name != 'manifest-sha256.json'}
(EVIDENCE / 'manifest-sha256.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
print(json.dumps({'evidence_files': len(manifest), 'evidence_bytes': sum(x['bytes'] for x in manifest.values()), 'local_links_checked': checked, 'report': str(OUT / (NAME + '.html'))}, ensure_ascii=False, indent=2))
