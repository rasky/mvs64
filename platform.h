#ifndef PLATFORM_H
#define PLATFORM_H

#ifdef N64
	#include <libdragon.h>

	#define BE16(x)  (x)
	#define BE32(x)  (x)

	enum {
		PLAT_KEY_P1_UP = 1,
		PLAT_KEY_P1_DOWN = 2,
		PLAT_KEY_P1_LEFT = 3,
		PLAT_KEY_P1_RIGHT = 4,
		PLAT_KEY_P1_A = 5,
		PLAT_KEY_P1_B = 6, 
		PLAT_KEY_P1_C = 7, 
		PLAT_KEY_P1_D = 8,
		PLAT_KEY_P1_START = 9,
		PLAT_KEY_P1_SELECT = 10,

		PLAT_KEY_COIN_1 = 50,
		PLAT_KEY_COIN_2 = 51,
		PLAT_KEY_COIN_3 = 52,
		PLAT_KEY_COIN_4 = 53,
		PLAT_KEY_SERVICE = 54,
	};

	extern uint8_t keystate[256];

#else
	#include <assert.h>
	#include <stdio.h>
	#include <SDL.h>

	#define BE16(x)  __builtin_bswap16(x)
	#define BE32(x)  __builtin_bswap32(x)

	#define debugf(msg, ...) fprintf(stderr, msg, ##__VA_ARGS__)

	#define assertf(cond, msg, ...) ({ \
		if (!(cond)) { \
			fprintf(stderr, "ASSERTION FAILED:\n"); \
			fprintf(stderr, msg "\n", __VA_ARGS__); \
			assert(cond); \
		} \
	})

	#define PLAT_KEY_P1_UP        SDL_SCANCODE_UP
	#define PLAT_KEY_P1_DOWN      SDL_SCANCODE_DOWN
	#define PLAT_KEY_P1_LEFT      SDL_SCANCODE_LEFT
	#define PLAT_KEY_P1_RIGHT     SDL_SCANCODE_RIGHT
	#define PLAT_KEY_P1_A         SDL_SCANCODE_Z
	#define PLAT_KEY_P1_B         SDL_SCANCODE_X
	#define PLAT_KEY_P1_C         SDL_SCANCODE_C
	#define PLAT_KEY_P1_D         SDL_SCANCODE_V
	#define PLAT_KEY_P1_START     SDL_SCANCODE_RETURN
	#define PLAT_KEY_P1_SELECT    SDL_SCANCODE_RSHIFT

	#define PLAT_KEY_COIN_1       SDL_SCANCODE_1
	#define PLAT_KEY_COIN_2       SDL_SCANCODE_2
	#define PLAT_KEY_COIN_3       SDL_SCANCODE_3
	#define PLAT_KEY_COIN_4       SDL_SCANCODE_4
	#define PLAT_KEY_SERVICE      SDL_SCANCODE_0

	extern const uint8_t *keystate;

	extern uint8_t *g_screen_ptr;
	extern int g_screen_pitch;

#endif


void plat_init(int audiofreq, int fps);
int plat_poll(void);

void plat_enable_audio(int enable);
void plat_enable_video(int enable);

void plat_save_screenshot(const char *fn);

void plat_beginframe(void);
void plat_endframe(void);

void plat_beginaudio(int16_t **buf, int *nsamples);
void plat_endaudio(void);

#endif
