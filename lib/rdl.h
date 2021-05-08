// RDP Display List support
//
// There are two possible approaches on how you can create a display list:
//
// Immediate mode
// **************
// A single large display list will be created and filled from scratch every
// frame. The display list will be filled with all the required display
// primitives to compose the final picture. RDP can start executing the display
// list in background, as the CPU fills it, so that resource usage can be
// maximized.
//
// To implement immediate mode:
// 
//    1) Allocate a large display list once, possibly with rdl_heap_alloc().
//    2) At the beginning of each frame, reset the display list with
//       rdl_reset() and start pushing it to the RPD with rdl_exec().
//    3) Fill the display list with rdl_push() or through higher-level APIs
//       like rdl_sprite(), rdl_fillrect(), etc.
//    4) Every once in a while (eg: after every "graphic actor" or "layer"),
//       call rdl_flush() to keep feeding the RDP with more primitives.
//
// During debugging, compile with asserts enabled so that rdl_push() will assert
// if the display list is not large enough.
// 
// Retained mode
// *************
// Every "graphic object" in the scene has its own display list. To draw the
// scene, many different display lists will be sent to the RDP, possibly using
// a queue.
//
// To implement retained mode:
//
//    1) Allocate a different display list for each object or group of objects.
//       Depending on the context, you can use rdl_stack_alloc, rdl_heap_alloc,
//       RDL_STRUCT_ALLOC, or RDL_STATIC_ALLOC.
//    2) For every frame, update or regenerate the display lists that need
//       to be changed. For instance, if the game score text is changed, you
//       can update the related display list by resetting it and regenerating
//       the new primitives.
//    3) For every frame, call rdl_exec on all the display lists that compose
//       your scene. RDP will process the display lists in background, so you
//       can keep queuing display lists until the scene is finished.
//
//


#include <libdragon.h>
#include <alloca.h>
#include <malloc.h>
#include <memory.h>
#include <stdbool.h>
#include "assert.h"

// A RDP display list. This is a linear container of RDP primitives that can be
// sent to RDP for asynchronous execution.
// 
// NOTE: Do not access the structure fields directly. The fields are not part of
// the public API and might go away at any point. Use the rdl_* functions/macros
// to inspect or modify a display list.
typedef struct {
	uint64_t *__cur;
	uint64_t *__end;
	uint8_t __flags;
	uint64_t __prims[];
} RdpDisplayList;

// Flags used by RdpDisplayList
#define RDL_FLAGS_NOFREE   (1<<0)    // The display list doesn't require to be freed
#define RDL_FLAGS_PLAYING  (1<<7)    // The display list is being played

// A RDP rectangle. Used by APIs to collect information on rectangles.
typedef struct {
    int16_t x0, y0, x1, y1;
} RdpRect;

// A RDP 32-bit color. Use RDP_COLOR32 macro to compose.
typedef uint32_t RdpColor32;

// Allocate a display list on the stack. nprims is the maximum number of primitives
// that can be held by the display list.
// Since the memory is stack allocated, there is no need to release it.
#define rdl_stack_alloc(nprims) ({ \
	RdpDisplayList* rdl = (RdpDisplayList*)alloca(sizeof(RdpDisplayList) + sizeof(uint64_t)*(nprims)); \
	rdl->__cur = &rdl->__prims[0]; rdl->__end = &rdl->__prims[nprims]; rdl->__flags = RDL_FLAGS_NOFREE; \
	rdl; \
})

// Allocate a display list on the heap. nprims is the maximum number of primitives
// that can be held by the display list.
// Once you are done with the display list, you can call free() to release it.
#define rdl_heap_alloc(nprims) ({ \
	RdpDisplayList* rdl = (RdpDisplayList*)malloc(sizeof(RdpDisplayList) + sizeof(uint64_t)*(nprims)); \
	assert(rdl); \
	rdl->__cur = &rdl->__prims[0]; rdl->__end = &rdl->__prims[nprims]; rdl->__flags = RDL_FLAGS_NOFREE; \
	rdl; \
})

// Define a RDP display list as member of a struct. "name" is the name of the
// field in the struct, and "nprims" is the number of primitives. The memory
// necessary to hold the primitives will also be allocated as part of the struct
// itself.
//
// Example:
//
//  struct Enemy {
//     int type;
//     int x, y;
//     RDL_STRUCT_ALLOC(rdl, 16);
//  };
//
//  NOTE: Use RDL_STRUCT_INIT to initialize the display list before first usage.
//
#define RDL_STRUCT_ALLOC(name, nprims) \
    union { \
        RdpDisplayList name; \
        uint8_t name ## _mem[sizeof(RdpDisplayList) + (nprims)*sizeof(uint64_t)]; \
    }

