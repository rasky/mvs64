#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <memory.h>
#include "video.h"
#include "roms.h"
#include "hw.h"
#include "platform.h"

// Magic table to calculate pixel-perfect vertical shrinking.
// This table can be thought of a condensed version of the original
// NeoGeo L0 ROM, but we just need 16 bytes to achieve the same results.
// The table is then duplicated (and mirrored) to 32 bytes to simplify
// lookup for tiles 16-31 (where the L0 ROM is read backwards).
// To see how it's calculated, see l0.py.
static const uint8_t VSHRINK_MAGIC[32] = { 
	1, 9, 5, 13, 3, 11, 7, 15, 0, 8, 4, 12, 2, 10, 6, 14,
	14, 6, 10, 2, 12, 4, 8, 0, 15, 7, 11, 3, 13, 5, 9, 1
};


// Given a vshrink code and the tile number, compute the tile
// height in pixels. For instance:
// vshrink_tile_height(0xBC, 4) == 12 means that when using
// vshrink code 0xBC, the fifth tile of a sprite will be exactly
// 12 pixel tall (shrunk from 16).
static inline int vshrink_tile_height(int vshrink, int num_tile) {
	vshrink += 1;
	return vshrink/16 + (vshrink%16 > VSHRINK_MAGIC[num_tile]);
}

// Given a tile of the specified height and y in [0,15],
// return True if the given line is drawn, or False if
// it should be skipped.
static inline bool vshrink_line_drawn(int height, int y) {
	return height > VSHRINK_MAGIC[y];
}

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
	int sx = 0, sy = 0, sh = 0, sw = 0, vshrink = 0;
	bool repeat_tiles = false;

	uint8_t aa;
	bool aa_enabled = lspc_get_auto_animation(&aa);

	render_begin_sprites();

	for (int snum=0;snum<381;snum++) {
		uint16_t zc = VIDEO_RAM[0x8000 + snum];
		uint16_t yc = VIDEO_RAM[0x8200 + snum];
		uint16_t xc = VIDEO_RAM[0x8400 + snum];
		uint16_t *tmap = VIDEO_RAM + snum*64;

		if (!(yc & 0x40)) {
			sx = xc >> 7;
			sy = 496 - (yc >> 7);
			sh = (yc & 0x3F) * 16;
			repeat_tiles = false;
			if (sh > 32*16) { sh = 32*16; repeat_tiles = true; };
			vshrink = zc & 0xFF;
		} else {
			sx += sw;
		}

		sw = ((zc>>8)&0xF) + 1;

		if (sh == 0) continue;
		if (sx >= 320 && sx+sw <= 512) continue;

		// debugf("[VIDEO] sprite snum:%d xc:%04x yc:%04x zc:%04x pos:%d,%d sh:%d chain:%d repeat:%d tmap:%04x:%04x\n", snum, xc, yc, zc, sx, sy, sh, (yc & 0x40), repeat_tiles, tmap[0], tmap[1]);

		int nt, y, maxy;
		int halfy = sh < 256 ? sh : 256;

		// Iterate on the two halves of the vertical sprite. This for loop
		// is mainly useful to reuse the core drawing loop. The setup
		// of the two halves is different (see below).
		for (int half = 0; half < 2; half++) {
			if (half == 0) {
				// Top half of the sprite (first 256 pixels). This part shrinks
				// to the top of the sprite position. In case of overfill, this
				// is exactly 256 pixels, repeating all tiles as required.
				maxy = halfy;
				nt = y = 0;
			} else {
				if (sh <= 256) break;

				// Bottom half of the sprite (pixels after 256). This part shrinks
				// to the bottom of the sprite (because it accesses the line ROM
				// backward, reversing also its contents).
				maxy = sh;
				if (repeat_tiles) {
					// In repeat mode, we need to find a starting Y where the next
					// tile begins, which is basically symmetric across the 256 pixel
					// line compared to where we ended up with the top half.
					// FIXME: overdraw here, we should instead clip.
					y -= (y-256)*2;
					nt = (32-nt)&31;
				} else {
					// In non-repeat mode, skip overfill area. We basically want
					// to reach the symmetric y coordinate in the bottom area.
					// FIXME: the top half overfill area should be filled with
					// the last line of tile #15, while the bottom half overfill
					// should be filled with the first line of tile #16. This
					// is currently not implemented.
					y = 32*16 - y;
				}
			}

			// Loop through the vertical sprite, tile by tile
			while (y < maxy) {
				// Calculate the vertical size of this tile. This is
				// a pixel-perfect formula using the magic table derived
				// from the original NeoGeo L0 ROM.
				// int ssh = vshrink/16 + (vshrink%16 > VSHRINK_MAGIC[nt]);
				int ssh = vshrink_tile_height(vshrink, nt);

				if (ssh > 0) {
					// vertical clip of the tile to the total sprite height
					// FIXME: this is wrong because ssh also affects the
					// shrinking size of the sprite. We should separate the
					// two matters.
					if (y + ssh > sh)
						ssh = sh - y;

					// See if this tile is visible, given its Y coordinate and size
					int ssy = sy + y;
					if (ssy < 224 || (ssy+ssh) > 512) {
						uint32_t tnum = tmap[nt*2+0];
						uint32_t tc = tmap[nt*2+1];

						tnum |= (tc << 12) & 0xF0000;
						int palnum = ((tc >> 8) & 0xFF);

						// debugf("[VIDEO]   %s: nt:%d y:%d ssy:%d ssh:%d tnum:%x\n", half?"bot":"top", nt, y, ssy, ssh, tnum);

						// Auto animation
						if (aa_enabled) {
							if (tc & 8)      { tnum &= ~7; tnum |= aa & 7; }
							else if (tc & 4) { tnum &= ~3; tnum |= aa & 3; }
						}

						// Draw the tile
						draw_sprite(tnum, palnum, sx, ssy, sw, ssh, tc&1, tc&2);
					}
				}

				y += ssh;
				nt++; nt &= 31;

				// In non-repeat mode (standard), the top half
				// finishes when/if we reach tile #16 (or before, if
				// the vertical sprite size is reached).
				if (!repeat_tiles && nt == 16) break;  // FIXME: draw overfill when not repeating
			}
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
