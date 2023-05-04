#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "platform.h"
#include "hw.h"
#include "roms.h"
#include "sprite_cache.h"

#ifdef N64
#include <malloc.h>
#define ALIGN_256K __attribute__((aligned(256*1024)))
#else
#define memalign(a, b) malloc(b)
#define ALIGN_256K
#endif

uint8_t *P_ROM;
#define P_ROM_SIZE (1024*1024)
uint8_t *PB_ROM;
#define PB_ROM_SIZE  (1024*1024)

// Address to trigger idle-skipping
unsigned int rom_pc_idle_skip = 0;

static SpriteCache srom_cache;
static SpriteCache crom_cache;

static const char* srom_fn[2] = {NULL, NULL};
static const char* crom_fn[1] = {NULL};
static int srom_bank = -1;

#ifdef N64
static int crom_file = -1;
static int srom_file = -1;
static int pbrom_file = -1;
#else
static FILE *crom_file = NULL;
static FILE *srom_file = NULL;
static FILE *pbrom_file = NULL;
#endif

static unsigned int crom_mask;
static unsigned int crom_num_tiles;
static unsigned int srom_num_tiles;

static void rom_cache_init(void) {
	sprite_cache_init(&srom_cache, 4*8, 256);
	sprite_cache_init(&crom_cache, 8*16, 768);
}

uint8_t* srom_get_sprite(int spritenum) {
	if (spritenum >= srom_num_tiles) spritenum = srom_num_tiles-1;
	uint8_t *pix = sprite_cache_lookup(&srom_cache, spritenum);
	if (pix) return pix;

	pix = sprite_cache_insert(&srom_cache, spritenum);
	assertf(pix, "SROM cache is full");

	#ifdef N64
	dfs_seek(srom_file, spritenum*4*8, SEEK_SET);
	dfs_read(pix, 1, 4*8, srom_file);
	data_cache_hit_writeback_invalidate(pix, 4*8);    // FIXME: should not be required
	#else
	fseek(srom_file, spritenum*4*8, SEEK_SET);
	fread(pix, 1, 4*8, srom_file);
	#endif

	return pix;
}

uint8_t* crom_get_sprite(int spritenum) {
	spritenum &= crom_mask;
	if (spritenum >= crom_num_tiles) spritenum = crom_num_tiles-1;
	
	uint8_t *pix = sprite_cache_lookup(&crom_cache, spritenum);
	if (pix) return pix;

	pix = sprite_cache_insert(&crom_cache, spritenum);
	assertf(pix, "CROM cache is full");

	#ifdef N64
	dfs_seek(crom_file, spritenum*8*16, SEEK_SET);
	dfs_read(pix, 1, 8*16, crom_file);
	data_cache_hit_writeback_invalidate(pix, 8*16);  // FIXME: should not be required
	#else
	fseek(crom_file, spritenum*8*16, SEEK_SET);
	fread(pix, 1, 8*16, crom_file);
	#endif

	return pix;
}

void srom_set_bank(int bank) {
	assert(bank == 0 || bank == 1);
	unsigned len;

	if (srom_bank != bank) {
		srom_bank = bank;

		#ifdef N64
		if (srom_file != -1) dfs_close(srom_file);
		srom_file = dfs_open(srom_fn[srom_bank]);
		assertf(srom_file >= 0, "cannot open: %s", srom_fn[srom_bank]);
		len = dfs_size(srom_file);
		#else
		if (srom_file) fclose(srom_file);
		srom_file = fopen(srom_fn[bank], "rb");
		assertf(srom_file, "cannot open: %s", srom_fn[bank]);
		fseek(srom_file, 0, SEEK_END);
		len = ftell(srom_file);
		#endif

		sprite_cache_reset(&srom_cache);
		srom_num_tiles = len / (4*8);
	}
}

void crom_set_bank(int bank) {
	assert(bank == 0);
	unsigned len;

	#ifdef N64
	if (crom_file != -1) dfs_close(crom_file);
	crom_file = dfs_open(crom_fn[bank]);
	assertf(crom_file >= 0, "cannot open: %s", crom_fn[bank]);
	len = dfs_size(crom_file);
	#else
	if (crom_file) fclose(crom_file);
	crom_file = fopen(crom_fn[bank], "rb");
	assertf(crom_file, "cannot open: %s", crom_fn[bank]);
	fseek(crom_file, 0, SEEK_END);
	len = ftell(crom_file);
	#endif

	sprite_cache_reset(&crom_cache);
	crom_num_tiles = len / (8*16);

	// Calculate mask based on next power of two
	len /= 8*16;
	len -= 1;
	len |= len >> 1;
	len |= len >> 2;
	len |= len >> 4;
	len |= len >> 8;
	len |= len >> 16;
	crom_mask = len;
}

