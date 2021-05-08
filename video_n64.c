
#include <libdragon.h>
#include "lib/rdl.h"

#define NUM_DISPLAY_LISTS  3

static RdpDisplayList *dl_fix[NUM_DISPLAY_LISTS] = { NULL };
static int dl_fix_idx = 0;
static bool rdp_mode_copy = false;
static int rdp_tex_slot = 0;
static int rdp_pal_slot = 0;
static int fix_last_spritnum = 0;

static void draw_sprite(int spritenum, int palnum, int x0, int y0, bool flipx, bool flipy) {
	const int w = 16, h = 16;
	uint8_t *src = crom_get_sprite(spritenum);
	uint16_t *pal = PALETTE_RAM + PALETTE_RAM_BANK + palnum*16;

	// Convert the coordinates from [0..511] to [-16..496]
	if (x0 >= 512-16) x0 = x0-512;
	if (y0 >= 512-16) y0 = y0-512;

	data_cache_hit_writeback_invalidate(pal, 16*2);

	// See if we're reusing either a palette slot or a texture slot that was
	// previously used. If so, we need to issue a RdpSyncLoad() command before
	// reusing the same slot.
	// TODO: I'm not 100% sure about this... I think this just happens to work
	// because there are enough slots so the RDP is never using them when we get
	// back to them.
	if (rdp_pal_slot == 0 || rdp_tex_slot == 0) {
		rdl_push(dl_fix[dl_fix_idx], RdpSyncLoad());
	}

	rdl_push(dl_fix[dl_fix_idx],
		// Load the sprite pixels into the current texture slot
		RdpSyncTile(),
		MRdpLoadTex4bpp(0, (uint32_t)src, w, h, RDP_AUTO_PITCH, RDP_AUTO_TMEM_SLOT(rdp_tex_slot), RDP_AUTO_PITCH),

		// Load the palette into the current palette slot (TODO: we should maybe keep a cache here)
		RdpSyncTile(),
		MRdpLoadPalette16(2, (uint32_t)pal, RDP_AUTO_TMEM_SLOT(rdp_pal_slot)),

		// Configure the tile descriptor for drawing this sprite (texture+palette)
		RdpSyncTile(),
		MRdpSetTile4bpp(1, RDP_AUTO_TMEM_SLOT(rdp_tex_slot), RDP_AUTO_PITCH, RDP_AUTO_TMEM_SLOT(rdp_pal_slot), w, h)
	);

	rdp_pal_slot++; if (rdp_pal_slot==15) rdp_pal_slot = 0;
	rdp_tex_slot++; if (rdp_tex_slot==15) rdp_tex_slot = 0;

	// We can draw the sprites in two different modes:
	//
	// RDP 1-Cycle mode: standard polygon drawing mode (1 pixel per cycle).
	// This supports all kind of sprites transformations.
	//
	// RDP Copy mode: faster, blits 4 pixels per cycle. It doesn't support
	// clipping, flipping or scaling. For Y clipping we workaround it, but
	// anything else must fallback to RDP 1 Cycle mode.
	//
	if (flipx || flipy || x0 < 0 || (x0+w)>320) {
		int s0 = 0, t0 = 0;
		int ds = 1, dt = 1;
		if (flipx) { s0 = w; ds = -ds; }
		if (flipy) { t0 = h; dt = -dt; }

		if (rdp_mode_copy) {
			rdl_push(dl_fix[dl_fix_idx],
				RdpSyncPipe(),
			    RdpSetOtherModes(SOM_CYCLE_1 | SOM_ALPHA_COMPARE | SOM_RGBDITHER_NONE | SOM_ALPHADITHER_NONE | SOM_ENABLE_TLUT_RGB16)
			);
			rdp_mode_copy = false;
		}

		rdl_push(dl_fix[dl_fix_idx],
			RdpTextureRectangle1I(1, x0, y0, x0+w, y0+h),
		    RdpTextureRectangle2I(s0, t0, ds, dt)
		);
	} else {
		int s0 = 0, t0 = 0;
		int ds = 4, dt = 1;   // in copy mode, delta-s = 4 as RDP is blitting 4 pixels per cycle
		int sw = w, sh = h;

		if (y0 < 0) {
			t0 = -y0;
			y0 = 0;
		}
		if (y0+sh > 224) {
			sh -= y0+sh-224;
		}

		if (!rdp_mode_copy) {
			rdl_push(dl_fix[dl_fix_idx],
				RdpSyncPipe(),
			    RdpSetOtherModes(SOM_CYCLE_COPY | SOM_ALPHA_COMPARE | SOM_ENABLE_TLUT_RGB16)
			);		
			rdp_mode_copy = true;			
		}

		rdl_push(dl_fix[dl_fix_idx],
		    RdpTextureRectangle1I(1, x0, y0, x0+sw-1, y0+sh-1),
		    RdpTextureRectangle2I(s0, t0, ds, dt)
		);
	}

	if (rdl_nempty(dl_fix[dl_fix_idx]) < 32) {	
		debugf("FLUSH sprite intermediate\n");
		rdl_flush(dl_fix[dl_fix_idx]);
		rdl_exec(dl_fix[dl_fix_idx]);
		dl_fix_idx++; if (dl_fix_idx==NUM_DISPLAY_LISTS) dl_fix_idx=0;
		rdl_reset(dl_fix[dl_fix_idx]);
	}
}

