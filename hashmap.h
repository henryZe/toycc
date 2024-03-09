struct HashEntry {
	const char *key;
	int keylen;
	void *val;
};

struct HashMap {
	struct HashEntry *buckets;
	int capacity;
	int used;
};

void *hashmap_get(struct HashMap *map, const char *key);
void *hashmap_get2(struct HashMap *map, const char *key, int keylen);
void hashmap_put(struct HashMap *map, const char *key, void *val);
void hashmap_put2(struct HashMap *map, const char *key, int keylen, void *val);
void hashmap_delete(struct HashMap *map, const char *key);
void hashmap_delete2(struct HashMap *map, const char *key, int keylen);
void hashmap_test(void);
