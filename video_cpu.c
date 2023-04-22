
static uint8_t hscale[16][16];
static bool hscale_init = false;

static void draw_sprite_fix(int spritenum, int palnum, int x, int y) {
	const int w=8, h=8;
	uint16_t *dst = (uint16_t*)g_screen_ptr + y*g_screen_pitch/2 + x;
	uint8_t *src = srom_get_sprite(spritenum);
	uint16_t *pal = PALETTE_RAM_EMU + palnum*16;

	for (int j=0;j<h;j++) {
		uint16_t *l = dst;
		for (int i=0;i<w;i+=2) {
			uint8_t px = *src++;
			if (px>>4) l[0] = pal[px>>4];
			if (px&0xF) l[1] = pal[px&0xF];
			l+=2;
		}
		dst += g_screen_pitch/2;
	}
}

static void render_begin_sprites(void) {
	if (!hscale_init) {	
		const uint64_t hbits = 0x5b1d7f39a06e2c48ull;
		memset(hscale, 0, 16*16);
		for (int y=0;y<16;y++)
			for (int x=y; x>=0; x--)
				hscale[y][(hbits>>(4*x))&0xF] = 1;
		hscale_init = true;
	}
}

static void render_end_sprites(void) {}


static void draw_sprite(int spritenum, int palnum, int x0, int y0, int sw, int sh, bool flipx, bool flipy) {
	const int w = 16, h = 16; 
	uint8_t *src = crom_get_sprite(spritenum);
	uint16_t *pal = PALETTE_RAM_EMU + palnum*16;

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
		if (vshrink_line_drawn(sh, j)) {
			y &= 511;
			if (y >= 0 && y < 224) {
				int x = x0 & 511;
				uint16_t *l = (uint16_t*)g_screen_ptr + y*g_screen_pitch/2;
				uint8_t *srcline = src;
				uint8_t *hs = hscale[sw-1];

				for (int i=0;i<w;i++) {
					if (!hs[i]) continue;
					uint8_t px = (i^src_bpp_flip)&1 ? srcline[i/2*src_x_inc]&0xF : srcline[i/2*src_x_inc]>>4;
					if (px && x>=0 && x<320) l[x] = pal[px];
					x++; x&=511;
				}
			}
			y++;
		}
		src += src_y_inc;
	}
}

static void render_begin_fix(void) {}
static void render_end_fix(void) {}

static void render_begin(void) {
	for (int i=0; i<4096; i++) {
		uint16_t val = PALETTE_RAM[PALETTE_RAM_BANK + i];
		uint16_t c16 = color_convert(val);

		// All colors but index 0 of each palette have alpha set to 1.
		if (i & 15) c16 |= 1;

		PALETTE_RAM_EMU[i] = c16;
	}

	uint16_t *screen = (uint16_t*)g_screen_ptr;
	for (int y=0;y<224;y++)
		for (int x=0;x<320;x++)
			screen[y*g_screen_pitch/2 + x] = PALETTE_RAM_EMU[0xFFF];
}

static void render_end(void) {}