static void render_begin_sprites(void) {
	static RdpDisplayList *dl = NULL;
	if (!dl) dl=rdl_heap_alloc(128);

	rdl_reset(dl);
	rdl_push(dl,
		RdpSyncPipe(),
		// By default, go into COPY mode which is used by most sprites
		RdpSetOtherModes(SOM_CYCLE_COPY | SOM_ALPHA_COMPARE | SOM_ENABLE_TLUT_RGB16),
		// Setup combiner for texture output without lighting. We need 1-cycle
		// mode for sprites with scaling or flipping, so the combiner must be
		// configured.
		RdpSetCombine(Comb1_Rgb(ZERO, ZERO, ZERO, TEX0), Comb1_Alpha(ZERO, ZERO, ZERO, TEX0)),
	    RdpSetBlendColor(0x0001) // alpha threshold value = 1
	);

	rdl_flush(dl);
	rdl_exec(dl);
	rdp_mode_copy = true;
	rdp_pal_slot = 0;
}

static void render_end_sprites(void) {
	rdl_flush(dl_fix[dl_fix_idx]);
	rdl_exec(dl_fix[dl_fix_idx]);
	dl_fix_idx++; if (dl_fix_idx==NUM_DISPLAY_LISTS) dl_fix_idx=0;
	rdl_reset(dl_fix[dl_fix_idx]);
}

static void draw_sprite_fix(int spritenum, int palnum, int x, int y) {
	uint8_t *src = srom_get_sprite(spritenum);

	// HACK: most of the fix layer is normally empty. Unfortunately "empty"
	// means a tile whose pixels are 0, which is something that might be
	// expensive to check at runtime. So for now we skip at least TMEM loading
	// when the previous tile is the same, which normally triggers for the
	// empty tile.
	if (spritenum != fix_last_spritnum) {
		fix_last_spritnum = spritenum;

		rdl_push(dl_fix[dl_fix_idx],
			RdpSyncTile(),
			RdpSyncLoad(),
			MRdpLoadTex4bpp(0, (uint32_t)src, 8, 8, RDP_AUTO_PITCH, RDP_AUTO_TMEM_SLOT(0), RDP_AUTO_PITCH)
		);
	}

	rdl_push(dl_fix[dl_fix_idx],
		RdpSyncTile(),
		MRdpSetTile4bpp(1, RDP_AUTO_TMEM_SLOT(0), RDP_AUTO_PITCH, RDP_AUTO_TMEM_SLOT(palnum), 8, 8),
		MRdpTextureRectangle4bpp(1, x, y, 8, 8)
	);

	if (rdl_nempty(dl_fix[dl_fix_idx]) < 32) {
		rdl_flush(dl_fix[dl_fix_idx]);
		rdl_exec(dl_fix[dl_fix_idx]);
		dl_fix_idx++; if (dl_fix_idx==NUM_DISPLAY_LISTS) dl_fix_idx=0;
		rdl_reset(dl_fix[dl_fix_idx]);
	}
}

static void render_begin_fix(void) {
	static RdpDisplayList *dl = NULL;
	if (!dl) dl=rdl_heap_alloc(128);

	rdl_reset(dl);
	rdl_push(dl,
		// Prepare for fix layer
		RdpSyncPipe(),
    	RdpSetOtherModes(SOM_CYCLE_COPY | SOM_ALPHA_COMPARE | SOM_ENABLE_TLUT_RGB16),
	    RdpSetBlendColor(0x0001) // alpha threshold value = 1
	);

	// Load all 16 palettes right away. They fit TMEM, so that we don't need
	// to load them while we process
	data_cache_hit_writeback_invalidate(PALETTE_RAM + PALETTE_RAM_BANK, 16*16*2);
	for (int i=0;i<16;i++) {		
		rdl_push(dl, MRdpLoadPalette16(i&7, (uint32_t)(PALETTE_RAM + PALETTE_RAM_BANK + i*16), RDP_AUTO_TMEM_SLOT(i)));
		if (i==7) rdl_push(dl, RdpSyncTile());
	}
	    
	rdl_flush(dl);
	rdl_exec(dl);

	fix_last_spritnum = -1;
}

static void render_end_fix(void) {
	debugf("render_end_fix\n");
	rdl_flush(dl_fix[dl_fix_idx]);
	rdl_exec(dl_fix[dl_fix_idx]);
	dl_fix_idx++; if (dl_fix_idx==NUM_DISPLAY_LISTS) dl_fix_idx=0;
	rdl_reset(dl_fix[dl_fix_idx]);
}

static void render_begin(void) {
	RdpDisplayList *dl = rdl_stack_alloc(16);

	rdl_push(dl,
		RdpSetOtherModes(SOM_CYCLE_FILL),
		RdpSetFillColor16(PALETTE_RAM[PALETTE_RAM_BANK + 0xFFF]),
		RdpFillRectangleI(0, 0, 320, 240)
	);

	rdl_flush(dl);
	rdl_exec(dl);

	if (!dl_fix[0]) {
		for (int i=0;i<NUM_DISPLAY_LISTS;i++)
			dl_fix[i] = rdl_heap_alloc(2048);
	}
	dl_fix_idx = 0;
	for (int i=0;i<NUM_DISPLAY_LISTS;i++)
		rdl_reset(dl_fix[i]);
}

static void render_end(void) {
}