// Initialize a RDP display list which is part of a structure. ptr is a pointer
// to the containing object, and "name" is the name of the field.
//
// Continuing the example above:
//
//    struct Enemy monster;
//    monster.type = MONSTER_DRAGON;
//    monster.x = 100; monster.y = 200;
//
//    RDL_STRUCT_INIT(&monster, rdl);
//    rdl_sprite(&monster.rdl, g_dragon, monster.x, monster.y, true, NULL);
//
#define RDL_STRUCT_INIT(name) ({ \
    RdpDisplayList *rdl = &ptr->name; \
    rdl->__cur = &rdl->__prims[0]; \
    rdl->__cur = &rdl->__prims[(sizeof(ptr->name ## _mem) - sizeof(RdpDisplayList)) / sizeof(uint64_t)]; \
    rdl->__flags = RDL_FLAGS_NOFREE; \
})

// Initialize a static RDP display list that cannot be modified dynamically.
// This static allocation can be used either within a function or at the global
// scope.
//
// Example:
//
//     static RdpDisplayList dlinit = RDL_STATIC_ALLOC(
//          RdpSetOtherModes(SOM_CYCLE_FILL),
//          RdpSetFillColor(RDP_COLOR16(0,0,0,0)),
//          RdpFillRectangleI(0, 0, 320, 240),
//     );
//
//  NOTE: Use RDL_STATIC_INIT to initialize the display list before first usage.
//
#define RDL_STATIC_ALLOC(...) \
    { NULL, NULL, RDL_FLAGS_NOFREE, { __VA_ARGS__ } }

// Initialize a statically-allocated RDP display list. This must be called before
// first usage.
#define RDL_STATIC_INIT(rdl, n) ({ \
    (rdl)->__end = &(rdl)->__prims[n]; \
    (rdl)->__cur = (rdl)->__end; \
})


// Returns the length of a display list (how many primitives it contains)
#define rdl_len(rdl)         ((rdl)->__cur - &(rdl)->__prims[0])

// Returns the number of empty slots in the display lists, that is how many
// more primitives can be pushed. If zero, the display list is full.
// If you want to extend a full display list, you can either chain to it
#define rdl_nempty(rdl)      ((rdl)->__end - (rdl)->__cur)

// Asserts that there are at least n slots in the display list. This function
// is useful for debugging purposes.
#define rdl_assert_nempty(rdl, n) ({ \
	assertf(rdl_nempty(rdl) >= n, "not enough space available in display list"); \
})

// Reset the display list to the empty status. All contained primitives are discarded.
#define rdl_reset(rdl) ({ \
	rdl->__cur = &rdl->__prims[0]; \
})

// Free a display list, releasing its memory.
#define rdl_free(rdl) ({ \
	(rdl)->__cur = (rdl)->__end = 0; \
	if (!((rdl)->__flags & RDL_FLAGS_NOFREE)) free(rdl); \
})


