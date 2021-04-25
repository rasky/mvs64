#ifndef PLATFORM_H
#define PLATFORM_H


#ifdef N64
	#include <libdragon.h>

	#define BE16(x)  (x)
	#define BE32(x)  (x)

#else
	#include <assert.h>
	#include <stdio.h>
	#include "platform_sdl.h"

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

#endif

#endif
