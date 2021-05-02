
#include <libdragon.h>
#include "lib/rdl.h"

static RdpDisplayList *dl_fix[2] = { NULL };
static int dl_fix_idx = 0;

static void draw_sprite(int spritenum, int palnum, int x0, int y0, bool flipx, bool flipy) {
	const int w = 16, h = 16;
	uint8_t *src = crom_get_sprite(spritenum);
	uint16_t *pal = PALETTE_RAM + PALETTE_RAM_BANK + palnum*16;

	data_cache_hit_writeback_invalidate(pal, 16*2);

	rdl_reset(dl_fix[dl_fix_idx]);

	rdl_push(dl_fix[dl_fix_idx],
		RdpSyncTile(),
		RdpSyncLoad(),
		MRdpLoadTex4bpp(0, (uint32_t)src, w, h, RDP_AUTO_PITCH, RDP_AUTO_TMEM_SLOT(0), RDP_AUTO_PITCH),

		RdpSyncTile(),
		RdpSyncLoad(),
		MRdpLoadPalette16(2, (uint32_t)pal, RDP_AUTO_TMEM_SLOT(0))
	);

	rdl_push(dl_fix[dl_fix_idx],
		RdpSyncLoad(),
		RdpSyncTile(),
		MRdpSetTile4bpp(1, RDP_AUTO_TMEM_SLOT(0), RDP_AUTO_PITCH, RDP_AUTO_TMEM_SLOT(0), w, h)
	);

	if (flipx || flipy) {	
		int s0 = 0, t0 = 0;
		int ds = 1, dt = 1;
		if (flipx) { s0 = w; ds = -ds; }
		if (flipy) { t0 = h; dt = -dt; }

		rdl_push(dl_fix[dl_fix_idx],
		    RdpSetOtherModes(SOM_CYCLE_1 | SOM_ALPHA_COMPARE | SOM_RGBDITHER_NONE | SOM_ALPHADITHER_NONE | SOM_ENABLE_TLUT_RGB16),
			RdpTextureRectangle1I(1, x0, y0, x0+w, y0+h),
		    RdpTextureRectangle2I(s0, t0, ds, dt)
		);
	} else {
		rdl_push(dl_fix[dl_fix_idx],
		    RdpSetOtherModes(SOM_CYCLE_COPY | SOM_ALPHA_COMPARE | SOM_ENABLE_TLUT_RGB16),
			MRdpTextureRectangle4bpp(1, x0, y0, 16, 16)
		);		
	}

	rdl_flush(dl_fix[dl_fix_idx]);
	rdl_exec(dl_fix[dl_fix_idx]);
	dl_fix_idx ^= 1;
}

static void render_begin_sprites(void) {
	RdpDisplayList *dl = rdl_stack_alloc(64);

	rdl_push(dl,
		// Setup combiner for texture output without lighting. We need 1-cycle
		// mode for sprites with scaling or flipping, so the combiner must be
		// configured.
		RdpSetCombine(Comb1_Rgb(ZERO, ZERO, ZERO, TEX0), Comb1_Alpha(ZERO, ZERO, ZERO, TEX0)),
	    RdpSetBlendColor(0x0001) // alpha threshold value = 1
	);

	rdl_flush(dl);
	rdl_exec(dl);
}

static void render_end_sprites(void) {}

static void draw_sprite_fix(int spritenum, int palnum, int x, int y) {
	uint8_t *src = srom_get_sprite(spritenum);

	rdl_reset(dl_fix[dl_fix_idx]);

	rdl_push(dl_fix[dl_fix_idx],
		RdpSyncTile(),
		RdpSyncLoad(),
		MRdpLoadTex4bpp(0, (uint32_t)src, 8, 8, RDP_AUTO_PITCH, RDP_AUTO_TMEM_SLOT(0), RDP_AUTO_PITCH),

		RdpSyncLoad(),
		RdpSyncTile(),
		MRdpSetTile4bpp(1, RDP_AUTO_TMEM_SLOT(0), RDP_AUTO_PITCH, RDP_AUTO_TMEM_SLOT(palnum), 8, 8),
		MRdpTextureRectangle4bpp(1, x, y, 8, 8)
	);

	rdl_flush(dl_fix[dl_fix_idx]);
	rdl_exec(dl_fix[dl_fix_idx]);
	dl_fix_idx ^= 1;
}

static void render_begin_fix(void) {
	RdpDisplayList *dl = rdl_stack_alloc(64);

	rdl_push(dl,
		// Prepare for fix layer
    	RdpSetOtherModes(SOM_CYCLE_COPY | SOM_ALPHA_COMPARE | SOM_ENABLE_TLUT_RGB16),
	    RdpSetBlendColor(0x0001) // alpha threshold value = 1
	);

	// Load all 16 palettes right away. They fit TMEM, so that we don't need
	// to load them while we process
	data_cache_hit_writeback_invalidate(PALETTE_RAM + PALETTE_RAM_BANK, 16*16*2);
	for (int i=0;i<16;i++) {		
		rdl_push(dl, MRdpLoadPalette16(i&7, (uint32_t)(PALETTE_RAM + PALETTE_RAM_BANK + i*16), RDP_AUTO_TMEM_SLOT(i)));
		if (i == 7) rdl_push(dl, RdpSyncTile());
	}
	    
	rdl_flush(dl);
	rdl_exec(dl);
}

static void render_end_fix(void) {}

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
		dl_fix[0] = rdl_heap_alloc(1024);
		dl_fix[1] = rdl_heap_alloc(1024);
	}
	dl_fix_idx = 0;
	rdl_reset(dl_fix[dl_fix_idx]);
}

static void render_end(void) {
}
