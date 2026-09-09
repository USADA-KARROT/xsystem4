from pathlib import Path
p=Path('work/stage2/source/src/hll/pe_v14_activity.c')
s=p.read_text()
keys=[('AREA','テキストエリア','文本エリア'),('ORIGIN','テキスト位置','文本位置'),('FACE','フォントタイプ','フォントタイプ'),('SIZE','フォントサイズ','フォントサイズ'),('COLOR','フォント色','フォント色'),('WEIGHT','フォント太さ','フォント太さ'),('EDGE','フォント縁取り','フォント縁取り'),('EDGE_COLOR','フォント縁取り色','フォント縁取り色'),('CHAR_SPACE','文字間隔','文字間隔'),('LINE_SPACE','行間隔','行間隔')]
def cbytes(text,encoding):return '"'+''.join('\\x%02x'%b for b in text.encode(encoding))+'"'
helper='''/* Message-window values are direct leaves of their own type-info branch.
 * Keep raw SJIS and the actual CN/GB18030 spellings separate; nested ruby and
 * key-wait settings must not be read as the window's text or CG states. */
enum pactex_message_key {
'''+''.join('\tPACTEX_MW_'+name+',\n' for name,jp,cn in keys)+'''};
static const struct { const char *sjis, *gbk; } pactex_message_keys[] = {
'''+''.join('\t[PACTEX_MW_'+name+'] = {'+cbytes(jp,'cp932')+', '+cbytes(cn,'gb18030')+'}, /* '+jp+' / '+cn+' */\n' for name,jp,cn in keys)+'''};

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
	if (!ptype || (strcmp(ptype, '''+cbytes('メッセージウィンドウ','cp932')+''') &&
			strcmp(ptype, '''+cbytes('信息窗口','gb18030')+'''))))
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

'''
anchor='/* Apply pactex properties (position, show, alpha, CG) to a parts entry.'
assert s.count(anchor)==1
s=s.replace(anchor,helper+anchor)
anchor2='\t/* --- Handle パネル (Panel) type: solid color rectangle --- */'
assert s.count(anchor2)==1
s=s.replace(anchor2,'\tif (pactex_apply_message_window(type_info, ptype, parts_no))\n\t\treturn;\n\n'+anchor2)
p.write_text(s)
