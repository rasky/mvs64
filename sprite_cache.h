#ifndef SPRITE_CACHE_H
#define SPRITE_CACHE_H

#include <stdint.h>

typedef struct SpriteCacheEntry_s SpriteCacheEntry;

// A sprite cache.
typedef struct {
	int sprite_size;                // size of a sprite in bytes
	int max_sprites;                // maximum number of sprites in cache
	int num_buckets;                // number of hash table buckets (should be pow2)
	int32_t cur_tick;               // current tick (frame counter)
	uint8_t *pixels;                // pixel memory (for all sprites)
	SpriteCacheEntry **buckets;     // hash table buckets
	SpriteCacheEntry *entries;      // all cache entries (one per sprite)
	SpriteCacheEntry *free;         // list of free (unallocated) entries
} SpriteCache;

void sprite_cache_init(SpriteCache *c, int sprite_size, int max_sprites);
void sprite_cache_reset(SpriteCache *c);
void sprite_cache_tick(SpriteCache *c);
uint8_t* sprite_cache_lookup(SpriteCache *c, uint32_t key);
uint8_t* sprite_cache_insert(SpriteCache *c, uint32_t key);
void sprite_cache_pop(SpriteCache *c);

#endif /* SPRITE_CACHE_H */