// PBROM handling.
//
// PBROM is a made-up name for the banked part of the PROM, which is
// mapped at 0x2xxxxx in the 68K address space. The ROM is saved in the
// B.ROM file, separated from P.ROM which contains only the first Mb
// (mapped at 0x0xxxxx).
//
// mvs64 allocates 1 Mb of total RDRAM to handle PBROM (PB_ROM array).
// There are two possible options, depending on the game: if the PBROM
// file is 1 Mb or less, we can simply load it in RDRAM in full. We call
// this "linear mapping of PBROM", and it's the easiest and fastest option.
//
// If the file is larger than the PBROM area (it can be up to 4Mb),
// then we switch to a cache-based implementation, where chunks of the
// B.ROM file are loaded and used on demand. The cache is organized
// around small chunks of data ("banks"). Banks are very small (eg: 64
// bytes) because games do many random accesses in the PBROM area so it
// doesn't make sense to waste time loading large banks when just a few
// bytes are accessed.
//
// Banks are kept in a hash table, that is sorted using the remaining bits
// of the bank address (PBROM_LOOKUP_BITS). The hashtable itself is stored
// in the PB_ROM array, so to reuse the same buffer of memory.
//
// Notice that in case of linear mapping, we simply use TLB to map the PBROM
// to the 68K; instead in case of cache, we cannot use TLB because we would
// be forced to use 4K banks, which would be too big.
#define PBROM_BANK_BITS    6
#define PBROM_LOOKUP_BITS  12
#define PBROM_BANK_MASK    ((1<<PBROM_BANK_BITS)-1)

// Single entry of the PBROM cache. We store two different memory banks
// for each entry, so to do very simple hash function conflicts. This is
// very important because there will always be conflicts and we absolutely
// need to avoid race loops where two entries push each other off the cache
// multiple times per frame.
//
// Notice also that we load 2 bytes more for each bank, to allow for
// a 32-bit memory read that crosses the bank boundary.
//
// Memory areas are kept aligned to 8 bytes to allow for direct DMA.
typedef struct {
	uint8_t mem1[(1<<PBROM_BANK_BITS)+2] __attribute__((aligned(8)));
	uint32_t bank1;
 	uint8_t mem2[(1<<PBROM_BANK_BITS)+2] __attribute__((aligned(8)));
 	uint32_t bank2;
} PBROMCacheEntry;

_Static_assert(sizeof(PBROMCacheEntry)*(1<<PBROM_LOOKUP_BITS) <= PB_ROM_SIZE, "PBROM cache too big");

static bool pbrom_is_linear = false;
static uint32_t pbrom_last_bank = 0xFFFFFFFF;
static uint8_t *pbrom_last_mem = NULL;

void pbrom_init(const char *fn) {
	PB_ROM = memalign(256*1024, PB_ROM_SIZE);
	assertf(PB_ROM, "cannot allocate PBROM buffer");
	unsigned len;
	#ifdef N64
	if (pbrom_file != -1) dfs_close(pbrom_file);
	pbrom_file = dfs_open(fn);
	if (pbrom_file == -1) {
		pbrom_is_linear = true;
		return;
	}
	len = dfs_size(pbrom_file);
	#else
	if (pbrom_file) fclose(pbrom_file);
	pbrom_file = fopen(fn, "rb");
	if (pbrom_file == NULL) {
		pbrom_is_linear = true;
		return;
	}
	fseek(pbrom_file, 0, SEEK_END);
	len = ftell(pbrom_file);
	fseek(pbrom_file, 0, SEEK_SET);
	#endif

	if (len > PB_ROM_SIZE) {
		pbrom_is_linear = false;
		pbrom_cache_init();
		return;
	}

	// We have enough RDRAM to fully load the PBROM file into RDRAM
	// (aka linear mapping)
	#ifdef N64
	dfs_read(PB_ROM, 1, len, pbrom_file);
	dfs_close(pbrom_file); pbrom_file = -1;
	#else
	fread(PB_ROM, 1, len, pbrom_file);
	fclose(pbrom_file); pbrom_file = NULL;
	#endif
	pbrom_is_linear = true;
}

static bool fastrand_bool(void) {
	#ifdef N64
	return C0_COUNT() & 2;
	#else
	return rand() & 1;
	#endif
}

// Return the PBROM linear mapping area, or NULL in case PBROM is banked.
uint8_t* pbrom_linear(void) {
	return pbrom_is_linear ? PB_ROM : NULL;
}

void pbrom_cache_init(void) {
	// Initialize pbrom cache
	PBROMCacheEntry *cache = (PBROMCacheEntry *)PB_ROM;
	for (int i=0; i < 1<<PBROM_LOOKUP_BITS; i++) {
		cache[i].bank1 = 0xFFFFFFFF;
		cache[i].bank2 = 0xFFFFFFFF;
	}
}

