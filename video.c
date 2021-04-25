#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <memory.h>
#include "video.h"
#include "hw.h"
#include "platform.h"

#define NIBBLE_SWAP(v_) ({ uint8_t v = (v_); (((v)>>4) | ((v)<<4)); })

static uint32_t *g_screen_ptr;
static int g_screen_pitch;


static void draw_sprite_4bpp(uint8_t *src, uint32_t *pal, int x, int y, int w, int h) {
	uint32_t *dst = g_screen_ptr + y*g_screen_pitch/4 + x;

	for (int j=0;j<h;j++) {
		uint32_t *l = dst;
		for (int i=0;i<w;i+=2) {
			uint8_t px = *src++;
			if (px>>4) l[0] = pal[px>>4];
			if (px&0xF) l[1] = pal[px&0xF];
			l+=2;
		}
		dst += g_screen_pitch/4;
	}
}

static void draw_sprite_4bpp_clip(uint8_t *src, uint32_t *pal, int x0, int y0, int w, int h, bool flipx, bool flipy) {
	int src_y_inc = w/2, src_x_inc = 1, src_bpp_flip=0;
	if (flipy) {
		src += w*(h-1)/2;
		src_y_inc = -src_y_inc;
	}
	if (flipx) {
		src += w/2-1;
		src_x_inc = -1;
		src_bpp_flip = 1;
	}

	int y = y0;
	for (int j=0;j<h;j++) {
		y &= 511;
		if (y >= 0 && y < 224) {
			int x = x0 & 511;
			uint32_t *l = g_screen_ptr + y*g_screen_pitch/4;
			uint8_t *srcline = src;

			for (int i=0;i<w;i+=2) {
				uint8_t px = *srcline;
				int xa = (x+(0^src_bpp_flip)) & 511, xb = (x+(1^src_bpp_flip)) & 511;

				if (px>>4) if (xa >= 0 && xa < 320) l[xa] = pal[px>>4];
				if (px&0xF) if (xb >= 0 && xb < 320) l[xb] = pal[px&0xF];
				srcline += src_x_inc;

				x+=2;
			}
		}
		src += src_y_inc;
		y++;
	}
}


static void fixrom_preprocess(uint8_t *rom, int sz) {
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

static void render_fix(void) {
	uint16_t *fix = VIDEO_RAM + 0x7000;

	fix += 32; // skip first column
	for (int i=0;i<38;i++) {
		fix += 2; // skip two lines
		for (int j=0;j<28;j++) {
			uint16_t v = *fix++;

			uint8_t *src = CUR_S_ROM + (v & 0xFFF)*8*4;
			uint32_t *pal = CUR_PALETTE_RAM + ((v >> 8) & 0xF0);

			draw_sprite_4bpp(src, pal, i*8, j*8, 8, 8);
		}
		fix += 2;
	}
}


static void render_sprites(void) {
	int sx = 0, sy = 0, ss = 0;

	for (int snum=0;snum<381;snum++) {
		// uint16_t zc = VIDEO_RAM[0x8000 + snum];
		uint16_t yc = VIDEO_RAM[0x8200 + snum];
		uint16_t xc = VIDEO_RAM[0x8400 + snum];
		uint16_t *tmap = VIDEO_RAM + snum*64;

		if (!(yc & 0x40)) {
			sx = xc >> 7;
			sy = 496 - (yc >> 7);
			ss = yc & 0x3F;
		} else {
			sx += 16;
		}

		if (ss == 0) continue;

		debugf("[VIDEO] sprite snum:%d xc:%04x yc:%04x pos:%d,%d ss:%d chain:%d tmap:%04x:%04x\n", snum, xc, yc, sx, sy, ss, (yc & 0x40), tmap[0], tmap[1]);

		for (int i=0;i<ss;i++) {
			uint32_t tnum = *tmap++;
			uint32_t tc = *tmap++;

			tnum |= (tc << 12) & 0xF0000;

			uint8_t *src = C_ROM + tnum*8*16;
			uint32_t *pal = CUR_PALETTE_RAM + ((tc >> 4) & 0xFF0);

			draw_sprite_4bpp_clip(src, pal, sx, sy+i*16, 16, 16, tc&1, tc&2);
		}
	}
}



void video_render(uint32_t* screen, int pitch) {
	g_screen_ptr = screen;
	g_screen_pitch = pitch;

	for (int y=0;y<224;y++)
		memset(screen + y*pitch/4, 0, 320*4);

	render_sprites();
	render_fix();
}

void video_palette_w(uint32_t address, uint32_t val, int sz) {
	if (sz==4) {
		video_palette_w(address+0, val >> 16, 2);
		video_palette_w(address+2, val & 0xFFFF, 2);
		return;
	}

	assert(sz == 2);
	address &= 0x1FFF;
	address /= 2;

	uint32_t r = ((val >> 6) & 0x3C) | ((val >> 13) & 2) | (val >> 15);
	uint32_t g = ((val >> 2) & 0x3C) | ((val >> 12) & 2) | (val >> 15);
	uint32_t b = ((val << 2) & 0x3C) | ((val >> 11) & 2) | (val >> 15);

	r = (r<<2) | (r>>4);
	g = (g<<2) | (g>>4);
	b = (b<<2) | (b>>4);

	CUR_PALETTE_RAM[address] = (r << 16) | (g << 8) | b;
}

uint32_t video_palette_r(uint32_t address, int sz) {
	assert(sz == 2);
	address &= 0x1FFF;
	address /= 2;

	uint32_t color = CUR_PALETTE_RAM[address];
	uint16_t c16 = ((color >> 12) & 0xF00) | ((color >> 8) & 0xF0) | ((color >> 4) & 0xF);

	c16 |= (color >> 5) & 0x4000;
	c16 |= (color << 2) & 0x2000;
	c16 |= (color << 9) & 0x1000;

	c16 |= (color >> 3) & 0x8000; // dark bit, should be identical across all colors

	return c16;
}

void video_init(void) {
	// Preprocess SROMs
	fixrom_preprocess(SFIX_ROM, sizeof(SFIX_ROM));
	fixrom_preprocess(S_ROM, sizeof(S_ROM));

	crom_preprocess(C_ROM, C_ROM_SIZE);
}
