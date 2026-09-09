
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
static const char SJIS_CG_MEI[]      = "\x82\x62\x82\x66\x96\xbc";
static const char GBK_CG_MEI[]      = "\xa3\xc3\xa3\xc7\xc3\xfb";
static const char SJIS_PARTS_TYPE[]  = "\x83\x70\x81\x5b\x83\x63\x83\x5e\x83\x43\x83\x76";
static const char GBK_PARTS_TYPE[]  = "\xb2\xbf\xbc\xfe\xa5\xbf\xa5\xa4\xa5\xd7";
static bool pactex_name_contains(struct ex_tree *node, const char *pattern)
{
	if (!node->name) return false;
	return strstr(node->name->text, pattern) != NULL;
}
static const char *pactex_get_string(struct ex_tree *node, const char *pattern)
{
	if (node->is_leaf) return NULL;
	for (unsigned i = 0; i < node->nr_children; i++) {
		struct ex_tree *c = &node->children[i];
		if (!c->is_leaf) continue;
		if (!pactex_name_contains(c, pattern)) continue;
		if (c->leaf.value.type == EX_STRING && c->leaf.value.s &&
		    c->leaf.value.s->text[0])
			return c->leaf.value.s->text;
		break;
	}
	return NULL;
}
enum pactex_message_key {
	PACTEX_MW_AREA,
	PACTEX_MW_ORIGIN,
	PACTEX_MW_FACE,
	PACTEX_MW_SIZE,
	PACTEX_MW_COLOR,
	PACTEX_MW_WEIGHT,
	PACTEX_MW_EDGE,
	PACTEX_MW_EDGE_COLOR,
	PACTEX_MW_CHAR_SPACE,
	PACTEX_MW_LINE_SPACE,
};
static const struct { const char *sjis, *gbk; } pactex_message_keys[] = {
	[PACTEX_MW_AREA] = {"\x83\x65\x83\x4c\x83\x58\x83\x67\x83\x47\x83\x8a\x83\x41", "\xce\xc4\xb1\xbe\xa5\xa8\xa5\xea\xa5\xa2"}, /* テキストエリア / 文本エリア */
	[PACTEX_MW_ORIGIN] = {"\x83\x65\x83\x4c\x83\x58\x83\x67\x88\xca\x92\x75", "\xce\xc4\xb1\xbe\xce\xbb\xd6\xc3"}, /* テキスト位置 / 文本位置 */
	[PACTEX_MW_FACE] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x83\x5e\x83\x43\x83\x76", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xbf\xa5\xa4\xa5\xd7"}, /* フォントタイプ / フォントタイプ */
	[PACTEX_MW_SIZE] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x83\x54\x83\x43\x83\x59", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xa5\xb5\xa5\xa4\xa5\xba"}, /* フォントサイズ / フォントサイズ */
	[PACTEX_MW_COLOR] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x90\x46", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xc9\xab"}, /* フォント色 / フォント色 */
	[PACTEX_MW_WEIGHT] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x91\xbe\x82\xb3", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xcc\xab\xa4\xb5"}, /* フォント太さ / フォント太さ */
	[PACTEX_MW_EDGE] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x89\x8f\x8e\xe6\x82\xe8", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xbf\x46\xc8\xa1\xa4\xea"}, /* フォント縁取り / フォント縁取り */
	[PACTEX_MW_EDGE_COLOR] = {"\x83\x74\x83\x48\x83\x93\x83\x67\x89\x8f\x8e\xe6\x82\xe8\x90\x46", "\xa5\xd5\xa5\xa9\xa5\xf3\xa5\xc8\xbf\x46\xc8\xa1\xa4\xea\xc9\xab"}, /* フォント縁取り色 / フォント縁取り色 */
	[PACTEX_MW_CHAR_SPACE] = {"\x95\xb6\x8e\x9a\x8a\xd4\x8a\x75", "\xce\xc4\xd7\xd6\xe9\x67\xb8\xf4"}, /* 文字間隔 / 文字間隔 */
	[PACTEX_MW_LINE_SPACE] = {"\x8d\x73\x8a\xd4\x8a\x75", "\xd0\xd0\xe9\x67\xb8\xf4"}, /* 行間隔 / 行間隔 */
};

static struct ex_value *pactex_message_value(struct ex_tree *node, enum pactex_message_key key)
{
	if (!node || node->is_leaf) return NULL;
	for (unsigned i = 0; i < node->nr_children; i++) {
		struct ex_tree *c = &node->children[i];
		if (c->is_leaf && c->name &&
				(!strcmp(c->name->text, pactex_message_keys[key].sjis) ||
				 !strcmp(c->name->text, pactex_message_keys[key].gbk)))
			return &c->leaf.value;
	}
	return NULL;
}