// Lookup PBROM cache, loading data on demand.
uint8_t *pbrom_cache_lookup(uint32_t address) {
	assert(!pbrom_is_linear);
	PBROMCacheEntry *prom_cache = (PBROMCacheEntry*)PB_ROM;

	// See if this is the same bank that was last accessed.
	// This is a simple speedup for the common case of multiple
	// subsequent accesses to nearby addresses.
	uint32_t bank = address >> PBROM_BANK_BITS;
	if (bank == pbrom_last_bank) return pbrom_last_mem + (address & PBROM_BANK_MASK);

	// Calculate hash function for the requested address. Experimentally
	// this works reasonably well with the kind of addresses that we see
	// in games, and is fast enough.
	uint32_t entry = bank;
	entry ^= entry >> 16;
  	entry *= 2654435761;
	entry ^= entry >> 16;
	entry *= 2654435761;
	entry &= ((1<<PBROM_LOOKUP_BITS)-1);

	// Lookup the cache to check whether this address have been already
	// loaded.
	PBROMCacheEntry *c = &prom_cache[entry];
	if (c->bank1 == bank) return c->mem1 + (address & PBROM_BANK_MASK);
	if (c->bank2 == bank) return c->mem2 + (address & PBROM_BANK_MASK);

	// Populate the cache, loading from N64 ROM
	uint32_t base = bank << PBROM_BANK_BITS;
	debugf("[PBROM] loading %06x (bank:%x entry:%x)\n", (unsigned)base, (unsigned)bank, (unsigned)entry);
	uint8_t *mem;
	if (fastrand_bool()) { c->bank1 = bank; mem = c->mem1; }
	else                 { c->bank2 = bank; mem = c->mem2; }

	#ifdef N64
	dfs_seek(pbrom_file, base, SEEK_SET);
	dfs_read(mem, 1, (1<<PBROM_BANK_BITS)+2, pbrom_file);
	#else
	fseek(pbrom_file, base, SEEK_SET);
	fread(mem, 1, (1<<PBROM_BANK_BITS)+2, pbrom_file);
	#endif

	pbrom_last_mem = mem;
	pbrom_last_bank = bank;

	return mem + (address & PBROM_BANK_MASK);
}

static void rom(const char *dir, const char* name, int off, int sz, uint8_t *buf, int bufsize, bool bswap) {
	char fullname[1024];
	strlcpy(fullname, dir, sizeof(fullname));
	strlcat(fullname, name, sizeof(fullname));

	FILE *f = fopen(fullname, "rb");
	assertf(f, "file not found: %s", fullname);
	if (!sz) {
		fseek(f, 0, SEEK_END);
		sz = ftell(f);
	}
	assertf(sz <= bufsize, "ROM too big: %s", name);
	fseek(f, off, SEEK_SET);
	int read = fread(buf, 1, sz, f);
	fclose(f);
	assertf(read == sz, "rom:%s off:%d sz:%d read:%d", fullname, off, sz, read);

	if (bswap) {
		for (int i=0;i<sz;i+=2) {
			uint8_t v = buf[i];
			buf[i] = buf[i+1];
			buf[i+1] = v;
		}
	}
}

#define strcatalloc(a, b) ({ char v[strlen(a)+strlen(b)+1]; strcpy(v, a); strcat(v, b); strdup(v); })

static uint32_t ini_get_integer(const char *ini, const char *key, bool *ok) {
	int klen = strlen(key); char *kv; 
	if ((kv = strstr(ini, key)) && kv[klen] == '=') {
		kv += klen+1;
		if (ok) *ok = true;
		if (kv[0] == '0' && kv[1] == 'x')
			return strtoul(kv, NULL, 16);
		else
			return strtoul(kv, NULL, 10);
	}
	if (ok) *ok=false;
	return 0;
}

void rom_next_frame(void) {
	sprite_cache_tick(&srom_cache);
	sprite_cache_tick(&crom_cache);
}

void rom_load(const char *dir) {
	P_ROM = memalign(256*1024, 1024*1024);
	assertf(P_ROM, "cannot allocate P_ROM buffer");
	rom(dir, "p.bios", 0, 0, BIOS, sizeof(BIOS), false);
	rom(dir, "p.rom", 0, 0, P_ROM, P_ROM_SIZE, false);

	char ini[1024];
	strcpy(ini, dir);
	strcat(ini, "game.ini");
	FILE *f = fopen(ini, "rb");
	if (f) {
		fread(ini, 1, sizeof(ini), f);
		fclose(f);

		bool ok;

		rom_pc_idle_skip = ini_get_integer(ini, "idle_skip", &ok);
		if (ok) debugf("[ROM] configure idle_skip: %x\n", rom_pc_idle_skip);
	}
	rom_pc_idle_skip = 0;

	#ifdef N64
	dir = "";
	#endif
	
	srom_fn[0] = strcatalloc(dir, "s.bios");
	srom_fn[1] = strcatalloc(dir, "s.rom");
	crom_fn[0] = strcatalloc(dir, "c.rom");

	rom_cache_init();
	srom_set_bank(0);  // Set SFIX as current
	crom_set_bank(0);
	pbrom_init(strcatalloc(dir, "b.rom"));
}

