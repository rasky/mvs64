#ifndef __PLAT_H__
#define __PLAT_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const uint8_t *keystate;
extern uint8_t keypressed[256];
extern uint8_t keyreleased[256];

void plat_init(int audiofreq, int fps);
int plat_poll(void);

void plat_enable_audio(int enable);
void plat_enable_video(int enable);

void plat_save_screenshot(const char *fn);

void plat_beginframe(uint8_t **screen, int *pitch);
void plat_endframe();

void plat_beginaudio(int16_t **buf, int *nsamples);
void plat_endaudio(void);

#ifdef __cplusplus
}
#endif

#endif

