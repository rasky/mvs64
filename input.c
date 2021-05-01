
static uint8_t input_p1cnt_r(void) {
	uint8_t state = 0;
	state |= (~keystate[PLAT_KEY_P1_UP] & 1) << 0;
	state |= (~keystate[PLAT_KEY_P1_DOWN] & 1) << 1;
	state |= (~keystate[PLAT_KEY_P1_LEFT] & 1) << 2;
	state |= (~keystate[PLAT_KEY_P1_RIGHT] & 1) << 3;
	state |= (~keystate[PLAT_KEY_P1_A] & 1) << 4;
	state |= (~keystate[PLAT_KEY_P1_B] & 1) << 5;
	state |= (~keystate[PLAT_KEY_P1_C] & 1) << 6;
	state |= (~keystate[PLAT_KEY_P1_D] & 1) << 7;
	return state;
}

static uint8_t input_status_a_r(void) {
	uint8_t state = 0;

	state |= (~keystate[PLAT_KEY_COIN_1] & 1) << 0;
	state |= (~keystate[PLAT_KEY_COIN_2] & 1) << 1;
	state |= (~keystate[PLAT_KEY_SERVICE] & 1) << 2;
	state |= (~keystate[PLAT_KEY_COIN_3] & 1) << 3;
	state |= (~keystate[PLAT_KEY_COIN_4] & 1) << 4;
	state |= (rtc_tp_r() << 6);
	state |= (rtc_data_r() << 7);

	return state;
}

static uint8_t input_status_b_r(void) {
	uint8_t state = 0;

	state |= (~keystate[PLAT_KEY_P1_START] & 1) << 0;
	state |= (~keystate[PLAT_KEY_P1_SELECT] & 1) << 1;

	state |= 0x20;   // memory card not inserted
	state |= 0x40;   // memory card write protected
	state |= 0x80;   // MVS

	return state;
}
