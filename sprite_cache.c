#include "sprite_cache.h"
#include "platform.h"
#include <memory.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef N64
#include <malloc.h>
#else
#define memalign(a, n)  malloc(n)
#endif

// An entry of the sprite cache. This is an internal bookkeeping structure
// used of keep track of cache allocations.
typedef struct SpriteCacheEntry_s {
	uint32_t key;
	int32_t last_tick;
	struct SpriteCacheEntry_s *next;
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
	c->pixels = memalign(16, sprite_size * max_sprites);
	assertf(c->pixels, "memory allocation failed");

	// Allocate entries
	c->entries = calloc(sizeof(SpriteCacheEntry), max_sprites);
	assertf(c->entries, "memory allocation failed");

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
	c->buckets = malloc(sizeof(SpriteCacheEntry*) * c->num_buckets);
	assertf(c->buckets, "memory allocation failed");

	sprite_cache_reset(c);
}

// Reset a sprite cache removing all cached entries.
void sprite_cache_reset(SpriteCache *c) {
	// Clear all the buckets
	memset(c->buckets, 0, sizeof(SpriteCacheEntry*) * c->num_buckets);

	// All entries should be in the free list.
	c->free = &c->entries[0];
	for (int i=0;i<c->max_sprites-1;i++)
		c->entries[i].next = &c->entries[i+1];
}

// Increment current tick for the cache. This is used for marking cache entries
// as recently used for LRU calculation. Can be incremented every frame.
void sprite_cache_tick(SpriteCache *c) {
	c->cur_tick++;
}

static SpriteCacheEntry** bucket_ptr(SpriteCache *c, uint32_t key) {
	int bidx = (key*2654435761) & (c->num_buckets-1);
	return &c->buckets[bidx];
}

uint8_t* entry_pixels(SpriteCache *c, SpriteCacheEntry *e) {
	int entry_idx = e - c->entries;
	return c->pixels + entry_idx * c->sprite_size;
}

// Lookup a sprite in the cache given its key. Return the pixel data, or NULL
// if the sprite is not found.
uint8_t* sprite_cache_lookup(SpriteCache *c, uint32_t key) {
	// Go through the connected entries looking for the one with the specified key.
	SpriteCacheEntry *e = *bucket_ptr(c, key);
	while (e) {
		if (e->key == key) {
			e->last_tick = c->cur_tick;
			return entry_pixels(c, e);
		}
		e = e->next;
	}

	// No matching entry found
	return NULL;
}

// Remove one or more sprites from the cache, with a pseudo-LRU approach:
// sprites are removed only if they have not been recently used (though not
// strictly the "least recent").
// The actual number of sprites that will be removed depend on the cache
// status.
void sprite_cache_pop(SpriteCache *c) {
	int bidx = rand() & (c->num_buckets-1);
	bool removed_one = false;

	#define TICK_CUTOFF  1

	// Go through all buckets, starting from a random one, until we managed
	// to remove at least one entry.
	for (int i=0;i<c->num_buckets && !removed_one;i++) {
		SpriteCacheEntry** bkt = &c->buckets[bidx];

		// Go through the current bucket, and remove all entries that
		// are older than the cutoff value (currently, entries that are 2
		// ticks older than the current tick).
		// We go through the while bucket to amortize the cost of removal.
		while (*bkt) {
			if ((*bkt)->last_tick < c->cur_tick-TICK_CUTOFF) {
				SpriteCacheEntry *e = *bkt;

				// Disconnect the entry from the bucket list
				*bkt = e->next;

				// Add the entry to the free list
				e->next = c->free;
				c->free = e;
				removed_one = true;
			} else {
				// The current entry is too new, go to the next one
				bkt = &(*bkt)->next;
			}
		}

		bidx = (bidx+1) & (c->num_buckets-1);
	}
}

// Insert a sprite in the cache, given its key; if the cache is full, sprite_cache_pop
// is automatically invoked to free space. The function returns the pointer
// to the available pixel data that can be filled, or NULL if the cache is full
// and all sprites were recently used.
uint8_t* sprite_cache_insert(SpriteCache *c, uint32_t key) {
	if (!c->free) {
		sprite_cache_pop(c);
		if (!c->free)
			return NULL;
	}

	SpriteCacheEntry *e = c->free;
	c->free = e->next;

	e->key = key;
	e->last_tick = c->cur_tick;

	// Add the entry to the correct bucket (top of the list)
	SpriteCacheEntry **bkt = bucket_ptr(c, key);
	e->next = *bkt;
	*bkt = e;

	return entry_pixels(c, e);
}
