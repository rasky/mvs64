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
uint8_t P_ROM[2*1024*1024] __attribute__((aligned(256*1024)));
#else
uint8_t P_ROM[8*1024*1024];
#endif

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
#else
static FILE *crom_file = NULL;
static FILE *srom_file = NULL;
#endif

static unsigned int crom_mask;

static void rom_cache_init(void) {
	sprite_cache_init(&srom_cache, 4*8, 256);
	sprite_cache_init(&crom_cache, 8*16, 768);
}

uint8_t* srom_get_sprite(int spritenum) {
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
	if (srom_bank != bank) {
		srom_bank = bank;

		#ifdef N64
		if (srom_file != -1) dfs_close(srom_file);
		srom_file = dfs_open(srom_fn[srom_bank]);
		assertf(srom_file >= 0, "cannot open: %s", srom_fn[srom_bank]);
		#else
		if (srom_file) fclose(srom_file);
		srom_file = fopen(srom_fn[bank], "rb");
		assertf(srom_file, "cannot open: %s", srom_fn[bank]);
		#endif

		sprite_cache_reset(&srom_cache);
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

static void rom(const char *dir, const char* name, int off, int sz, uint8_t *buf, bool bswap) {
	char fullname[1024];
	strlcpy(fullname, dir, sizeof(fullname));
	strlcat(fullname, name, sizeof(fullname));

	FILE *f = fopen(fullname, "rb");
	assertf(f, "file not found: %s", fullname);
	if (!sz) {
		fseek(f, 0, SEEK_END);
		sz = ftell(f);
	}
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
	rom(dir, "p.bios", 0, 0, BIOS, false);
	rom(dir, "p.rom", 0, 0, P_ROM, false);

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

	#ifdef N64
	dir = "";
	#endif
	
	srom_fn[0] = strcatalloc(dir, "s.bios");
	srom_fn[1] = strcatalloc(dir, "s.rom");
	crom_fn[0] = strcatalloc(dir, "c.rom");

	rom_cache_init();
	srom_set_bank(0);  // Set SFIX as current
	crom_set_bank(0);
}

