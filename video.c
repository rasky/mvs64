#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <memory.h>
#include "video.h"
#include "roms.h"
#include "hw.h"
#include "platform.h"

#ifdef N64
	#if 1
	#include "video_n64.c"
	#else
	#include "video_cpu.c"
	#endif
#else
#include "video_cpu.c"
#endif

static uint8_t PALETTE_DARK_BITS[8*1024/8];

static void render_fix(void) {
	uint16_t *fix = VIDEO_RAM + 0x7000;

	render_begin_fix();

	for (int i=0;i<40;i++) {
		fix += 2; // skip two lines
		for (int j=0;j<28;j++) {
			uint16_t v = *fix++;
			if (v)
				draw_sprite_fix(v & 0xFFF, (v >> 12) & 0xF, i*8, j*8);
		}
		fix += 2;
	}

	render_end_fix();
}


static void render_sprites(void) {
	int sx = 0, sy = 0, ss = 0;

	uint8_t aa;
	bool aa_enabled = lspc_get_auto_animation(&aa);

	render_begin_sprites();

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

		int w = 16, h = 16;

		if (sx >= 320 && sx+w <= 512) continue;

		// debugf("[VIDEO] sprite snum:%d xc:%04x yc:%04x pos:%d,%d ss:%d chain:%d tmap:%04x:%04x\n", snum, xc, yc, sx, sy, ss, (yc & 0x40), tmap[0], tmap[1]);

		for (int i=0;i<ss;i++) {
			int ssy = (sy+i*16) & 511;
			uint32_t tnum = *tmap++;
			uint32_t tc = *tmap++;

			if (ssy >= 224 && ssy+h <= 512) continue;

			tnum |= (tc << 12) & 0xF0000;
			int palnum = ((tc >> 8) & 0xFF);

			if (aa_enabled) {
				if (tc & 8)      { tnum &= ~7; tnum |= aa & 7; }
				else if (tc & 4) { tnum &= ~3; tnum |= aa & 3; }
			}

			draw_sprite(tnum, palnum, sx, ssy, tc&1, tc&2);
		}
	}

	render_end_sprites();
}



void video_render(void) {
	render_begin();
	render_sprites();
	render_fix();
	render_end();
}

void video_palette_w(uint32_t address, uint32_t val, int sz) {
	if (sz == 4) {
		video_palette_w(address+0, val >> 16, 2);
		video_palette_w(address+2, val & 0xFFFF, 2);
		return;
	}

	if (sz == 1) val |= val << 8;  // FIXME: this is used by unibios in-game menu, verify
	address &= 0x1FFF;
	address /= 2;
	address += PALETTE_RAM_BANK;

	uint16_t c16 = 0;

	c16 |= ((val & 0x0F00) << 4) | ((val & 0x4000) >> 3);
	c16 |= ((val & 0x00F0) << 3) | ((val & 0x2000) >> 7);
	c16 |= ((val & 0x000F) << 2) | ((val & 0x1000) >> 11);

	// All colors but index 0 of each palette have alpha set to 1.
	if (address & 15) c16 |= 1;

	// Store dark bit in a parallel data structure. This is required in case
	// the game reads back the pallette RAM, to reconstruct the correct color.
	PALETTE_DARK_BITS[address/8] &= ~(1 << (address%8));
	PALETTE_DARK_BITS[address/8] |= (val>>15) << (address%8);

	PALETTE_RAM[address] = c16;
}

uint32_t video_palette_r(uint32_t address, int sz) {
	if (sz==4)
		return (video_palette_r(address+0, 2) << 16) | video_palette_r(address+2, 2);

	assertf(sz == 2, "video_palette_r access size %d", sz);
	address &= 0x1FFF;
	address /= 2;
	address += PALETTE_RAM_BANK;

	uint16_t c16 = PALETTE_RAM[address];

	uint16_t val = 0;

	val |= ((c16 >> 4) & 0x0F00) | ((c16 << 3) & 0x4000);
	val |= ((c16 >> 3) & 0x00F0) | ((c16 << 7) & 0x2000);
	val |= ((c16 >> 2) & 0x000F) | ((c16 << 11) & 0x1000);
	val |= (PALETTE_DARK_BITS[address/8] >> (address%8)) << 15;

	return val;
}
