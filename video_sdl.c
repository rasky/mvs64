
static void draw_sprite_fix(int spritenum, int palnum, int x, int y) {
	const int w=8, h=8;
	uint16_t *dst = (uint16_t*)g_screen_ptr + y*g_screen_pitch/2 + x;
	uint8_t *src = srom_get_sprite(spritenum);
	uint16_t *pal = PALETTE_RAM + PALETTE_RAM_BANK + palnum*16;

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

static void render_begin_sprites(void) {}
static void render_end_sprites(void) {}


static void draw_sprite(int spritenum, int palnum, int x0, int y0, bool flipx, bool flipy) {
	const int w = 16, h = 16; 
	uint8_t *src = crom_get_sprite(spritenum);
	uint16_t *pal = PALETTE_RAM + PALETTE_RAM_BANK + palnum*16;

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
			uint16_t *l = (uint16_t*)g_screen_ptr + y*g_screen_pitch/2;
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

static void render_begin_fix(void) {}
static void render_end_fix(void) {}

static void render_begin(void) {
	uint16_t *screen = (uint16_t*)g_screen_ptr;
	for (int y=0;y<224;y++)
		for (int x=0;x<320;x++)
			screen[y*g_screen_pitch/2 + x] = PALETTE_RAM[PALETTE_RAM_BANK+0xFFF];
}

static void render_end(void) {}

