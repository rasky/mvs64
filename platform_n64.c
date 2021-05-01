#include "platform.h"
#include <memory.h>

uint8_t keystate[256];

extern char end __attribute__((section (".data")));

static int rdp_disp;

void plat_init(int audiofreq, int fps) {
	init_interrupts();

    debug_init_isviewer();
    debug_init_usblog();
    debugf("MVS64\n");

    char *heap_top = (char*)0x80000000 + get_memory_size() - 0x10000;
    char *heap_end = &end;
    debugf("heap [%p - %p = %d]\n", heap_top, heap_end, heap_top-heap_end);

	controller_init();
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, 2, GAMMA_NONE, ANTIALIAS_OFF);
    dfs_init(DFS_DEFAULT_LOCATION);
    rdp_init();
}

int plat_poll(void) {
	controller_scan();
    struct controller_data ckeys = get_keys_pressed();

    memset(keystate, 0, sizeof(keystate));

    if (ckeys.c[0].up)      { keystate[PLAT_KEY_P1_UP] = 1; }
    if (ckeys.c[0].down)    { keystate[PLAT_KEY_P1_DOWN] = 1; }
    if (ckeys.c[0].left)    { keystate[PLAT_KEY_P1_LEFT] = 1; }
    if (ckeys.c[0].right)   { keystate[PLAT_KEY_P1_RIGHT] = 1; }
    if (ckeys.c[0].A)       { keystate[PLAT_KEY_P1_A] = 1; }
    if (ckeys.c[0].B)       { keystate[PLAT_KEY_P1_B] = 1; }
    if (ckeys.c[0].C_down)  { keystate[PLAT_KEY_P1_C] = 1; }
    if (ckeys.c[0].C_right) { keystate[PLAT_KEY_P1_D] = 1; }
    if (ckeys.c[0].start)   { keystate[PLAT_KEY_P1_START] = 1; }

    return 1;
}

void plat_enable_video(int enable) {

}

void plat_save_screenshot(const char *fn) {

}

uint8_t *g_screen_ptr;
int g_screen_pitch;

void plat_beginframe(void) {
    while(!(rdp_disp = display_lock())) {}

    extern void *__safe_buffer[];
	g_screen_ptr = __safe_buffer[rdp_disp-1];
	g_screen_pitch = 320*2;

    rdp_attach_display(rdp_disp);
	rdp_set_default_clipping();
}

void plat_endframe(void) {
	rdp_detach_display();
	display_show(rdp_disp);
	rdp_disp = 0;
}
