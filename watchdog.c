
#define ACCURATE_WATCHDOG        0

static int watchdog_event;
uint8_t watchdog_kicked;

static uint32_t watchdog_expired(void* arg) {
	debugf("[HW] Watchdog expired! Rebooting\n");
	emu_cpu_reset();
	return WATCHDOG_PERIOD;
}

static void watchdog_kick(void) {
	if (ACCURATE_WATCHDOG)
		emu_change_event(watchdog_event, emu_clock() + WATCHDOG_PERIOD);
	else
		watchdog_kicked = true;
}

static void watchdog_init(void) {
	watchdog_event = emu_add_event(WATCHDOG_PERIOD, watchdog_expired, NULL);
}

static void watchdog_vblank(void) {
	if (watchdog_kicked) {
		watchdog_kicked = false;
		emu_change_event(watchdog_event, emu_clock() + WATCHDOG_PERIOD);
	}
}
