
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

static void clear_screen(void) {
	for (int y=0;y<224;y++)
		memset(g_screen_ptr + y*g_screen_pitch/4, 0, 320*4);
}