static float pactex_message_number(struct ex_tree *node, enum pactex_message_key key, float fallback)
{
	struct ex_value *v = pactex_message_value(node, key);
	if (!v) return fallback;
	if (v->type == EX_FLOAT) return v->f;
	if (v->type == EX_INT) return v->i;
	return fallback;
}

static int pactex_message_item(struct ex_tree *node, enum pactex_message_key key,
		unsigned index, int fallback)
{
	struct ex_value *v = pactex_message_value(node, key);
	if (!v || v->type != EX_LIST || !v->list || index >= v->list->nr_items)
		return fallback;
	struct ex_value *item = &v->list->items[index].value;
	if (item->type == EX_FLOAT) return item->f;
	if (item->type == EX_INT) return item->i;
	return fallback;
}

static bool pactex_apply_message_window(struct ex_tree *type_info, const char *ptype, int parts_no)
{
	if (!ptype || (strcmp(ptype, "\x83\x81\x83\x62\x83\x5a\x81\x5b\x83\x57\x83\x45\x83\x42\x83\x93\x83\x68\x83\x45") &&
			strcmp(ptype, "\xd0\xc5\xcf\xa2\xb4\xb0\xbf\xda")))
		return false;
	struct parts *parts = parts_get(parts_no);
	parts->message_window = true;
	const char *cg = pactex_get_string(type_info, SJIS_CG_MEI);
	if (!cg) cg = pactex_get_string(type_info, GBK_CG_MEI);
	if (cg) {
		struct string *name = cstr_to_string(cg);
		PE_SetMessageWindowCGName(parts_no, name);
		free_string(name);
	}
	PE_SetMessageWindowTextArea(parts_no,
		pactex_message_item(type_info, PACTEX_MW_AREA, 0, 0),
		pactex_message_item(type_info, PACTEX_MW_AREA, 1, 0),
		pactex_message_item(type_info, PACTEX_MW_AREA, 2, 0),
		pactex_message_item(type_info, PACTEX_MW_AREA, 3, 0));
	PE_SetMessageWindowTextOriginPosMode(parts_no,
		pactex_message_number(type_info, PACTEX_MW_ORIGIN, 1));
	PE_SetMessageWindowTextFont(parts_no,
		pactex_message_number(type_info, PACTEX_MW_FACE, 0),
		pactex_message_number(type_info, PACTEX_MW_SIZE, 16),
		pactex_message_item(type_info, PACTEX_MW_COLOR, 0, 255),
		pactex_message_item(type_info, PACTEX_MW_COLOR, 1, 255),
		pactex_message_item(type_info, PACTEX_MW_COLOR, 2, 255),
		pactex_message_number(type_info, PACTEX_MW_WEIGHT, 0),
		pactex_message_item(type_info, PACTEX_MW_EDGE_COLOR, 0, 0),
		pactex_message_item(type_info, PACTEX_MW_EDGE_COLOR, 1, 0),
		pactex_message_item(type_info, PACTEX_MW_EDGE_COLOR, 2, 0),
		pactex_message_number(type_info, PACTEX_MW_EDGE, 0));
	PE_SetMessageWindowTextSpace(parts_no,
		pactex_message_number(type_info, PACTEX_MW_CHAR_SPACE, 0),
		pactex_message_number(type_info, PACTEX_MW_LINE_SPACE, 0));
	return true;
}

static const struct { const char *name;int area[4],size,color;float edge;int edge_color,letter,line;const char *cg; } expected[] = {
{"main",{130,62,940,181},25,255,0.0,0,-1,10,"\x9c\x79\xd4\x87\xa3\xaf\xd0\xc5\xcf\xa2\xb4\xb0\xbf\xda\xa3\xaf\xa3\xc1"},
{"mainB",{130,62,940,181},25,0,0.0,0,-1,10,"\x9c\x79\xd4\x87\xa3\xaf\xd0\xc5\xcf\xa2\xb4\xb0\xbf\xda\xa3\xaf\xa3\xc2"},
{"event",{360,600,940,181},25,255,1.5,64,-4,8,"\xd0\xc5\xcf\xa2\xb4\xb0\xbf\xda\xa3\xaf\xca\xc2\xbc\xfe"},
{"plot",{80,90,1280,720},20,255,1.0,0,-2,5,"\xcf\xb5\xbd\x79\xa3\xaf\x81\xa2\xa3\xaf\xa5\xd7\xa5\xed\xa5\xc3\xa5\xc8"},
};

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
