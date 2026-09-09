from pathlib import Path
import subprocess
here=Path(__file__).resolve().parent
archive='<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/game-workcopy/多娜多娜 一起幹壞事吧/dohnadohnaPact.afa'
output=['Read-only libsys4 EX metadata from '+archive, 'Encoding: GB18030/GBK raw names, shown as UTF-8; no layout setters changed.','']
for name in ['AdvMessageWindow_main','AdvMessageWindow_mainB','AdvMessageWindow_event','AdvMessageWindow_plot']:
 path='Scene\\10_Adv\\Main\\'+name+'.pactex'
 raw=subprocess.check_output([str(here/'pact_probe'),archive,path]);text=raw.decode('gb18030')
 (here/(name+'.raw')).write_bytes(raw)
 for line in text.splitlines():
  if ' type=' not in line: output.append(line);continue
  field=line.split(' type=')[0].split('/')[-1]
  if field=='文本':continue
  if any(x in field for x in ['テキスト','文字','文本','行間','行间','行間隔','フォント','ルビ','字體','字型','字间','ピッチ','間隔','スペース','サイズ','座標','パーツタイプ','部件タイプ','原點','ＣＧ名']):
   output.append(line)
   output.append('  field GBK hex='+field.encode('gb18030').hex())
 output.append('')
(here.parent/'message-layout.log').write_text('\n'.join(output)+'\n')
