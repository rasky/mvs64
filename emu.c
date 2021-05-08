#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emu.h"
#include "m68k.h"
#include "hw.h"
#include "video.h"
#include "roms.h"
#include "platform.h"

static int cpu_trace_count = 0;

void cpu_trace(unsigned int pc) {
	if (cpu_trace_count == 0) {
		m68k_set_instr_hook_callback(NULL);
		return;
	}

	char inst[1024];
	m68k_disassemble(inst, pc, M68K_CPU_TYPE_68000);
	debugf("trace: %06x %-30s", pc, inst);

	if (strstr(inst, "A0")) debugf("A0=%08x ", m68k_get_reg(NULL, M68K_REG_A0));
	if (strstr(inst, "A1")) debugf("A1=%08x ", m68k_get_reg(NULL, M68K_REG_A1));
	if (strstr(inst, "A2")) debugf("A2=%08x ", m68k_get_reg(NULL, M68K_REG_A2));
	if (strstr(inst, "A3")) debugf("A3=%08x ", m68k_get_reg(NULL, M68K_REG_A3));
	if (strstr(inst, "A4")) debugf("A4=%08x ", m68k_get_reg(NULL, M68K_REG_A4));
	if (strstr(inst, "A5")) debugf("A5=%08x ", m68k_get_reg(NULL, M68K_REG_A5));
	if (strstr(inst, "A6")) debugf("A6=%08x ", m68k_get_reg(NULL, M68K_REG_A6));
	if (strstr(inst, "A7")) debugf("A7=%08x ", m68k_get_reg(NULL, M68K_REG_A7));
	if (strstr(inst, "D0")) debugf("D0=%08x ", m68k_get_reg(NULL, M68K_REG_D0));
	if (strstr(inst, "D1")) debugf("D1=%08x ", m68k_get_reg(NULL, M68K_REG_D1));
	if (strstr(inst, "D2")) debugf("D2=%08x ", m68k_get_reg(NULL, M68K_REG_D2));
	if (strstr(inst, "D3")) debugf("D3=%08x ", m68k_get_reg(NULL, M68K_REG_D3));
	if (strstr(inst, "D4")) debugf("D4=%08x ", m68k_get_reg(NULL, M68K_REG_D4));
	if (strstr(inst, "D5")) debugf("D5=%08x ", m68k_get_reg(NULL, M68K_REG_D5));
	if (strstr(inst, "D6")) debugf("D6=%08x ", m68k_get_reg(NULL, M68K_REG_D6));
	if (strstr(inst, "D7")) debugf("D7=%08x ", m68k_get_reg(NULL, M68K_REG_D7));

	debugf("\n");

	cpu_trace_count--;
}

void cpu_start_trace(int cnt) {
	#ifndef N64
	m68k_set_instr_hook_callback(cpu_trace);
	cpu_trace_count = cnt;
	#endif
}

static int g_frame;
static uint64_t g_clock, g_clock_framebegin;
static uint64_t m68k_clock;
static EmuEvent events[MAX_EVENTS];

static uint64_t m68k_exec(uint64_t clock) {
	clock /= M68K_CLOCK_DIV;
	if (clock > m68k_clock) {
		m68k_clock += m68k_execute(clock - m68k_clock);	
	}
	return m68k_clock * M68K_CLOCK_DIV;
}


// Return the next event that must be executed
static EmuEvent* next_event() {
    EmuEvent *e = NULL;
    for (int i=0;i<MAX_EVENTS;i++) {
        if (!events[i].cb) continue;
        if (!e || events[i].clock < e->clock) e=&events[i];
    }
    return e;
}

int emu_add_event(int64_t clock, EmuEventCb cb, void *cbarg) {
    for (int i=0;i<MAX_EVENTS;i++) {
        if (events[i].cb) continue;
        events[i].clock = clock;
        events[i].cb = cb;
        events[i].cbarg = cbarg;
        return i;
    }
    assert(0);
}

void emu_change_event(int event_id, int64_t newclock) {
	events[event_id].clock = newclock;
	m68k_end_timeslice();
}

int64_t emu_clock(void) {
	return g_clock + m68k_cycles_run() * M68K_CLOCK_DIV;
}

int64_t emu_clock_frame(void) {
	return emu_clock() - g_clock_framebegin;
}

uint32_t emu_pc(void) {
	return m68k_get_reg(NULL, M68K_REG_PC);
}

uint32_t emu_vblank_start(void* arg) {
	m68k_set_virq(1, true);
	hw_vblank();
	debugf("[EMU] VBlank - clock:%lld clock_frame:%lld\n", emu_clock(), emu_clock_frame());
	return FRAME_CLOCK;
}

void emu_run_frame(void) {
	g_clock_framebegin = g_clock;
    uint64_t vsync = g_clock + FRAME_CLOCK;
    EmuEvent *e;

    // Run all events that are scheduled before next vsync
    while ((e = next_event()) && (e->clock < vsync)) {
        g_clock = m68k_exec(e->clock);

        // Call the event callback, and check if it must be repeated.
        uint32_t repeat = e->cb(e->cbarg);
        if (repeat != 0) e->clock += repeat;
        else e->cb = NULL;
    }

    g_clock = m68k_exec(vsync);

    // Frame completed
	debugf("[EMU] Frame completed: %d (vsync: %llu, g_clock: %llu)\n", g_frame, vsync, g_clock);
    g_clock = vsync;
    g_frame++;
}

int main(void) {
	plat_init(44100, FPS);
	plat_enable_video(true);

	#ifdef N64
	rom_load_bios("rom:/");
	rom_load_mslug("rom:/");
	#else
	rom_load_bios("roms/bios/");
	rom_load_mslug("roms/mslug64/");
	// rom_load_aof("roms/aof/");
	// rom_load_kof98("roms/kof98/");
	// rom_load_samsho("roms/samsho/");
	// rom_load_spriteex("roms/spriteex/");
	// rom_load_nyanmvs("roms/nyanmvs/");
	// rom_load_krom("roms/krom/");
	#endif

	m68k_init();

	hw_init();
	g_clock = 0;

	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();
	m68k_clock = 0;

	emu_add_event(LINE_CLOCK*248, emu_vblank_start, NULL);

	for (int i=0;i<7000;i++) {
		#ifdef N64
		uint32_t t0 = TICKS_READ();
		#endif
		emu_run_frame();
		if (!plat_poll()) break;

		#ifdef N64
		uint32_t tlen = TICKS_DISTANCE(t0, TICKS_READ());
		#else
		unsigned long tlen = 0;
		const int TICKS_PER_SECOND = 60;
		#endif
		debugf("[PROFILE] ticks:%ld cpu:%.2f%% PC:%06x\n", tlen, (float)tlen * 100.f / (float)(TICKS_PER_SECOND / 60), m68k_get_reg(NULL, M68K_REG_PC));

		plat_beginframe();
		video_render();
		plat_endframe();

		rom_next_frame();
	}

	debugf("end\n");
	cpu_start_trace(1000);
	m68k_exec(g_clock+100);

	fprintf(stderr, "SR=%04x\n", m68k_get_reg(NULL, M68K_REG_SR));
	#ifndef N64
	FILE *f = fopen("vram.dump", "wb");
	fwrite(VIDEO_RAM, 1, sizeof(VIDEO_RAM), f);
	fclose(f);
	#endif

	plat_save_screenshot("screen.bmp");
}
