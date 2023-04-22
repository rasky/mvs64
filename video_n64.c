
#include <libdragon.h>

#define RSP_FIX_LAYER    1

extern uint32_t RSP_OVL_ID;

static bool rdp_mode_copy = false;
static int rdp_tex_slot = 0;
static int rdp_pal_slot = 0;
static int fix_last_spritnum = 0;
static int fix_last_palnum = -1;
static int pal_slot_cache[16];

static void draw_sprite(int spritenum, int palnum, int x0, int y0, int sw, int sh, bool flipx, bool flipy) {
	static const int16_t scale_fx[17] = { 0, (16<<10)/1, (16<<10)/2, (16<<10)/3, (16<<10)/4, (16<<10)/5, (16<<10)/6, (16<<10)/7, (16<<10)/8, (16<<10)/9, (16<<10)/10, (16<<10)/11, (16<<10)/12, (16<<10)/13, (16<<10)/14, (16<<10)/15, (16<<10)/16 };
	uint8_t *src = crom_get_sprite(spritenum);
	uint16_t *pal = PALETTE_RAM_EMU + palnum*16;

	// Convert the coordinates from [0..511] to [-16..496]
	if (x0 >= 512-16) x0 = x0-512;
	if (y0 >= 512-16) y0 = y0-512;

	// Search if we have already loaded this palette. If so, skip loading it.
	int pal_slot;
	for (pal_slot=0;pal_slot<16;pal_slot++) {
		if (pal_slot_cache[pal_slot] == palnum)
			break;
	}
	if (pal_slot == 16) {
		// Load the palette.
		data_cache_hit_writeback_invalidate(pal, 16*2);

		// Select slot to reuse (TODO: should be LRU or random)
		pal_slot = rdp_pal_slot++;
		if (rdp_pal_slot == 16) rdp_pal_slot = 0;

		rdpq_tex_load_tlut(pal, pal_slot*16, 16);
		pal_slot_cache[pal_slot] = palnum;
	}

	const int pitch = 8;
	const int tmem_addr = rdp_tex_slot * 16 * 8;
	rdpq_set_texture_image_raw(0, PhysicalAddr(src), FMT_RGBA16, 16/4, 16);
	rdpq_set_tile(TILE1, FMT_RGBA16, tmem_addr, 0, 0);
	rdpq_set_tile(TILE0, FMT_CI4, tmem_addr, pitch, &(rdpq_tileparms_t){ .palette = pal_slot });
	rdpq_set_tile_size(TILE0, 0, 0, 16, 16);
	rdpq_load_block(TILE1, 0, 0, 16*16/4, pitch);

	if (++rdp_tex_slot==8) rdp_tex_slot = 0;

	// We can draw the sprites in two different modes:
	//
	// RDP 1-Cycle mode: standard polygon drawing mode (1 pixel per cycle).
	// This supports all kind of sprites transformations.
	//
	// RDP Copy mode: faster, blits 4 pixels per cycle. It doesn't support
	// clipping, flipping or scaling. For Y clipping we workaround it, but
	// anything else must fallback to RDP 1 Cycle mode.
	//
	if (flipx || flipy || x0 < 0 || (x0+sw)>320 || sw != 16 || sh != 16) {
		int s0 = 0, t0 = 0;
		int ds = scale_fx[sw], dt = scale_fx[sh];

		if (x0 < 0) { s0 = -x0; sw -= s0; x0 = 0; }
		if (y0 < 0) { t0 = -y0; sh -= t0; y0 = 0; }

		if (flipx) { s0 = 16-s0; ds = -ds; }
		if (flipy) { t0 = 16-t0; dt = -dt; }

		if (rdp_mode_copy) {
			rdpq_set_mode_standard();
			rdpq_mode_tlut(TLUT_RGBA16);
			rdpq_mode_alphacompare(1);
			rdp_mode_copy = false;
		}

		rdpq_texture_rectangle_raw(
			TILE0, x0, y0, x0+sw, y0+sh,
			s0, t0, ds*(1.0f / 1024.f), dt*(1.0f / 1024.f));
	} else {
		int s0 = 0, t0 = 0;
		int ds = 1, dt = 1;
		int sw = 16, sh = 16;

		if (y0 < 0) { t0 = -y0; sh -= t0; y0 = 0; }
		if (y0+sh > 224) { sh -= y0+sh-224; }

		if (!rdp_mode_copy) {
			rdpq_set_mode_copy(true);
			rdpq_mode_tlut(TLUT_RGBA16);
			rdp_mode_copy = true;			
		}

		rdpq_texture_rectangle_raw(
			TILE0, x0, y0, x0+sw, y0+sh,
			s0, t0, ds, dt);
	}
}

