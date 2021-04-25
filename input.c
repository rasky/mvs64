
#include <SDL2/SDL.h>

static uint8_t input_p1cnt_r(void) {
	uint8_t state = 0;
	state |= (~keystate[SDL_SCANCODE_UP] & 1) << 0;
	state |= (~keystate[SDL_SCANCODE_DOWN] & 1) << 1;
	state |= (~keystate[SDL_SCANCODE_LEFT] & 1) << 2;
	state |= (~keystate[SDL_SCANCODE_RIGHT] & 1) << 3;
	state |= (~keystate[SDL_SCANCODE_Z] & 1) << 4;
	state |= (~keystate[SDL_SCANCODE_X] & 1) << 5;
	state |= (~keystate[SDL_SCANCODE_C] & 1) << 6;
	state |= (~keystate[SDL_SCANCODE_V] & 1) << 7;
	return state;
}

static uint8_t input_status_a_r(void) {
	uint8_t state = 0;

	state |= (~keystate[SDL_SCANCODE_1] & 1) << 0;
	state |= (~keystate[SDL_SCANCODE_2] & 1) << 1;
	state |= (~keystate[SDL_SCANCODE_0] & 1) << 2;
	state |= (~keystate[SDL_SCANCODE_3] & 1) << 3;
	state |= (~keystate[SDL_SCANCODE_4] & 1) << 4;
	state |= (rtc_tp_r() << 6);
	state |= (rtc_data_r() << 7);

	return state;
}

static uint8_t input_status_b_r(void) {
	uint8_t state = 0;

	state |= (~keystate[SDL_SCANCODE_RETURN] & 1) << 0;
	state |= (~keystate[SDL_SCANCODE_RSHIFT] & 1) << 1;

	state |= 0x20;   // memory card not inserted
	state |= 0x40;   // memory card write protected
	state |= 0x80;   // MVS

	return state;
}
