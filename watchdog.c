
static int watchdog_event;

static unsigned int watchdog_expired(void* arg) {
	debugf("[HW] Watchdog expired! Rebooting\n");
	m68k_pulse_reset();
	return WATCHDOG_PERIOD;
}

static void watchdog_kick(void) {
	emu_change_event(watchdog_event, emu_clock() + WATCHDOG_PERIOD);
}

static void watchdog_init(void) {
	watchdog_event = emu_add_event(WATCHDOG_PERIOD, watchdog_expired, NULL);
}
