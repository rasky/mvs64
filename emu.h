#ifndef EMU_H
#define EMU_H

// Frameskipping mode:
//   0 - never frameskip, game might slowdown
//   1 - 30 FPS mode (draw one frame every two)
//   2 - auto mode. Game will frameskip as much as necessary to keep up with 60 FPS
#define CONFIG_FRAMESKIP_MODE            0

#include <stdint.h>
#include <stdbool.h>

#define MVS_CLOCK         24000000
#define M68K_CLOCK_DIV    2
#define FPS        		  60
#define FRAME_CLOCK       (MVS_CLOCK / FPS)
#define LINE_CLOCK        (FRAME_CLOCK / 264)
#define WATCHDOG_PERIOD   3244030

#define MAX_EVENTS 8

typedef uint32_t (*EmuEventCb)(void *cbarg);

typedef struct {
    int64_t clock;
    EmuEventCb cb;
    void *cbarg;
    bool current;
} EmuEvent;

int emu_add_event(int64_t clock, EmuEventCb cb, void *cbarg);
void emu_change_event(int event_id, int64_t clock);
int64_t emu_clock(void);
int64_t emu_clock_frame(void);
uint32_t emu_pc(void);

void emu_cpu_reset(void);
void emu_cpu_irq(int level, bool state);

#endif
