/* v14 "TextFile" HLL library — text file I/O */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "system4/file.h"
#include "xsystem4.h"
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

// GBK games pass GBK file names (for example under GetGameFolderPath); decode
// them like every other game path. SJIS games keep the raw fopen.
static FILE *text_file_open(struct string *fileName, const char *mode)
{
	if (!game_charset_is_gbk())
		return fopen(fileName->text, mode);
	char *path = unix_path(fileName->text);
	FILE *fp = file_open_utf8(path, mode);
	free(path);
	return fp;
}

// [1] bool WriteAll(string fileName, string text)
static bool TextFile_WriteAll(struct string *fileName, struct string *text)
{
	if (!fileName || !text) return false;
	FILE *fp = text_file_open(fileName, "wb");
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
	text_files[h].fp = text_file_open(fileName, "wb");
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
// wrap<string> arrives as the heap slot of the target string (CIF sint32),
// not as a pointer. See wrap_slot_set_string() in hll.h.
static bool TextFile_ReadAll(struct string *fileName, int text_slot)
{
	if (!fileName) return false;

	FILE *fp = text_file_open(fileName, "rb");
	if (!fp)
		return false;

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (size < 0) {
		fclose(fp);
		return false;
	}

	struct string *content = string_alloc(size);
	size_t got = fread(content->text, 1, size, fp);
	content->text[got] = '\0';
	content->size = got;
	fclose(fp);

	return wrap_slot_set_string(text_slot, content);
}

// [6] int OpenReader(string fileName)
static int TextFile_OpenReader(struct string *fileName)
{
	if (!fileName) return -1;
	int h = alloc_handle();
	if (h < 0) return -1;
	text_files[h].fp = text_file_open(fileName, "rb");
	if (!text_files[h].fp) {
		WARNING("TextFile.OpenReader: cannot open '%s'", display_game0(fileName->text));
		return -1;
	}
	text_files[h].is_writer = false;
	text_files[h].active = true;
	return h;
}

// [7] bool Read(int handle, wrap<string> text) - read all remaining
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
	if (pos < 0 || size < 0)
		return false;

	struct string *content = string_alloc(size);
	size_t got = fread(content->text, 1, size, fp);
	content->text[got] = '\0';
	content->size = got;

	return wrap_slot_set_string(text_slot, content);
}

// [8] bool ReadLine(int handle, wrap<string> text)
// Reads one line of any length; strips the trailing LF / CRLF. Returns false
// only when nothing could be read (EOF or error), leaving text untouched.
static bool TextFile_ReadLine(int handle, int text_slot)
{
	if (handle < 0 || handle >= MAX_TEXT_FILES || !text_files[handle].active)
		return false;
	FILE *fp = text_files[handle].fp;
	if (!fp || feof(fp)) return false;

	size_t cap = 256, len = 0;
	char *buf = xmalloc(cap);
	int c = EOF;
	while ((c = fgetc(fp)) != EOF) {
		if (c == '\n')
			break;
		if (len + 1 >= cap) {
			cap *= 2;
			buf = xrealloc(buf, cap);
		}
		buf[len++] = (char)c;
	}
	if (c == EOF && len == 0) {
		free(buf);
		return false;
	}
	if (len > 0 && buf[len-1] == '\r')
		len--;

	struct string *line = make_string(buf, len);
	free(buf);
	return wrap_slot_set_string(text_slot, line);
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
