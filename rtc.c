
static uint8_t reg_rtc_data_in;
static uint8_t reg_rtc_clock;
static uint8_t reg_rtc_cmd;
static uint8_t reg_rtc_tp;
static int rtc_event_id;
static int rtc_event_period;

static uint32_t rtc_event_cb(void* arg) {
	reg_rtc_tp ^= 1;
	debugf("[RTC] TP trigger: %x\n", reg_rtc_tp);
	return rtc_event_period/2;
}

static void rtc_data_w(uint8_t x) {
	reg_rtc_data_in = x;
}

static void rtc_clock_w(uint8_t x) {
	if (!reg_rtc_clock && x) {
		debugf("[RTC] clock: data=%x\n", reg_rtc_data_in);
		reg_rtc_cmd >>= 1;
		reg_rtc_cmd |= reg_rtc_data_in << 3;
	}
	reg_rtc_clock = x;
}

static void rtc_stb_w(uint8_t x) {
	if (x) {
		switch (reg_rtc_cmd) {
			case 8: 
				debugf("[RTC] set TP mode: 1 sec\n");
				rtc_event_period = MVS_CLOCK;
				emu_change_event(rtc_event_id, emu_clock() + rtc_event_period/2);
				return;

			case 7: debugf("[RTC] set TP freq: 4096Hz\n"); return;
		}
		debugf("[RTC] unimplemented cmd=%x\n", reg_rtc_cmd);
	}
}

static uint8_t rtc_data_r(void) { return 1; }
static uint8_t rtc_tp_r(void) { return reg_rtc_tp; }

static void rtc_init(void) {
	rtc_event_period = MVS_CLOCK;
	rtc_event_id = emu_add_event(rtc_event_period/2, rtc_event_cb, NULL);
}
