/* v14 "TextFile" HLL library — text file I/O */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hll.h"

#define MAX_TEXT_FILES 32

struct text_file {
	FILE *fp;
	bool is_writer;
	bool active;
};

static struct text_file text_files[MAX_TEXT_FILES];

static int alloc_handle(void)
{
	for (int i = 0; i < MAX_TEXT_FILES; i++) {
		if (!text_files[i].active)
			return i;
	}
	return -1;
}

// [0] bool Close(int handle)
static bool TextFile_Close(int handle)
{
	if (handle < 0 || handle >= MAX_TEXT_FILES || !text_files[handle].active)
		return false;
	if (text_files[handle].fp)
		fclose(text_files[handle].fp);
	text_files[handle].fp = NULL;
	text_files[handle].active = false;
	return true;
}

// [1] bool WriteAll(string fileName, string text)
static bool TextFile_WriteAll(struct string *fileName, struct string *text)
{
	if (!fileName || !text) return false;
	FILE *fp = fopen(fileName->text, "wb");
	if (!fp) return false;
	fwrite(text->text, 1, text->size, fp);
	fclose(fp);
	return true;
}

// [2] int CreateWriter(string fileName)
static int TextFile_CreateWriter(struct string *fileName)
{
	if (!fileName) return -1;
	int h = alloc_handle();
	if (h < 0) return -1;
	text_files[h].fp = fopen(fileName->text, "wb");
	if (!text_files[h].fp) return -1;
	text_files[h].is_writer = true;
	text_files[h].active = true;
	return h;
}

// [3] bool Write(int handle, string text)
static bool TextFile_Write(int handle, struct string *text)
{
	if (handle < 0 || handle >= MAX_TEXT_FILES || !text_files[handle].active)
		return false;
	if (!text_files[handle].fp || !text) return false;
	fwrite(text->text, 1, text->size, text_files[handle].fp);
	return true;
}

// [4] bool WriteLine(int handle, string text)
static bool TextFile_WriteLine(int handle, struct string *text)
{
	if (!TextFile_Write(handle, text)) return false;
	fputc('\n', text_files[handle].fp);
	return true;
}

// [5] bool ReadAll(string fileName, wrap<string> text)
// v14: AIN_WRAP — FFI passes heap slot index as int.
static bool TextFile_ReadAll(struct string *fileName, int text_slot)
{
	if (!fileName) return false;

	FILE *fp = fopen(fileName->text, "rb");
	if (!fp) {
		static int ra_warn = 0;
		if (ra_warn++ < 5)
			WARNING("TextFile.ReadAll: cannot open '%s'", fileName->text);
		return false;
	}

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	struct string *content = string_alloc(size);
	fread(content->text, 1, size, fp);
	content->text[size] = '\0';
	fclose(fp);

	wrap_set_string(text_slot, content);
	return true;
}

// [6] int OpenReader(string fileName)
static int TextFile_OpenReader(struct string *fileName)
{
	if (!fileName) return -1;
	int h = alloc_handle();
	if (h < 0) return -1;
	text_files[h].fp = fopen(fileName->text, "rb");
	if (!text_files[h].fp) {
		WARNING("TextFile.OpenReader: cannot open '%s'", fileName->text);
		return -1;
	}
	text_files[h].is_writer = false;
	text_files[h].active = true;
	return h;
}

// [7] bool Read(int handle, wrap<string> text) — read all remaining
static bool TextFile_Read(int handle, int text_slot)
{
	if (handle < 0 || handle >= MAX_TEXT_FILES || !text_files[handle].active)
		return false;
	FILE *fp = text_files[handle].fp;
	if (!fp) return false;

	long pos = ftell(fp);
	fseek(fp, 0, SEEK_END);
	long end = ftell(fp);
	fseek(fp, pos, SEEK_SET);
	long size = end - pos;

	struct string *content = string_alloc(size);
	fread(content->text, 1, size, fp);
	content->text[size] = '\0';

	wrap_set_string(text_slot, content);
	return true;
}

// [8] bool ReadLine(int handle, wrap<string> text)
static bool TextFile_ReadLine(int handle, int text_slot)
{
	if (handle < 0 || handle >= MAX_TEXT_FILES || !text_files[handle].active)
		return false;
	FILE *fp = text_files[handle].fp;
	if (!fp || feof(fp)) return false;

	char buf[4096];
	if (!fgets(buf, sizeof(buf), fp))
		return false;

	int len = strlen(buf);
	while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
		len--;
	buf[len] = '\0';

	wrap_set_string(text_slot, make_string(buf, len));
	return true;
}

// [9] bool IsEOF(int handle)
static bool TextFile_IsEOF(int handle)
{
	if (handle < 0 || handle >= MAX_TEXT_FILES || !text_files[handle].active)
		return true;
	return !text_files[handle].fp || feof(text_files[handle].fp);
}

HLL_LIBRARY(TextFile,
	    HLL_EXPORT(Close, TextFile_Close),
	    HLL_EXPORT(WriteAll, TextFile_WriteAll),
	    HLL_EXPORT(CreateWriter, TextFile_CreateWriter),
	    HLL_EXPORT(Write, TextFile_Write),
	    HLL_EXPORT(WriteLine, TextFile_WriteLine),
	    HLL_EXPORT(ReadAll, TextFile_ReadAll),
	    HLL_EXPORT(OpenReader, TextFile_OpenReader),
	    HLL_EXPORT(Read, TextFile_Read),
	    HLL_EXPORT(ReadLine, TextFile_ReadLine),
	    HLL_EXPORT(IsEOF, TextFile_IsEOF)
	    );
