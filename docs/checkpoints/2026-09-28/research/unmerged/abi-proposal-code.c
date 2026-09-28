以 worktree HEAD 763f5bd 為基底的完整 patch（對 763f5bd 的 src+include 快照已通過 git apply --check）。檔案：<scratchpad>/overload2/g-cif/cif-fix-on-763f5bd.diff。另有 a8d92df 版：同目錄 cif-fix.diff。以下為全文。

diff --git a/include/parts.h b/include/parts.h
--- a/include/parts.h
+++ b/include/parts.h
@@ -315,6 +315,8 @@ int PE_GetLayoutBoxLayoutType(int parts_no);
 void PE_SetLayoutBoxReturn(int parts_no, bool return_flag, int return_size);
 bool PE_IsLayoutBoxReturn(int parts_no);
 int PE_GetLayoutBoxReturnSize(int parts_no);
+void PE_SetLayoutBoxReturnF(int parts_no, bool return_flag, float return_size);
+float PE_GetLayoutBoxReturnSizeF(int parts_no);
 void PE_SetLayoutBoxAlign(int parts_no, int align);

diff --git a/src/ffi.c b/src/ffi.c   (link_static_library, 763f5bd 1164 行附近)
 				if (!strcmp(lib->name, "Array")) {
 					extern void *array_select_function(const struct ain_hll_function *f, void *fallback);
 					funcptr = array_select_function(&ainlib->functions[i], funcptr);
+				} else if (funcptr) {
+					extern void *hll_shape_select_function(const char *lib,
+						const struct ain_hll_function *f, void *dflt);
+					funcptr = hll_shape_select_function(lib->name, &ainlib->functions[i], funcptr);
 				}
 				if (funcptr)
 					link_static_library_function(&dst[i], &ainlib->functions[i], funcptr);

diff --git a/src/hll/Array.c b/src/hll/Array.c
@@ Array_SYSTEMONLY_GetStructPageList（取代空的 void 版）
+/*
+ * array<?> SYSTEMONLY_GetStructPageList(ref array<hll_param> self)
+ *
+ * Not a debug no-op: AFL_GameSave_StructSave/StructLoad/Serialize/Deserialize
+ * (fno 6172/6173/6176/6177) pass the result straight to
+ * system.SerializeStruct / DeserializeStruct(string, array<int>, bool).
+ * Native (dump_SCY.exe, Array case 83 @0x644ecd -> 0x64a2d0): walk self, fetch
+ * each element's struct page (error "failed to get struct page of array
+ * element" when missing), push its handle into a std::vector<int>, return that
+ * as the result array.
+ *
+ * Here the handle is the struct's heap slot. The result is array<int>: it does
+ * not own the structs (no heap_ref), matching the AIN parameter type of
+ * SerializeStruct. It is a fresh slot with ref 1; ffi's variable_fini on the
+ * SerializeStruct argument releases it.
+ *
+ * CN call sites use arg3 = 65539 (array<wrap<iwrap<ISerializable>>>), i.e.
+ * 2-slot interface elements [page, vtable offset]; only the page slot is taken.
+ */
+static int Array_SYSTEMONLY_GetStructPageList(struct page **array)
 {
+	struct page *src = (array && *array) ? *array : NULL;
+	int stride = array_elem_is_2slot() ? 2 : 1;
+	int n = (src && src->type == ARRAY_PAGE) ? src->nr_vars / stride : 0;
+
+	heap_gc_inhibit();
+	struct page *out = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n);
+	out->array.rank = 1;
+	for (int i = 0; i < n; i++) {
+		int s = src->values[i * stride].i;
+		struct page *sp = (s > 0 && heap_index_valid(s) && heap[s].type == VM_PAGE)
+			? heap[s].page : NULL;
+		if (!sp || sp->type != STRUCT_PAGE) {
+			WARNING("Array.SYSTEMONLY_GetStructPageList: element %d (slot %d) is not a struct", i, s);
+			s = -1;
+		}
+		out->values[i].i = s;
+	}
+	int slot = heap_alloc_page(out);
+	heap_gc_allow();
+	return slot;
 }
