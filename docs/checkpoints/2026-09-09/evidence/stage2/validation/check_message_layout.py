from pathlib import Path
import json,subprocess,hashlib,re
base=Path(__file__).resolve().parent.parent;source=base/'source';here=base/'validation'
s=(source/'src/hll/pe_v14_activity.c').read_text()
def fun(sig):
 start=s.index(sig);i=s.index('{',start);depth=1;end=i+1
 while depth:
  if s[end]=='{':depth+=1
  elif s[end]=='}':depth-=1
  end+=1
 return s[start:end]
def bs(x):return '"'+''.join('\\x%02x'%b for b in x.encode('gb18030'))+'"'
consts='\n'.join(re.search(r'static const char '+k+r'\[\].*?;',s).group(0) for k in ['SJIS_CG_MEI','GBK_CG_MEI','SJIS_PARTS_TYPE','GBK_PARTS_TYPE'])
helper=s[s.index('enum pactex_message_key {'):s.index('/* Apply pactex properties')]
prefix=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "system4/afa.h"
#include "system4/ex.h"
#include "system4/string.h"
#include "parts.h"
#include "../source/src/parts/parts_internal.h"
struct capture { int area[4], origin,face,size,color[3],edge_color[3],space[2]; float weight,edge; struct string *cg; };
static struct capture got;
static struct parts fixture;
struct parts *parts_get(int n) { assert(n==7);return &fixture; }
void PE_SetMessageWindowCGName(int n,struct string *name) { assert(n==7);assert(!got.cg);got.cg=string_dup(name); }
void PE_SetMessageWindowTextArea(int n,int x,int y,int w,int h) { assert(n==7);got.area[0]=x;got.area[1]=y;got.area[2]=w;got.area[3]=h; }
void PE_SetMessageWindowTextOriginPosMode(int n,int mode) { assert(n==7);got.origin=mode; }
void PE_SetMessageWindowTextFont(int n,int type,int size,int r,int g,int b,float weight,int er,int eg,int eb,float edge) {
 assert(n==7);got.face=type;got.size=size;got.color[0]=r;got.color[1]=g;got.color[2]=b;got.weight=weight;got.edge_color[0]=er;got.edge_color[1]=eg;got.edge_color[2]=eb;got.edge=edge;
}
void PE_SetMessageWindowTextSpace(int n,int letter,int line) { assert(n==7);got.space[0]=letter;got.space[1]=line; }
'''
rows=[('main',[130,62,940,181],25,255,0.,0,-1,10,'測試／信息窗口／Ａ'),('mainB',[130,62,940,181],25,0,0.,0,-1,10,'測試／信息窗口／Ｂ'),('event',[360,600,940,181],25,255,1.5,64,-4,8,'信息窗口／事件'),('plot',[80,90,1280,720],20,255,1.0,0,-2,5,'系統／仮／プロット')]
# Plot's full values come from the actual metadata dump; ensure expectations independently supplied.
expected='static const struct { const char *name;int area[4],size,color;float edge;int edge_color,letter,line;const char *cg; } expected[] = {\n'
for name,area,size,color,edge,edgecolor,letter,line,cg in rows:
 expected+='{"'+name+'",{'+','.join(map(str,area))+'},'+','.join(map(str,[size,color,edge,edgecolor,letter,line]))+','+bs(cg)+'},\n'
expected+='};\n'
tests=r'''
static int checked;
static void walk(struct ex_tree *n,int index) {
 if(n->is_leaf)return;
 const char *type=pactex_get_string(n,GBK_PARTS_TYPE);
 if(type && pactex_apply_message_window(n,type,7)) {
  assert(++checked==1);
  assert(fixture.message_window);
  for(int j=0;j<4;j++)assert(got.area[j]==expected[index].area[j]);
  assert(got.origin==1 && got.face==0 && got.size==expected[index].size);
  for(int j=0;j<3;j++)assert(got.color[j]==expected[index].color);
  assert(got.weight==0 && got.edge==expected[index].edge);
  for(int j=0;j<3;j++)assert(got.edge_color[j]==expected[index].edge_color);
  assert(got.space[0]==expected[index].letter && got.space[1]==expected[index].line);
  assert(got.cg && !strcmp(got.cg->text,expected[index].cg));
  struct capture before=got;
  free_string(got.cg);got.cg=NULL;
  for(unsigned i=0;i<n->nr_children;i++) {
   struct ex_tree *c=&n->children[i];
   if(!c->is_leaf || !c->name)continue;
   for(unsigned k=0;k<sizeof(pactex_message_keys)/sizeof(pactex_message_keys[0]);k++) {
    if(!strcmp(c->name->text,pactex_message_keys[k].gbk)) { free_string(c->name);c->name=cstr_to_string(pactex_message_keys[k].sjis);break; }
   }
   if(!strcmp(c->name->text,GBK_CG_MEI)) { free_string(c->name);c->name=cstr_to_string(SJIS_CG_MEI); }
  }
  assert(pactex_apply_message_window(n,"\x83\x81\x83\x62\x83\x5a\x81\x5b\x83\x57\x83\x45\x83\x42\x83\x93\x83\x68\x83\x45",7));
  assert(!memcmp(got.area,before.area,sizeof(got.area)) && got.size==before.size && got.edge==before.edge);
  assert(got.cg && !strcmp(got.cg->text,expected[index].cg));
  free_string(got.cg);memset(&got,0,sizeof(got));
 }
 for(unsigned i=0;i<n->nr_children;i++)walk(&n->children[i],index);
}
int main(int argc,char **argv) {
 assert(argc==2);int error=0;unsigned empty_before=EMPTY_STRING.ref;
 struct afa_archive *a=afa_open(argv[1],0,&error);assert(a);
 for(int index=0;index<4;index++) {
  char name[200];snprintf(name,sizeof(name),"Scene\\10_Adv\\Main\\AdvMessageWindow_%s.pactex",expected[index].name);
  struct archive_data *d=archive_get_by_name(&a->ar,name);assert(d);
  struct ex *e=ex_read(d->data,d->size);assert(e);checked=0;memset(&fixture,0,sizeof(fixture));
  assert(!pactex_apply_message_window(NULL,NULL,7));
  assert(!pactex_apply_message_window(NULL,"button",7));
  assert(!fixture.message_window);
  for(unsigned i=0;i<e->nr_blocks;i++)if(e->blocks[i].val.type==EX_TREE)walk(e->blocks[i].val.tree,index);
  assert(checked==1);ex_free(e);archive_free_data(d);printf("%s: direct window CG, area, font, spacing; GB18030 and SJIS keys: PASS\n",expected[index].name);
 }
 archive_free(&a->ar);assert(EMPTY_STRING.ref==empty_before);return 0;
}
'''
code=prefix+consts+'\n'+fun('static bool pactex_name_contains(')+'\n'+fun('static const char *pactex_get_string(')+'\n'+helper+expected+tests
path=here/'message_layout_fixture.c';path.write_text(code)
old=json.loads((here/'message-window-result.json').read_text());cmd=old['compile_command'].copy();cmd[cmd.index(str(here/'message_window_fixture.c'))]=str(path);cmd[-1]=str(here/'message_layout_fixture')
r=subprocess.run(cmd,cwd=base/'normal-build',capture_output=True,text=True)
result={'compile_command':cmd,'compile_exit':r.returncode,'compile_stderr':r.stderr,'source_sha256':hashlib.sha256(helper.encode()).hexdigest()}
if r.returncode==0:
 archive='<USER_HOME>/xsystem4-dev/dohnadohna-mac-port/game-workcopy/多娜多娜 一起幹壞事吧/dohnadohnaPact.afa'
 run=subprocess.run([cmd[-1],archive],capture_output=True,text=True);result.update(test_exit=run.returncode,stdout=run.stdout,stderr=run.stderr)
(here/'message-layout-result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,ensure_ascii=False,indent=2))
