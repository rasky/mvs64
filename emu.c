#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emu.h"
#ifdef N64
#include "m64k/m64k.h"
#else
#include "m68k.h"
#endif
#include "hw.h"
#include "video.h"
#include "roms.h"
#include "platform.h"

static int cpu_trace_count = 0;
void cpu_trace(unsigned int pc) {
	(void)cpu_trace_count;
	#ifndef N64
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
	#endif
}

void cpu_start_trace(int cnt) {
	#ifndef N64
	m68k_set_instr_hook_callback(cpu_trace);
	#endif
	cpu_trace_count = cnt;
}

static int g_frame;
#ifdef N64
static m64k_t m64k;
#endif
static uint64_t g_clock, g_clock_framebegin;
static uint64_t m68k_clock;
static EmuEvent events[MAX_EVENTS];
uint32_t hw_io_profile;
uint32_t profile_hw_io;

static uint64_t m68k_exec(uint64_t clock) {
	clock /= M68K_CLOCK_DIV;
	if (clock > m68k_clock) {
		#ifdef N64
		debugf("m68k_exec: %d\n", (int)(clock - m68k_clock));
		m68k_clock = m64k_run(&m64k, clock);
		#else
		m68k_clock += m68k_execute(clock - m68k_clock);	
		#endif
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
        events[i].current = false;
        return i;
    }
    assert(0);
}

void emu_change_event(int event_id, int64_t newclock) {
	events[event_id].clock = newclock;
	if (events[event_id].current) {
		#ifdef N64
		m64k_run_stop(&m64k);
		#else
		m68k_end_timeslice();
		#endif
	}
}

int64_t emu_clock(void) {
	#ifdef N64
	return m64k_get_clock(&m64k) * M68K_CLOCK_DIV;
	#else
	return g_clock + m68k_cycles_run() * M68K_CLOCK_DIV;
	#endif
}

int64_t emu_clock_frame(void) {
	return emu_clock() - g_clock_framebegin;
}

void emu_cpu_reset(void) {
	#ifdef N64
	m64k_pulse_reset(&m64k);
	#else
	m68k_pulse_reset();
	#endif
}

uint32_t emu_pc(void) {
	#ifdef N64
	return m64k_get_pc(&m64k) & 0xFFFFFF;
	#else
	return m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF;
	#endif
}

void emu_cpu_irq(int irq, bool on) {
	#ifdef N64
	m64k_set_virq(&m64k, irq, on);
	#else
	m68k_set_virq(irq, on);
	#endif
}

#ifdef N64
int cpu_irqack(void *ctx, int level)
{	
	// On NeoGeo hardware, interrupts must be manually acknowledged via a write
	// to register 0x3C000C. So we do nothing here.
	// NOTE: we still must register this hook, otherwise the m64k core will
	// by default auto-acnowledge the interrupts.
	return 0;
}
#endif

uint32_t emu_vblank_start(void* arg) {
	emu_cpu_irq(1, true);
	hw_vblank();
	debugf("[EMU] VBlank - clock:%lld clock_frame:%lld\n", emu_clock(), emu_clock_frame());
	return FRAME_CLOCK;
}

void emu_run_frame(void) {
    uint64_t vsync = g_clock_framebegin + FRAME_CLOCK;
    EmuEvent *e;

    // Run all events that are scheduled before next vsync
    while ((e = next_event()) && (e->clock < vsync)) {
    	e->current = true;
        g_clock = m68k_exec(e->clock);
        e->current = false;

        // Call the event callback, and check if it must be repeated.
        if (g_clock >= e->clock) {    	
	        uint32_t repeat = e->cb(e->cbarg);
	        if (repeat != 0) e->clock += repeat;
	        else e->cb = NULL;
        }
    }

    while (g_clock < vsync)
    	g_clock = m68k_exec(vsync);

    // Frame completed
	debugf("[EMU] Frame completed: %d (vsync: %llu)\n", g_frame, vsync);
    g_frame++;
	g_clock_framebegin += FRAME_CLOCK;
}

int main(int argc, char *argv[]) {
	#ifndef N64
	if (argc < 2) {
		fprintf(stderr, "Usage:\n    mvs64 <romdir>\n");
		return 1;
	}
	#else 
	argc = 0; argv = NULL;
	#endif

	plat_init(44100, FPS);
	plat_enable_video(true);

	#ifdef N64
	rom_load("rom:/");
	#else
	rom_load(argv[1]);
	#endif

	#ifdef N64
	m64k_init(&m64k);
	m64k_set_hook_irqack(&m64k, cpu_irqack, NULL);
	#else
	m68k_init();
	#endif

	hw_init();
	g_clock = 0;

	#ifdef N64
	m64k_pulse_reset(&m64k);
	#else
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();	
	#endif
	m68k_clock = 0;

	emu_add_event(LINE_CLOCK*248, emu_vblank_start, NULL);

	for (int i=0;i<7000000;i++) {
		hw_io_profile = 0;
		#ifdef N64
		uint32_t t0 = TICKS_READ();
		#endif
		emu_run_frame();
		if (!plat_poll()) break;

		#ifdef N64
		uint32_t emu_time = TICKS_READ();
		#endif

		// Draw the screen
		plat_beginframe();
		video_render();
		plat_endframe();

		#ifdef N64
		uint32_t draw_time = TICKS_READ();

		debugf("[PROFILE] cpu:%.2f%% io:%.2f%% draw:%.2f%% PC:%06lx\n",
			(float)TICKS_DISTANCE(t0, emu_time) * 100.f / (float)(TICKS_PER_SECOND / 60),
			(float)hw_io_profile * 100.f / (float)(TICKS_PER_SECOND / 60),
			(float)TICKS_DISTANCE(emu_time, draw_time) * 100.f / (float)(TICKS_PER_SECOND / 60),
			#ifdef N64
			m64k_get_pc(&m64k));
			#else
			(uint32_t)m68k_get_reg(NULL, M68K_REG_PC));
			#endif
		#endif

		rom_next_frame();
	}

	debugf("end\n");
	cpu_start_trace(1000);
	m68k_exec(g_clock+100);

	#ifndef N64
	FILE *f = fopen("vram.dump", "wb");
	fwrite(VIDEO_RAM, 1, sizeof(VIDEO_RAM), f);
	fclose(f);
	#endif

	plat_save_screenshot("screen.bmp");
}