#define _get_nth_arg(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, N, ...)  N
#define _fe_0(_call, x, ...)
#define _fe_1(_call, x, y)       _call(x, y)
#define _fe_2(_call, x, y, ...)  _call(x, y) _fe_1(_call, x, __VA_ARGS__)
#define _fe_3(_call, x, y, ...)  _call(x, y) _fe_2(_call, x, __VA_ARGS__)
#define _fe_4(_call, x, y, ...)  _call(x, y) _fe_3(_call, x, __VA_ARGS__)
#define _fe_5(_call, x, y, ...)  _call(x, y) _fe_4(_call, x, __VA_ARGS__)
#define _fe_6(_call, x, y, ...)  _call(x, y) _fe_5(_call, x, __VA_ARGS__)
#define _fe_7(_call, x, y, ...)  _call(x, y) _fe_6(_call, x, __VA_ARGS__)
#define _fe_8(_call, x, y, ...)  _call(x, y) _fe_7(_call, x, __VA_ARGS__)
#define _fe_9(_call, x, y, ...)  _call(x, y) _fe_8(_call, x, __VA_ARGS__)
#define _fe_10(_call, x, y, ...) _call(x, y) _fe_9(_call, x, __VA_ARGS__)
#define _fe_11(_call, x, y, ...) _call(x, y) _fe_10(_call, x, __VA_ARGS__)
#define _fe_12(_call, x, y, ...) _call(x, y) _fe_11(_call, x, __VA_ARGS__)
#define _fe_13(_call, x, y, ...) _call(x, y) _fe_12(_call, x, __VA_ARGS__)
#define _fe_14(_call, x, y, ...) _call(x, y) _fe_13(_call, x, __VA_ARGS__)
#define _fe_15(_call, x, y, ...) _call(x, y) _fe_14(_call, x, __VA_ARGS__)
#define _fe_16(_call, x, y, ...) _call(x, y) _fe_15(_call, x, __VA_ARGS__)
#define _fe_17(_call, x, y, ...) _call(x, y) _fe_16(_call, x, __VA_ARGS__)
#define _fe_18(_call, x, y, ...) _call(x, y) _fe_17(_call, x, __VA_ARGS__)
#define _fe_19(_call, x, y, ...) _call(x, y) _fe_18(_call, x, __VA_ARGS__)
#define _fe_20(_call, x, y, ...) _call(x, y) _fe_19(_call, x, __VA_ARGS__)
#define _fe_21(_call, x, y, ...) _call(x, y) _fe_20(_call, x, __VA_ARGS__)
#define _fe_22(_call, x, y, ...) _call(x, y) _fe_21(_call, x, __VA_ARGS__)
#define _fe_23(_call, x, y, ...) _call(x, y) _fe_22(_call, x, __VA_ARGS__)
#define _fe_24(_call, x, y, ...) _call(x, y) _fe_23(_call, x, __VA_ARGS__)
#define _fe_25(_call, x, y, ...) _call(x, y) _fe_24(_call, x, __VA_ARGS__)
#define _fe_26(_call, x, y, ...) _call(x, y) _fe_25(_call, x, __VA_ARGS__)
#define _fe_27(_call, x, y, ...) _call(x, y) _fe_26(_call, x, __VA_ARGS__)
#define _fe_28(_call, x, y, ...) _call(x, y) _fe_27(_call, x, __VA_ARGS__)
#define _fe_29(_call, x, y, ...) _call(x, y) _fe_28(_call, x, __VA_ARGS__)
#define _fe_30(_call, x, y, ...) _call(x, y) _fe_29(_call, x, __VA_ARGS__)
#define _fe_31(_call, x, y, ...) _call(x, y) _fe_30(_call, x, __VA_ARGS__)
#define _count_varargs(...)      _get_nth_arg("ignored", ##__VA_ARGS__, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#define _call_multi(...)         _get_nth_arg("ignored", ##__VA_ARGS__, _fe_31, _fe_30, _fe_29, _fe_28, _fe_27, _fe_26, _fe_25, _fe_24, _fe_23, _fe_22, _fe_21, _fe_20, _fe_19, _fe_18, _fe_17, _fe_16, _fe_15, _fe_14, _fe_13, _fe_12, _fe_11, _fe_10, _fe_9, _fe_8, _fe_7, _fe_6, _fe_5, _fe_4, _fe_3, _fe_2, _fe_1, _fe_0)

#define _rdl_push_1(rdl, prim)   *rdl->__cur++ = prim;
#define _rdl_push_multi(rdl, ...) ({ \
	if (rdl->__cur + _count_varargs(__VA_ARGS__) > rdl->__end) assertf(0, "display list is full"); \
	_call_multi(__VA_ARGS__)(_rdl_push_1, rdl, ##__VA_ARGS__); \
})

// Push one or more new graphic primitives into the display list.
//
// The function will assert in case the display list is full. Use rdl_nempty()
// if you want to check if there's enough space.
//
// Example:
//    rdl_push(rdl, RdpSetFillColor(COLOR32(0,0,0,0));
//

#define rdl_push(rdl, ...) _rdl_push_multi(rdl, __VA_ARGS__)


// Grow a display list to be able to hold at least the specified number
// of primitives, in addition to the ones already contained. The function
// return the new, larger display list that substitute the previous one.
//
// NOTE: this function is provided mainly for very simple prototypes and debug
// purposes. Don't abuse heap resizing on N64 because you will risk memory
// fragmentation. Prefer pre-allocating once a big enough display list.
RdpDisplayList* rdl_grow_for(RdpDisplayList *rdl, int nprims);