@@ 放在 Array_Copy 之後、Array_Realloc_Fill 之前
+/*
+ * Duplicate, v14 shape: void Duplicate(ref array<hll_param> self,
+ * wrap<array<hll_param>> src). src is the array's heap slot (CIF sint32);
+ * the legacy Array_Duplicate(struct page **, struct page **) dereferenced it
+ * and crashed (UBSan Array.c:2232 at a8d92df, probe signal 11).
+ *
+ * Native (Array case 3 @0x6443d8 -> 0x647550): resize self to src's length
+ * (CArrayPage vtable +0x4c), then run the Copy helper 0x648210(self, 0, src,
+ * 0, n), i.e. the same path as Copy(self, src). array_copy_range() mirrors
+ * that helper, and array_elem_store() gives strings and value structs their
+ * own copy while ref elements (arg3 >= 0x10000) share the object.
+ */
+static void Array_Duplicate_Wrap(struct page **self, int src_slot)
+{
+	if (!self)
+		return;
+	struct page *src = array_wrap_page(src_slot);
+	if (!src || src == *self)
+		return;
+	int n = array_call_numof(src);
+	heap_gc_inhibit();
+	// An empty self takes src's element type, otherwise array_copy() on a
+	// default AIN_ARRAY_INT page would share string slots.
+	if (!*self || (*self)->nr_vars == 0) {
+		if (*self)
+			free_page(*self);
+		*self = alloc_page(ARRAY_PAGE, src->a_type, 0);
+		(*self)->array = src->array;
+	}
+	Array_Realloc(self, n * array_call_stride());
+	array_copy_range(self, 0, src, 0, n);
+	heap_gc_allow();
+}
@@ array_select_function（在 First 分支之後）
+	if (!strcmp(f->name, "Duplicate")) {
+		if (f->nr_arguments == 2 && f->return_type.data == AIN_VOID
+		    && array_arg_is_wrap_array(f, 1))
+			return (void *)Array_Duplicate_Wrap;
+		return fallback;
+	}

diff --git a/src/hll/hll.h b/src/hll/hll.h   (加在 wrap_set_string 之後)
+/*
+ * Write a string to a 1-slot wrap<string> argument.
+ *
+ * ffi passes wrap<string> as ffi_type_sint32 (link_static_library_function):
+ * the C side gets the heap slot, never a pointer. In CN v14 bytecode the slot
+ * is the VM_STRING slot of the target variable itself (".LOCALREF text", or
+ * "X_REF 1" on a string member, see AFL_TextFile_ReadAll / gamesave read),
+ * so the string object is replaced in place and every holder of that slot
+ * sees the new text. A v14 wrap box (STRUCT_PAGE, index == -1, values[0] =
+ * inner string slot) is also accepted.
+ *
+ * Takes ownership of s. Returns false (and frees s) when the slot cannot be
+ * written.
+ */
+static inline bool wrap_slot_set_string(int slot, struct string *s)
+{
+	if (slot > 0 && (size_t)slot < heap_size) {
+		if (heap[slot].type == VM_STRING) {
+			if (heap[slot].s)
+				free_string(heap[slot].s);
+			heap[slot].s = s;
+			return true;
+		}
+		struct page *box = heap[slot].type == VM_PAGE ? heap[slot].page : NULL;
+		if (box && box->type == STRUCT_PAGE && box->index == -1 && box->nr_vars >= 1) {
+			int inner = box->values[0].i;
+			if (inner > 0 && (size_t)inner < heap_size && heap[inner].type == VM_STRING) {
+				if (heap[inner].s)
+					free_string(heap[inner].s);
+				heap[inner].s = s;
+			} else {
+				box->values[0].i = heap_alloc_string(s);
+				if (inner > 0)
+					heap_unref(inner);
+			}
+			return true;
+		}
+	}
+	free_string(s);
+	return false;
+}

diff --git a/src/hll/TextFile.c b/src/hll/TextFile.c
-static bool TextFile_ReadAll(struct string *fileName, int *text_out)
+// wrap<string> arrives as the heap slot of the target string (CIF sint32),
+// not as a pointer. See wrap_slot_set_string() in hll.h.
+static bool TextFile_ReadAll(struct string *fileName, int text_slot)
 ...
+	if (size < 0) {
+		fclose(fp);
+		return false;
+	}
 	struct string *content = string_alloc(size);
-	fread(content->text, 1, size, fp);
-	content->text[size] = '\0';
+	size_t got = fread(content->text, 1, size, fp);
+	content->text[got] = '\0';
+	content->size = got;
 	fclose(fp);
-	wrap_set_string(text_out, content);
-	return true;
+	return wrap_slot_set_string(text_slot, content);
 }
-static bool TextFile_Read(int handle, int *text_out)
+static bool TextFile_Read(int handle, int text_slot)
 ...（同樣改成 got/size 檢查，最後 return wrap_slot_set_string(text_slot, content);）
