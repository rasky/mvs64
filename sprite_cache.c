#include "sprite_cache.h"
#include "platform.h"
#include <memory.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef N64
#include <malloc.h>
#define LOG(...)
#else
#define memalign(a, n)  malloc(n)
#define malloc_uncached_aligned(a, n)  malloc(n)
#define LOG printf
#endif

#define SPRITE_FREEIDX_SCALE    8

// An entry of the sprite cache. This is an internal bookkeeping structure
// used of keep track of cache allocations.
typedef struct SpriteCacheEntry_s {
	struct {
		int last_tick : 8;
		uint32_t key : 24;
	};
	uint8_t *sprite;
}  SpriteCacheEntry;

// Initialize a sprite cache that can hold up to max_sprites, each one
// of sprite_size bytes.
void sprite_cache_init(SpriteCache *c, int sprite_size, int max_sprites) {
	memset(c, 0, sizeof(SpriteCache));
	c->sprite_size = sprite_size;
	c->max_sprites = max_sprites;

	// Allocate pixel data as 16-byte aligned memory. 8-byte alignment is sufficient
	// to do direct DMA from cartridge ROM, but we force 16-byte as it costs virtually
	// nothing and it also allows faster memory invalidations without writebacks.
	c->sprites = memalign(16, sprite_size * max_sprites);
	assertf(c->sprites, "memory allocation failed");

	c->free_sprite_indices = malloc(sizeof(uint16_t) * max_sprites);
	assertf(c->free_sprite_indices, "memory allocation failed");

	// Compute number of buckets as next-next power of two of the maximum number of
	// sprites. Notice that a bucket is just 32-bit of memory, so it makes sense
	// to have more buckets available to speed up lookups.
	c->num_buckets = max_sprites-1;
	c->num_buckets |= c->num_buckets >> 1;
	c->num_buckets |= c->num_buckets >> 2;
	c->num_buckets |= c->num_buckets >> 4;
	c->num_buckets |= c->num_buckets >> 8;
	c->num_buckets |= c->num_buckets >> 16;
	c->num_buckets++;
	c->num_buckets *= 2;
	c->buckets = malloc(sizeof(SpriteCacheEntry) * c->num_buckets);
	assertf(c->buckets, "memory allocation failed");

	sprite_cache_reset(c);
}

// Reset a sprite cache removing all cached entries.
void sprite_cache_reset(SpriteCache *c) {
	// Clear all the buckets
	memset(c->buckets, 0, sizeof(SpriteCacheEntry) * c->num_buckets);
	c->num_sprites = 0;

	// All sprite indices are in the free list
	assert((c->sprite_size % SPRITE_FREEIDX_SCALE) == 0);
	for (int i=0; i<c->max_sprites; i++)
		c->free_sprite_indices[i] = i * c->sprite_size / SPRITE_FREEIDX_SCALE;
}

// Increment current tick for the cache. This is used for marking cache entries
// as recently used for LRU calculation. Can be incremented every frame.
void sprite_cache_tick(SpriteCache *c) {
	c->cur_tick++;
	c->tick_cutoff = c->cur_tick-4;
	sprite_cache_pop(c);
}

static uint32_t hash(uint32_t key) {
	key = key * 2654435761;
	key ^= key >> 16;
	return key;
}

// Lookup a sprite in the cache given its key. Return the pixel data, or NULL
// if the sprite is not found.
uint8_t* sprite_cache_lookup(SpriteCache *c, uint32_t key) {
	assertf(!(key >> 24), "key must be 24-bit");
	int bidx = hash(key) & (c->num_buckets-1);

	// Check if it's the correct entry

	int dist = 0;
	while (1) {
		SpriteCacheEntry *b = &c->buckets[bidx];
		if (b->key == key && b->sprite) {
			b->last_tick = c->cur_tick;
			return b->sprite;
		}
		
		int desired = hash(b->key) & (c->num_buckets-1);
		int cur_dist = (bidx + c->num_buckets - desired) & (c->num_buckets-1);
		if (cur_dist < dist)
			return NULL;

		dist++;
		bidx = (bidx + 1) & (c->num_buckets-1);
	}
}

// Insert a sprite in the cache, given its key.
uint8_t* sprite_cache_insert(SpriteCache *c, uint32_t key) {
	assertf(!(key >> 24), "key must be 24-bit");

	if (c->num_sprites == c->max_sprites) {
		LOG("[CACHE] cache full (%d/%d)\n", c->num_sprites, c->max_sprites);
		do {
			assert(c->tick_cutoff < c->cur_tick);
			c->tick_cutoff++;
			sprite_cache_pop(c);
		} while (c->num_sprites == c->max_sprites);
	}

	int free_sidx = c->free_sprite_indices[c->max_sprites - c->num_sprites - 1];
	uint8_t *sprite = c->sprites + free_sidx * SPRITE_FREEIDX_SCALE;
	SpriteCacheEntry newb = (SpriteCacheEntry){
		.key = key,
		.sprite = sprite,
		.last_tick = c->cur_tick,
	};
	c->num_sprites++;

	int bidx = hash(key) & (c->num_buckets-1);
	int dist = 0;
	while (1) {
		SpriteCacheEntry *b = &c->buckets[bidx];

		if (b->sprite == NULL) {
			// Found an empty slot, use it
			*b = newb;
			return sprite;
		}

		// Check the distance of this slot from its optimal position
		int desired = hash(b->key) & (c->num_buckets-1);
		int cur_dist = (bidx + c->num_buckets - desired) & (c->num_buckets-1);

		if (cur_dist < dist) {
			// This slot is closer to its optimal position than the new
			// entry, so we can swap them.
			SpriteCacheEntry tmp = *b;
			*b = newb;
			newb = tmp;
			dist = cur_dist;
		}

		dist++;
		bidx = (bidx + 1) & (c->num_buckets-1);
	}
}

// Remove one or more sprites from the cache, with a pseudo-LRU approach:
// sprites are removed only if they have not been recently used (though not
// strictly the "least recent").
// The actual number of sprites that will be removed depend on the cache
// status.
void sprite_cache_pop(SpriteCache *c) {
	int bidx = rand() & (c->num_buckets-1);

	int cutoff = c->cur_tick - c->tick_cutoff;
	int n = 0;
	int target = c->max_sprites / 3 * 2;
	LOG("[CACHE] pop target %d => %d\n", c->num_sprites, target);
	while (c->num_sprites > target && n < c->num_buckets) {
		SpriteCacheEntry *b = &c->buckets[bidx];
		if (b->sprite && ((c->cur_tick & 0xFF) - b->last_tick) > cutoff) {
			// Found an entry that is older than the cutoff, remove it
			int sprite_idx = (b->sprite - c->sprites) / SPRITE_FREEIDX_SCALE;
			c->free_sprite_indices[c->max_sprites - c->num_sprites] = sprite_idx;
			c->num_sprites--;
			b->sprite = NULL;
			LOG("[CACHE] evicted (tick:%d cutoff:%d)\n", (int)b->last_tick, (int)c->tick_cutoff);
			bidx = rand() & (c->num_buckets-1);
		}

		n++;
		bidx = (bidx + 1) & (c->num_buckets-1);
	}
	LOG("[CACHE] pop end %d\n", c->num_sprites);
}
