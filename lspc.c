
static uint16_t *reg_vram_bank;
static uint16_t reg_vram_addr;
static uint16_t reg_vram_mod;
static uint16_t reg_vram_mask;
static uint16_t reg_lspcmode;
static uint8_t lspc_aa_counter;
static uint8_t lspc_aa_tick;

static void lspc_vram_data_w(uint16_t val) {
	reg_vram_bank[reg_vram_addr] = val;
	reg_vram_addr += reg_vram_mod;
	reg_vram_addr &= reg_vram_mask;
}

static uint16_t lspc_vram_data_r(void) {
	return reg_vram_bank[reg_vram_addr];
}

static void lspc_vram_addr_w(uint32_t val) {
	if (!(val & 0x8000)) {
		reg_vram_bank = VIDEO_RAM;
		reg_vram_mask = 0x7FFF;
	} else {
		reg_vram_bank = VIDEO_RAM + 0x8000;
		reg_vram_mask = 0x7FF;		
	}

	reg_vram_addr = val & reg_vram_mask;
}

static void lspc_vram_modulo_w(uint16_t val) {
	reg_vram_mod = val;
}

static uint16_t lspc_vram_modulo_r(void) {
	return reg_vram_mod;
}

static uint16_t lspc_mode_r() {
	int64_t clk = emu_clock_frame();
	int line = clk / (MVS_CLOCK / FPS / 264);

	return ((line+0xF8) << 7) | (lspc_aa_counter & 7);
}

static void lspc_mode_w(uint16_t val) {
	reg_lspcmode = val;
	debugf("[LSPC] mode: %02x\n", val);

	if (val & (1<<4))
		debugf("[LSPC] Timer interrupt **************************\n");
}

static void lspc_vblank(void) {
	if (lspc_aa_tick == 0) {
		lspc_aa_tick = reg_lspcmode >> 8;
		lspc_aa_counter++;
	} else 
		--lspc_aa_tick;
}

bool lspc_get_auto_animation(uint8_t *value) {
	*value = lspc_aa_counter & 7;
	return !(reg_lspcmode & (1<<3));
}