// This function adds a RDP Pipe Sync primitive to the RDP display list and
// then flushes the display list from the CPU cache, so that it is ready for
// execution with rdl_exec().
// 
// If the display list is already being executed (through rdl_exec()), this 
// function notifies the RDP that new primitives have been added to the same
// list, so that the RDP can process them.
//
// In retained mode, call rdl_flush to "terminate" a display list, so that it's
// ready for execution.
//
// In immediate mode, you can call rdl_flush whenever you want the RDP to enqueue
// more primitives for processing. It makes sense to call this function several
// times while building the display list to make sure to keep the RDP working.
void rdl_flush(RdpDisplayList *rdl);

// Execute a RDL display list, asynchronously. This function sends the display
// list to the RDP via DMA for immediate execution. If the RDP is currently busy
// and the DMA queue is full, this function enqueues internally the display list,
// and will be executed once the RDP is free.
//
// The display list must have been "terminated" by rdl_flush(). If rdl_flush()
// wasn't called, this function will assert and the behavior might be undefined.
void rdl_exec(RdpDisplayList *rdl);


////////////////////////////////////////////////////////////////////////////////
// Low-level RDP primitives
////////////////////////////////////////////////////////////////////////////////

#include "rdp_commands.h"


////////////////////////////////////////////////////////////////////////////////
// High-level graphic objects
////////////////////////////////////////////////////////////////////////////////
// 
// The following functions implement higher-level graphic objects, made 
// of multiple graphic primitives. They are easier to use that the low-level RDP
// graphic primitives, but less flexible in exploiting all hardware features.
//
// Each "object" is created appending multiple graphic primitives to a display
// list. It is responsibility of the caller to make sure that the display list
// is large enough; the functions will assert if it is not.
//
// Some objects can also be modified after their creation, by modifying the
// display list. Typically, this is offered whenever the modification is much
// faster then recreating an object from scratch, and is useful in retained mode
// where display lists are reused across multiple frames.
//
// For instance, rdl_sprite() creates (fills) a display list that draws an
// arbitrary-sized 2D sprite. The display list can be reused across frames. If
// the sprite needs to move, instead of recreating the display list from scratch,
// it is possible to call rdl_sprite_move() that updates the X/Y position of
// sprite in the display list and is much faster.
//
// These objects manipulation functions always take the display list pointer
// and an offset as argument. The offset is the position within the display
// list where the object primitives are stored. If the display list only holds
// that object, 0 can be passed. Otherwise, if you are storing multiple objects
// in the same display list, you can call rdl_len() before creation to get the
// offset of each object. For instance:
//
//   RdpDisplayList *rdl = rdl_heap_alloc(256);
//
//   int off1 = 0;  // first object is at offset 0
//   rdl_sprite(rdl, g_sprite_1, 100, 200, true, NULL);
//
//   int off2 = rdl_len(rdl);  // get offset of second object
//   rdl_sprite(rdl, g_sprite_1, 300, 200, true, NULL);
//
//   [ ... later ...]
//
//   rdl_sprite_move(rdl, off2, 350, 200);  // move the second object
// 
////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////
// Sprites
///////////////////////////////////

// Draw a 2D sprite.
//
// This function is optimized for 2D blitting with arbitrary translation but
// no rotation or scaling. It uses the RDP COPY mode for the highest throughput.
//
// The sprite can have arbitrary size. The function will split the sprite into
// smaller blocks as required to fit the constraints of the RDP texture memory.
//
//   sprite_t *s         a libdragon sprite
//   int sx, sy          screen position (negative coordinates are supported)
//   bool colorkey       if true, color keying will be done (through alpha testing)
//   RdpRect *clip       if not NULL, it select an internal portion of the sprite.
//                       The top-left pixel selected by clip will be the one blitted
//                       at (sx,sy).
//
RdpDisplayList* rdl_sprite(RdpDisplayList *rdl, sprite_t *s, int sx, int sy, bool colorkey, RdpRect *clip);

// Move the screen position of a sprite.
void rdl_sprite_move(RdpDisplayList *rdl, int rdl_offset, int sx, int sy);



///////////////////////////////////
// Fillrect
///////////////////////////////////

// Fill a solid rectangle.
//
// This function can blit a rectangle using a solid color. If the specified color
// has alpha value different from 0xFF, alpha blending will be performed using
// RDP 1-cycle mode; otherwise the faster RDP FILL mode will be used.
//
//   RdpRect rect        top-left coordinate of the recatongle
//   RdpColor32 color    color of the rect (see RDP_COLOR_32())
//
RdpDisplayList* rdl_fillrect(RdpDisplayList *rdl, RdpRect rect, RdpColor32 color);
