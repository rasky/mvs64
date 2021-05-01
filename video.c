#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <memory.h>
#include "video.h"
#include "roms.h"
#include "hw.h"
#include "platform.h"

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

			uint8_t *src = crom_get_sprite(tnum);
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

#include "m68k.h"

uint32_t video_palette_r(uint32_t address, int sz) {
	assertf(sz == 2, "video_palette_r access size %d %x %lx", sz, m68k_get_reg(NULL, M68K_REG_PC), C0_READ_EPC());
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
