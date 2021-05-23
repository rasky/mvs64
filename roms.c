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
uint8_t P_ROM[2*1024*1024];
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

	#ifdef N64
	if (crom_file != -1) dfs_close(crom_file);
	crom_file = dfs_open(crom_fn[bank]);
	assertf(crom_file >= 0, "cannot open: %s", crom_fn[bank]);
	#else
	if (crom_file) fclose(crom_file);
	crom_file = fopen(crom_fn[bank], "rb");
	assertf(crom_file, "cannot open: %s", crom_fn[bank]);
	#endif

	sprite_cache_reset(&crom_cache);
}

void fixrom_preprocess(uint8_t *rom, int sz) {
	#define NIBBLE_SWAP(v_) ({ uint8_t v = (v_); (((v)>>4) | ((v)<<4)); })
	uint8_t buf[4*8];

	for (int i=0; i<sz; i+=4*8) {
		uint8_t *c0 = &rom[16], *c1 = &rom[24], *c2 = &rom[0], *c3 = &rom[8];
		uint8_t *d = buf;
		for (int j=0;j<8;j++) {
			*d++ = NIBBLE_SWAP(*c0++);
			*d++ = NIBBLE_SWAP(*c1++);
			*d++ = NIBBLE_SWAP(*c2++);
			*d++ = NIBBLE_SWAP(*c3++);
		}
		memcpy(rom, buf, 4*8);
		rom += 4*8;
	}
}

void crom_preprocess(uint8_t *rom0, int sz) {
	uint8_t *buf0 = calloc(1, sz), *buf = buf0, *rom = rom0;
	uint8_t *c1 = rom, *c2 = rom+sz/2;
	
	for (int i=0;i<sz;i+=8*16) {
		for (int b=0;b<4;b++) {
			uint8_t *dst = buf + (b&1)*64 + ((b^2)&2)*2;
			for (int y=0;y<8;y++) {
				for (int x=0;x<8;x+=2) {
					uint8_t px4 = (c1[0] >> (x)) & 1;
					uint8_t px5 = (c1[1] >> (x)) & 1;
					uint8_t px6 = (c2[0] >> (x)) & 1;
					uint8_t px7 = (c2[1] >> (x)) & 1;
					uint8_t px0 = (c1[0] >> (x+1)) & 1;
					uint8_t px1 = (c1[1] >> (x+1)) & 1;
					uint8_t px2 = (c2[0] >> (x+1)) & 1;
					uint8_t px3 = (c2[1] >> (x+1)) & 1;
					dst[x/2] = (px7<<7)|(px6<<6)|(px5<<5)|(px4<<4)|(px3<<3)|(px2<<2)|(px1<<1)|(px0<<0);
				}
				c1 += 2; c2 += 2;
				dst += 8;
			}
		}
		buf += 8*16;
	}

	memcpy(rom0, buf0, sz);
	free(buf0);
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