static void render_begin_sprites(void) {
	rdpq_set_mode_copy(true);
	rdpq_mode_tlut(TLUT_RGBA16);

	rdp_mode_copy = true;
	rdp_pal_slot = 0;
	for (int i=0;i<16;i++) pal_slot_cache[i] = -1;
}

static void render_end_sprites(void) {}

static void rsp_fix_init(void) {
	rspq_write(RSP_OVL_ID, 0x0);
}
static void rsp_fix_draw(uint8_t *src, int palnum, int x, int y) {
	rspq_write(RSP_OVL_ID, 0x1, PhysicalAddr(src), 
		(palnum << 20) | (x << 10) | y);
}
static void rsp_pal_convert(uint16_t *src, uint16_t *dst) {
	rspq_write(RSP_OVL_ID, 0x3, PhysicalAddr(src), PhysicalAddr(dst));
}

#define FIX_TMEM_ADDR 	0
#define FIX_TMEM_PITCH  8

static void draw_sprite_fix(int spritenum, int palnum, int x, int y) {
	if (RSP_FIX_LAYER) {
		uint8_t *src = NULL;
		if (spritenum != fix_last_spritnum) {
			fix_last_spritnum = spritenum;
			src = srom_get_sprite(spritenum);
		}
		rsp_fix_draw(src, palnum, x, y);
		return;
	}

	// HACK: most of the fix layer is normally empty. Unfortunately "empty"
	// means a tile whose pixels are 0, which is something that might be
	// expensive to check at runtime. So for now we skip at least TMEM loading
	// when the previous tile is the same, which normally triggers for the
	// empty tile.
	if (spritenum != fix_last_spritnum) {
		fix_last_spritnum = spritenum;

		uint8_t *src = srom_get_sprite(spritenum);

		// We can't use LOAD_BLOCK for a 8x8 CI4 sprite, because the TMEM
		// pitch must be 8 bytes minimum.
		rdpq_set_texture_image_raw(0, PhysicalAddr(src), FMT_CI8, 8/2, 8);
		rdpq_load_tile(TILE1, 0, 0, 4, 8);
	}

	if (palnum != fix_last_palnum) {
		fix_last_palnum = palnum;
		rdpq_set_tile(TILE0, FMT_CI4, FIX_TMEM_ADDR, FIX_TMEM_PITCH, &(rdpq_tileparms_t){ .palette = palnum });;
		rdpq_set_tile_size(TILE0, 0, 0, 8, 8);
	}

	rdpq_texture_rectangle_raw(TILE0, x, y, x+8, y+8, 0, 0, 1, 1);
}

static void render_begin_fix(void) {
	rdpq_set_mode_copy(true);
	rdpq_mode_tlut(TLUT_RGBA16);

	// Load all 16 palettes right away. They fit TMEM, so that we don't need
	// to load them while we process
	rdpq_tex_load_tlut(PALETTE_RAM_EMU, 0, 256);

	// Configure tiles once
	rdpq_set_tile(TILE0, FMT_CI4, FIX_TMEM_ADDR, FIX_TMEM_PITCH, 0);  // used for drawing
	rdpq_set_tile(TILE1, FMT_CI8, FIX_TMEM_ADDR, FIX_TMEM_PITCH, 0);  // used for loading
	rdpq_set_tile_size(TILE0, 0, 0, 8, 8);

	fix_last_spritnum = -1;
	fix_last_palnum = -1;

	if (RSP_FIX_LAYER)
		rsp_fix_init();
}

static void render_end_fix(void) {}

static void render_begin(void) {
	data_cache_hit_writeback(PALETTE_RAM + PALETTE_RAM_BANK, 4096*2);
	for (int i=0; i<4096 / 0x400; i++) {
		rsp_pal_convert(PALETTE_RAM + PALETTE_RAM_BANK + i*0x400, PALETTE_RAM_EMU + i*0x400);
	}

	uint16_t bkg = color_convert(PALETTE_RAM[PALETTE_RAM_BANK+0xFFF]) | 1;

	// Clear the screen
	// rdpq_debug_log(true);
	rdpq_set_mode_fill(color_from_packed16(bkg));
	rdpq_fill_rectangle(0, 0, 320, 240);
	rspq_flush();
}

static void render_end(void) {
	rspq_flush();
	// rdpq_debug_log(false);
}
