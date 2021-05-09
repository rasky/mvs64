
static uint16_t reg_vramaddr;
static uint16_t reg_vrammod;
static uint16_t reg_lspcmode;
static uint8_t lspc_aa_counter;
static uint8_t lspc_aa_tick;

static void lspc_vram_data_w(uint16_t val) {
	assertf(reg_vramaddr < sizeof(VIDEO_RAM)/2, "Invalid VRAM address: %02x", reg_vramaddr);
	VIDEO_RAM[reg_vramaddr] = val; 

	reg_vramaddr = (reg_vramaddr & 0x8000) | ((reg_vramaddr+reg_vrammod) & 0x7FFF);
}

static uint16_t lspc_vram_data_r(void) {
	assertf(reg_vramaddr < sizeof(VIDEO_RAM)/2, "Invalid VRAM address: %02x", reg_vramaddr);
	return VIDEO_RAM[reg_vramaddr];
}

static void lspc_vram_addr_w(uint32_t val) {
	reg_vramaddr = val;
	// debugf("[HWIO] vram addr=%04x (PC:%06x)\n", reg_vramaddr, (unsigned int)emu_pc());
}

static void lspc_vram_modulo_w(uint16_t val) {
	reg_vrammod = val;
	// debugf("[HWIO] vram mod=%02x\n", reg_vrammod	); 
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