-static bool TextFile_ReadLine(int handle, int *text_out)
+// Reads one line of any length; strips the trailing LF / CRLF. Returns false
+// only when nothing could be read (EOF or error), leaving text untouched.
+static bool TextFile_ReadLine(int handle, int text_slot)
 {
 	if (handle < 0 || handle >= MAX_TEXT_FILES || !text_files[handle].active)
 		return false;
 	FILE *fp = text_files[handle].fp;
 	if (!fp || feof(fp)) return false;
+	size_t cap = 256, len = 0;
+	char *buf = xmalloc(cap);
+	int c = EOF;
+	while ((c = fgetc(fp)) != EOF) {
+		if (c == '\n')
+			break;
+		if (len + 1 >= cap) {
+			cap *= 2;
+			buf = xrealloc(buf, cap);
+		}
+		buf[len++] = (char)c;
+	}
+	if (c == EOF && len == 0) {
+		free(buf);
+		return false;
+	}
+	if (len > 0 && buf[len-1] == '\r')
+		len--;
+	struct string *line = make_string(buf, len);
+	free(buf);
+	return wrap_slot_set_string(text_slot, line);
 }

diff --git a/src/hll/VSFile.c b/src/hll/VSFile.c
+#include "system4/ain.h"
-static bool VSFile_ReadString(struct string **str)
+static struct string *vsf_read_cstring(void)
 {  (原讀取迴圈；失敗回 NULL，成功回 s) }
+// Legacy shape: bool ReadString(ref string pIString) -> struct string **.
+static bool VSFile_ReadString(struct string **str)
+{
+	struct string *s = vsf_read_cstring();
+	if (!s)
+		return false;
+	if (*str)
+		free_string(*str);
+	*str = s;
+	return true;
+}
+// v14 shape: bool ReadString(wrap<string> pIString). ffi passes the target
+// string's heap slot as sint32 (gamesave::detail::<additional read> fno 6195
+// pushes ".LOCALREF data; PUSH 7; X_REF 1", i.e. the member's VM_STRING slot).
+// The file position has already advanced, so a slot we cannot write to only
+// loses the value; keep returning true so the caller's read sequence stays in
+// sync with the file.
+static bool VSFile_ReadString_Wrap(int str_slot)
+{
+	struct string *s = vsf_read_cstring();
+	if (!s)
+		return false;
+	if (!wrap_slot_set_string(str_slot, s))
+		WARNING("VSFile.ReadString: cannot write to wrap<string> slot %d", str_slot);
+	return true;
+}

diff --git a/src/hll/FileOperation.c b/src/hll/FileOperation.c
+#include "system4/ain.h"
+/* v14 shape: array<?> GetFileList(string) / GetFolderList(string) ... (見 diff 註解) */
+static int file_list_slot(struct string *folder_name, bool folders)
+{
+	heap_gc_inhibit();
+	struct page *page = NULL;
+	if (!folder_name || !get_file_list(folder_name, &page, folders) || !page) {
+		union vm_value dim = { .i = 0 };
+		page = alloc_array(1, &dim, AIN_ARRAY_STRING, 0, false);
+	}
+	int slot = heap_alloc_page(page);
+	heap_gc_allow();
+	return slot;
+}
+static int FileOperation_GetFileList_Array(struct string *folder_name)
+{
+	return file_list_slot(folder_name, false);
+}
+static int FileOperation_GetFolderList_Array(struct string *folder_name)
+{
+	return file_list_slot(folder_name, true);
+}

diff --git a/src/hll/SealEngine.c b/src/hll/SealEngine.c
-static int SealEngine_GetInstanceInfoText(int p, int i)
+static struct string *SealEngine_GetInstanceInfoText(int p, int i)
 ...
-	struct string *s = make_string(buf, strlen(buf));
-	int slot = heap_alloc_slot(VM_STRING);
-	heap[slot].s = s;
-	return slot;
+	return make_string(buf, strlen(buf));
（GetInstanceMaterialInfoText 同樣處理；Tool_CreateFBXAscii 改為 return make_string("", 0);）

diff --git a/src/parts/layoutbox.c b/src/parts/layoutbox.c
+#include <math.h>
+#include <string.h>
+#include "system4/ain.h"
+/* v14 shape ... (見 diff 註解：原版 0x594d80/0x594df0，floorf 保持比較語義) */
+void PE_SetLayoutBoxReturnF(int parts_no, bool return_flag, float return_size)
+{
+	struct parts *parts = parts_try_get(parts_no);
+	if (!parts)
+		return;
+	struct parts_layout_box *lb = parts_get_layout_box(parts);
+	int size = (int)floorf(return_size);
+	if (lb->wrap != return_flag || lb->wrap_size != size) {
+		lb->wrap = return_flag;
+		lb->wrap_size = size;
+		parts_component_dirty(parts);
+	}
+}
+float PE_GetLayoutBoxReturnSizeF(int parts_no)
+{
+	return (float)PE_GetLayoutBoxReturnSize(parts_no);
+}

新檔 src/hll/hll_shape_select.c（見 selector_logic）；src/meson.build 在 'hll/FileOperation.c' 後加上 'hll/hll_shape_select.c'。