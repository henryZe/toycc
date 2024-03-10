// This is an implementation of the open-addressing hash table.

#include <toycc.h>
#include <hashmap.h>

// Initial hash bucket size
#define INIT_SIZE 16
// Rehash if the usage exceeds 70%.
#define HIGH_WATERMARK 70
// We'll keep the usage below 50% after rehashing.
#define LOW_WATERMARK 50
// Represents a deleted hash entry
#define TOMBSTONE ((void *)-1)

static uint64_t fnv_hash(const char *s, int len)
{
	// 64-bit offset_basis
	uint64_t hash = 0xcbf29ce484222325;

	for (int i = 0; i < len; i++) {
		// 64-bit FNV-prime
		hash *= 0x100000001b3;
		hash ^= (unsigned char)s[i];
	}
	return hash;
}

static bool match(struct HashEntry *ent, const char *key, int keylen)
{
	return ent->key && ent->key != TOMBSTONE &&
		ent->keylen == keylen && memcmp(ent->key, key, keylen) == 0;
}

// Make room for new entires in a given hashmap by
// 1. removing tombstones
// 2. and possibly extending the bucket size.
static void rehash(struct HashMap *map)
{
	// Compute the size of the new hashmap.
	int nkeys = 0;
	for (int i = 0; i < map->capacity; i++)
		if (map->buckets[i].key && map->buckets[i].key != TOMBSTONE)
			nkeys++;

	int cap = map->capacity;
	while ((nkeys * 100) / cap >= LOW_WATERMARK)
		cap *= 2;
	assert(cap > 0);

	// Create a new hashmap and copy all key-values.
	struct HashMap map2 = {};
	map2.buckets = calloc(cap, sizeof(struct HashEntry));
	map2.capacity = cap;

	for (int i = 0; i < map->capacity; i++) {
		struct HashEntry *ent = &map->buckets[i];
		if (ent->key && ent->key != TOMBSTONE)
			hashmap_put2(&map2, ent->key, ent->keylen, ent->val);
	}

	assert(map2.used == nkeys);
	*map = map2;
}

static struct HashEntry *get_entry(struct HashMap *map, const char *key, int keylen)
{
	if (!map->buckets)
		return NULL;

	uint64_t hash = fnv_hash(key, keylen);

	for (int i = 0; i < map->capacity; i++) {
		struct HashEntry *ent = &map->buckets[(hash + i) % map->capacity];
		if (match(ent, key, keylen))
			return ent;
		if (ent->key == NULL)
			return NULL;

		// check next
	}
	unreachable();
}

static struct HashEntry *get_or_insert_entry(struct HashMap *map, const char *key, int keylen)
{
	if (!map->buckets) {
		map->buckets = calloc(INIT_SIZE, sizeof(struct HashEntry));
		map->capacity = INIT_SIZE;

	} else if ((map->used * 100) / map->capacity >= HIGH_WATERMARK) {
		rehash(map);
	}

	uint64_t hash = fnv_hash(key, keylen);

	for (int i = 0; i < map->capacity; i++) {
		struct HashEntry *ent = &map->buckets[(hash + i) % map->capacity];

		if (match(ent, key, keylen))
			return ent;

		if (ent->key == TOMBSTONE) {
			ent->key = key;
			ent->keylen = keylen;
			return ent;
		}

		if (ent->key == NULL) {
			ent->key = key;
			ent->keylen = keylen;
			map->used++;
			return ent;
		}

		// check next
	}
	unreachable();
}

void *hashmap_get2(struct HashMap *map, const char *key, int keylen)
{
	struct HashEntry *ent = get_entry(map, key, keylen);
	return ent ? ent->val : NULL;
}

void hashmap_put2(struct HashMap *map, const char *key, int keylen, void *val)
{
	struct HashEntry *ent = get_or_insert_entry(map, key, keylen);
	ent->val = val;
}

void hashmap_delete2(struct HashMap *map, const char *key, int keylen)
{
	struct HashEntry *ent = get_entry(map, key, keylen);
	if (ent)
		ent->key = TOMBSTONE;
}

void *hashmap_get(struct HashMap *map, const char *key)
{
	return hashmap_get2(map, key, strlen(key));
}

void hashmap_put(struct HashMap *map, const char *key, void *val)
{
	hashmap_put2(map, key, strlen(key), val);
}

void hashmap_delete(struct HashMap *map, const char *key)
{
	hashmap_delete2(map, key, strlen(key));
}

void hashmap_test(void)
{
	struct HashMap *map = calloc(1, sizeof(struct HashMap));

	for (int i = 0; i < 5000; i++)
		hashmap_put(map, format("key %d", i), (void *)(size_t)i);
	for (int i = 1000; i < 2000; i++)
		hashmap_delete(map, format("key %d", i));
	for (int i = 1500; i < 1600; i++)
		hashmap_put(map, format("key %d", i), (void *)(size_t)i);
	for (int i = 6000; i < 7000; i++)
		hashmap_put(map, format("key %d", i), (void *)(size_t)i);

	for (int i = 0; i < 1000; i++)
		assert((size_t)hashmap_get(map, format("key %d", i)) == (size_t)i);
	for (int i = 1000; i < 1500; i++)
		assert(hashmap_get(map, "no such key") == NULL);
	for (int i = 1500; i < 1600; i++)
		assert((size_t)hashmap_get(map, format("key %d", i)) == (size_t)i);
	for (int i = 1600; i < 2000; i++)
		assert(hashmap_get(map, "no such key") == NULL);
	for (int i = 2000; i < 5000; i++)
		assert((size_t)hashmap_get(map, format("key %d", i)) == (size_t)i);
	for (int i = 5000; i < 6000; i++)
		assert(hashmap_get(map, "no such key") == NULL);
	for (int i = 6000; i < 7000; i++)
		hashmap_put(map, format("key %d", i), (void *)(size_t)i);

	assert(hashmap_get(map, "no such key") == NULL);
	printf("OK\n");
}
