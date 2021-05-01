#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "platform.h"
#include "hw.h"
#include "roms.h"

#ifdef N64

uint8_t P_ROM[2*1024*1024] __attribute__((aligned(256*1024)));

//static uint8_t crom_cache[(8*16) * 128] __attribute__((aligned(8)));
// static int crom_cache_idx;
// static int32_t crom_file;

uint8_t* crom_get_sprite(int spritenum) {
	return NULL;
	// return crom_cache;
}

uint8_t* srom_get_sprite(int spritenum) {
	return NULL;
	// return crom_cache;
}

#else

uint8_t P_ROM[5*1024*1024];
static uint8_t C_ROM[64*1024*1024];

static void fixrom_preprocess(uint8_t *rom, int sz) {
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

static void crom_preprocess(uint8_t *rom0, int sz) {
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

	FILE *f = fopen("sprites.bin", "wb");
	fwrite(rom0, 1, sz, f);
	fclose(f);
}

uint8_t* crom_get_sprite(int spritenum) {
	return C_ROM + spritenum*8*16;
}

#endif

static void rom(const char *dir, const char* name, int off, int sz, uint8_t *buf, bool bswap) {
	char fullname[1024];
	strlcpy(fullname, dir, sizeof(fullname));
	strlcat(fullname, name, sizeof(fullname));

	FILE *f = fopen(fullname, "rb");
	assertf(f, "file not found: %s", fullname);
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

void rom_load_bios(const char *dir) {
	// rom(dir, "sp1-selftest.bin", 0, 128*1024, BIOS, true);
	rom(dir, "sp-s2.sp1", 0, 128*1024, BIOS, true);
	#ifdef N64
	rom(dir, "sfix.n64.bin", 0, 128*1024, SFIX_ROM, false);
	#else
	rom(dir, "sfix.sfix", 0, 128*1024, SFIX_ROM, false);
	fixrom_preprocess(SFIX_ROM, 128*1024);
	FILE *f=fopen("sfix.n64.bin", "wb");
	fwrite(SFIX_ROM, 128*1024, 1, f);
	fclose(f);
	#endif
}

void rom_load_mslug(const char *dir) {
	rom(dir, "201-p1.bin", 1*1024*1024, 1024*1024, P_ROM+0*1024*1024, true);
	rom(dir, "201-p1.bin", 0*1024*1024, 1024*1024, P_ROM+1*1024*1024, true);
	#ifdef N64
	rom(dir, "201-s1.n64.bin", 0, 128*1024, S_ROM, false);
	#else
	rom(dir, "201-s1.bin", 0, 128*1024, S_ROM, false);
	rom(dir, "201-c1.bin", 0, 4*1024*1024, C_ROM+0*4*1024*1024, false);
	rom(dir, "201-c3.bin", 0, 4*1024*1024, C_ROM+1*4*1024*1024, false);
	rom(dir, "201-c2.bin", 0, 4*1024*1024, C_ROM+2*4*1024*1024, false);
	rom(dir, "201-c4.bin", 0, 4*1024*1024, C_ROM+3*4*1024*1024, false);
	fixrom_preprocess(S_ROM, 128*1024);
	crom_preprocess(C_ROM, 16*1024*1024);
	FILE *f=fopen("201-s1.n64.bin", "wb");
	fwrite(C_ROM, 128*1024, 1, f);
	fclose(f);
	f=fopen("201-c1.n64.bin", "wb");
	fwrite(C_ROM, 16*1024*1024, 1, f);
	fclose(f);
	#endif
}

void rom_load_kof98(const char *dir) {
	rom(dir, "kof98_p1.rom", 0, 1024*1024, P_ROM+0*1024*1024, true);
	rom(dir, "kof98_p2.rom", 0, 4*1024*1024, P_ROM+1*1024*1024, true);
	rom(dir, "kof98_s1.rom", 0, 128*1024, S_ROM, false);
	#ifndef N64
	rom(dir, "kof98_c1.rom", 0, 8*1024*1024, C_ROM+0*8*1024*1024, false);
	rom(dir, "kof98_c3.rom", 0, 8*1024*1024, C_ROM+1*8*1024*1024, false);
	rom(dir, "kof98_c5.rom", 0, 8*1024*1024, C_ROM+2*8*1024*1024, false);
	rom(dir, "kof98_c7.rom", 0, 8*1024*1024, C_ROM+3*8*1024*1024, false);
	rom(dir, "kof98_c2.rom", 0, 8*1024*1024, C_ROM+4*8*1024*1024, false);
	rom(dir, "kof98_c4.rom", 0, 8*1024*1024, C_ROM+5*8*1024*1024, false);
	rom(dir, "kof98_c6.rom", 0, 8*1024*1024, C_ROM+6*8*1024*1024, false);
	rom(dir, "kof98_c8.rom", 0, 8*1024*1024, C_ROM+7*8*1024*1024, false);
	fixrom_preprocess(S_ROM, 128*1024);
	crom_preprocess(C_ROM, 64*1024*1024);
	#endif
}

void rom_load_aof(const char *dir) {
	rom(dir, "044-p1.bin", 0, 512*1024, P_ROM, true);
	rom(dir, "044-s1.bin", 0, 128*1024, S_ROM, false);
	#ifndef N64
	rom(dir, "044-c1.bin", 0, 2*1024*1024, C_ROM+0*2*1024*1024, false);
	rom(dir, "044-c3.bin", 0, 2*1024*1024, C_ROM+1*2*1024*1024, false);
	rom(dir, "044-c2.bin", 0, 2*1024*1024, C_ROM+2*2*1024*1024, false);
	rom(dir, "044-c4.bin", 0, 2*1024*1024, C_ROM+3*2*1024*1024, false);
	fixrom_preprocess(S_ROM, 128*1024);
	crom_preprocess(C_ROM, 8*1024*1024);
	#endif
}

void rom_load_samsho(const char *dir) {
	rom(dir, "045-p1.bin", 0, 1024*1024, P_ROM, true);
	rom(dir, "045-s1.bin", 0, 128*1024, S_ROM, false);
	#ifndef N64
	rom(dir, "045-c1.bin", 0, 2*1024*1024, C_ROM+0*2*1024*1024, false);
	rom(dir, "045-c3.bin", 0, 2*1024*1024, C_ROM+1*2*1024*1024, false);
	rom(dir, "045-c51.bin", 0, 1*1024*1024, C_ROM+2*2*1024*1024, false);
	rom(dir, "045-c2.bin", 0, 2*1024*1024, C_ROM+3*2*1024*1024, false);
	rom(dir, "045-c4.bin", 0, 2*1024*1024, C_ROM+4*2*1024*1024, false);
	rom(dir, "045-c61.bin", 0, 1*1024*1024, C_ROM+5*2*1024*1024, false);
	fixrom_preprocess(S_ROM, 128*1024);
	crom_preprocess(C_ROM, 12*1024*1024);
	#endif
}

void rom_load_nyanmvs(const char *dir) {
	rom(dir, "052-p1.bin", 0, 128*1024, P_ROM, false);
	rom(dir, "052-s1.bin", 0, 128*1024, S_ROM, false);
	#ifndef N64
	rom(dir, "052-c1.bin", 0, 4*1024*1024, C_ROM+0*4*1024*1024, false);
	rom(dir, "052-c2.bin", 0, 4*1024*1024, C_ROM+1*4*1024*1024, false);
	fixrom_preprocess(S_ROM, 128*1024);
	crom_preprocess(C_ROM, 8*1024*1024);
	#endif
}

void rom_load_spriteex(const char *dir) {
	rom(dir, "052-p1.bin", 0, 512*1024, P_ROM, true);
	rom(dir, "052-s1.bin", 0, 128*1024, S_ROM, false);
	#ifndef N64
	rom(dir, "052-c1.bin", 0, 4*1024*1024, C_ROM+0*4*1024*1024, false);
	rom(dir, "052-c2.bin", 0, 4*1024*1024, C_ROM+1*4*1024*1024, false);
	fixrom_preprocess(S_ROM, 128*1024);
	crom_preprocess(C_ROM, 8*1024*1024);
	#endif
}

void rom_load_krom(const char *dir) {
	rom(dir, "LSPC/3D-LineSpriteChain/202-p1.p1", 0, 512*1024, P_ROM, true);
	rom(dir, "LSPC/3D-LineSpriteChain/202-s1.s1", 0, 128*1024, S_ROM, false);
	#ifndef N64
	rom(dir, "LSPC/3D-LineSpriteChain/202-c1.c1", 0, 1024*1024, C_ROM, false);
	rom(dir, "LSPC/3D-LineSpriteChain/202-c2.c2", 0, 1024*1024, C_ROM+1024*1024, false);
	fixrom_preprocess(S_ROM, 128*1024);
	crom_preprocess(C_ROM, 2*1024*1024);
	#endif
}

