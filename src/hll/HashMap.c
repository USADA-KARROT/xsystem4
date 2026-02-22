/* v14 "HashMap" HLL library — string->int hash map */

#include <stdlib.h>
#include <string.h>

#include "system4/ain.h"
#include "system4/string.h"
#include "vm.h"
#include "vm/heap.h"
#include "vm/page.h"
#include "hll.h"

#define MAX_HASHMAPS 64
#define HASHMAP_BUCKETS 256

struct hm_entry {
	char *key;
	int value;
	struct hm_entry *next;
};

struct hashmap {
	struct hm_entry *buckets[HASHMAP_BUCKETS];
	int count;
	bool active;
};

static struct hashmap hashmaps[MAX_HASHMAPS];

static unsigned int hash_string(const char *str)
{
	unsigned int h = 5381;
	while (*str) {
		h = ((h << 5) + h) + (unsigned char)*str;
		str++;
	}
	return h % HASHMAP_BUCKETS;
}

static struct hashmap *get_map(int id)
{
	if (id < 0 || id >= MAX_HASHMAPS || !hashmaps[id].active)
		return NULL;
	return &hashmaps[id];
}

// [0] int Create()
static int HashMap_Create(void)
{
	for (int i = 0; i < MAX_HASHMAPS; i++) {
		if (!hashmaps[i].active) {
			memset(&hashmaps[i], 0, sizeof(struct hashmap));
			hashmaps[i].active = true;
			return i;
		}
	}
	WARNING("HashMap: no free slots");
	return -1;
}

static void free_map_entries(struct hashmap *map)
{
	for (int i = 0; i < HASHMAP_BUCKETS; i++) {
		struct hm_entry *e = map->buckets[i];
		while (e) {
			struct hm_entry *next = e->next;
			free(e->key);
			free(e);
			e = next;
		}
		map->buckets[i] = NULL;
	}
	map->count = 0;
}

// [1] void Release(int id)
static void HashMap_Release(int id)
{
	struct hashmap *map = get_map(id);
	if (!map) return;
	free_map_entries(map);
	map->active = false;
}

// [2] void Free(int id)
static void HashMap_Free(int id)
{
	HashMap_Release(id);
}

// [3] bool Add(int id, string key, int value)
static bool HashMap_Add(int id, struct string *key, int value)
{
	struct hashmap *map = get_map(id);
	if (!map || !key) return false;

	unsigned int h = hash_string(key->text);
	// Check for existing key
	for (struct hm_entry *e = map->buckets[h]; e; e = e->next) {
		if (!strcmp(e->key, key->text)) {
			e->value = value;
			return true;
		}
	}
	// Insert new entry
	struct hm_entry *e = malloc(sizeof(struct hm_entry));
	e->key = strdup(key->text);
	e->value = value;
	e->next = map->buckets[h];
	map->buckets[h] = e;
	map->count++;
	return true;
}

// [4] void Erase(int id, string key)
static void HashMap_Erase(int id, struct string *key)
{
	struct hashmap *map = get_map(id);
	if (!map || !key) return;

	unsigned int h = hash_string(key->text);
	struct hm_entry **pp = &map->buckets[h];
	while (*pp) {
		if (!strcmp((*pp)->key, key->text)) {
			struct hm_entry *e = *pp;
			*pp = e->next;
			free(e->key);
			free(e);
			map->count--;
			return;
		}
		pp = &(*pp)->next;
	}
}

// [5] int Numof(int id)
static int HashMap_Numof(int id)
{
	struct hashmap *map = get_map(id);
	return map ? map->count : 0;
}

// [6] bool Empty(int id)
static bool HashMap_Empty(int id)
{
	struct hashmap *map = get_map(id);
	return !map || map->count == 0;
}

// [7] bool Any(int id)
// [8] bool Any(int id, string key)
// Both overloads map to same C function; when called with 1 arg, key is undefined.
static bool HashMap_Any(int id, struct string *key)
{
	struct hashmap *map = get_map(id);
	if (!map) return false;
	if (!key) return map->count > 0;

	unsigned int h = hash_string(key->text);
	for (struct hm_entry *e = map->buckets[h]; e; e = e->next) {
		if (!strcmp(e->key, key->text))
			return true;
	}
	return false;
}

// [9] bool At(int id, string key, ref int value)
static bool HashMap_At(int id, struct string *key, int *value)
{
	struct hashmap *map = get_map(id);
	if (!map || !key) return false;

	unsigned int h = hash_string(key->text);
	for (struct hm_entry *e = map->buckets[h]; e; e = e->next) {
		if (!strcmp(e->key, key->text)) {
			if (value) *value = e->value;
			return true;
		}
	}
	return false;
}

// [10] array<string> GetKeyList(int id)
static int HashMap_GetKeyList(int id)
{
	struct hashmap *map = get_map(id);
	int n = map ? map->count : 0;

	struct page *result = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRING, n);
	result->array.rank = 1;

	if (map) {
		int idx = 0;
		for (int i = 0; i < HASHMAP_BUCKETS && idx < n; i++) {
			for (struct hm_entry *e = map->buckets[i]; e && idx < n; e = e->next) {
				int str_slot = heap_alloc_slot(VM_STRING);
				heap[str_slot].s = cstr_to_string(e->key);
				result->values[idx].i = str_slot;
				idx++;
			}
		}
	}

	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, result);
	return slot;
}

// [11] array<int> GetValueList(int id)
static int HashMap_GetValueList(int id)
{
	struct hashmap *map = get_map(id);
	int n = map ? map->count : 0;

	struct page *result = alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, n);
	result->array.rank = 1;

	if (map) {
		int idx = 0;
		for (int i = 0; i < HASHMAP_BUCKETS && idx < n; i++) {
			for (struct hm_entry *e = map->buckets[i]; e && idx < n; e = e->next) {
				result->values[idx].i = e->value;
				idx++;
			}
		}
	}

	int slot = heap_alloc_slot(VM_PAGE);
	heap_set_page(slot, result);
	return slot;
}

// [12] bool Save(wrap<array<int>> SaveDataBuffer) — stub
static bool HashMap_Save(int buf_slot)
{
	return false;
}

// [13] bool Load(wrap<array<int>> SaveDataBuffer) — stub
static bool HashMap_Load(int buf_slot)
{
	return false;
}

HLL_LIBRARY(HashMap,
	    HLL_EXPORT(Create, HashMap_Create),
	    HLL_EXPORT(Release, HashMap_Release),
	    HLL_EXPORT(Free, HashMap_Free),
	    HLL_EXPORT(Add, HashMap_Add),
	    HLL_EXPORT(Erase, HashMap_Erase),
	    HLL_EXPORT(Numof, HashMap_Numof),
	    HLL_EXPORT(Empty, HashMap_Empty),
	    HLL_EXPORT(Any, HashMap_Any),
	    HLL_EXPORT(At, HashMap_At),
	    HLL_EXPORT(GetKeyList, HashMap_GetKeyList),
	    HLL_EXPORT(GetValueList, HashMap_GetValueList),
	    HLL_EXPORT(Save, HashMap_Save),
	    HLL_EXPORT(Load, HashMap_Load)
	    );
