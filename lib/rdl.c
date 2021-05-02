#include "rdl.h"

#include <malloc.h>
#include <stdio.h>
#include "rdp_commands.h"

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))

RdpDisplayList* rdl_realloc(RdpDisplayList *rdl, int np) {
    assert(np >= rdl_len(rdl));
    RdpDisplayList *nrdl = (RdpDisplayList*)malloc(sizeof(uint64_t)*np + sizeof(RdpDisplayList));
    assert(nrdl != NULL);
    memcpy(nrdl, rdl, sizeof(uint64_t)*rdl_len(rdl) + sizeof(RdpDisplayList));
    rdl_free(rdl);
    return nrdl;
}

RdpDisplayList* rdl_grow_for(RdpDisplayList *rdl, int np) {
    if (!rdl) return rdl_heap_alloc(np);
    if (rdl_nempty(rdl) <= np) return rdl;
    int cap = rdl_len(rdl)+rdl_nempty(rdl);
    return rdl_realloc(rdl, MAX(np, cap*3/2));
}

void rdl_flush(RdpDisplayList *dl) {
    rdl_push(dl, RdpSyncPipe());
    data_cache_hit_writeback_invalidate(dl->__prims, rdl_len(dl)*sizeof(uint64_t));

    // If the display list is already programmed in the RDP, update the end
    // pointer adding new data.
    // volatile uint32_t *RDP = (volatile uint32_t *)0xA4100000;
    // if ((RDP[0] & 0x0FFFFFFF) == ((uint32_t)&dl->__prims[0] & 0x0FFFFFFF)) {
    //     RDP[1] = (uint32_t)dl->__cur | 0xA0000000;
    // }
}

void rdl_exec(RdpDisplayList *rdl)
{
    volatile uint32_t *RDP = (volatile uint32_t *)0xA4100000;

    assertf(rdl->__cur[-1] == RdpSyncPipe(), "rdl_flush() not called before rdl_exec()");

    /* Best effort to be sure we can write once we disable interrupts */
    while (RDP[3] & 0x600) {}

    /* Make sure another thread doesn't attempt to render */
    disable_interrupts();

    /* Clear XBUS/Flush/Freeze */
    RDP[3] = 0x15;
    MEMORY_BARRIER();

    /* Don't saturate the RDP command buffer.  Another command could have been written
     * since we checked before disabling interrupts, but it is unlikely, so we probably
     * won't stall in this critical section long. */
    while (RDP[3] & 0x600) {}

    /* Send start and end of buffer location to kick off the command transfer */
    MEMORY_BARRIER();
    RDP[0] = ((uint32_t)&rdl->__prims[0] | 0xA0000000);
    MEMORY_BARRIER();
    RDP[1] = ((uint32_t)rdl->__cur | 0xA0000000);
    MEMORY_BARRIER();

    /* We are good now */
    enable_interrupts();
}


enum { SPRITE_BLOCK_W = 32, SPRITE_BLOCK_H = 32 };
#define InternalSpriteHeader(w, h) ((0x1ull << 62) | ((w) & 0xFFF) | ((h) & 0xFFF) << 12)

RdpDisplayList *rdl_sprite(RdpDisplayList *rdl, sprite_t *s, int sx, int sy, bool transparent, RdpRect *clip) {
    int s0 = 0, t0 = 0;
    int width = s->width;
    int height = s->height;
    if (clip) {
        s0 = clip->x0;
        t0 = clip->y0;
        width = MIN(width-clip->x0, clip->x1-clip->x0);
        height = MIN(height-clip->y0, clip->y1-clip->y0);
    }

    int ntx = (width + SPRITE_BLOCK_W - 1) / SPRITE_BLOCK_W;
    int nty = (height + SPRITE_BLOCK_H - 1) / SPRITE_BLOCK_H;

    int tf, dsdx, dsdy;
    switch (s->bitdepth) {
    case 2: tf = RDP_TILE_SIZE_16BIT; break;
    case 4: tf = RDP_TILE_SIZE_32BIT; break;
    default: assertf(0, "unsupported bitdepth: %d", s->bitdepth);
    }

    int nprims = 6 + ntx*nty*4;
    if (rdl)
        rdl_assert_nempty(rdl, nprims);
    else
        rdl = rdl_heap_alloc(nprims);

    rdl_push(rdl, InternalSpriteHeader(width, height));
    rdl_push(rdl, RdpSetOtherModes(SOM_CYCLE_COPY | (transparent ? SOM_ALPHA_COMPARE : 0)));
    rdl_push(rdl, RdpSetTexImage(RDP_TILE_FORMAT_RGBA, tf, (uint32_t)s->data, s->width));
    rdl_push(rdl, RdpSetTile(RDP_TILE_FORMAT_RGBA, tf, SPRITE_BLOCK_W*s->bitdepth/8, 0, 0));
    rdl_push(rdl, RdpSetBlendColor(0x0001)); // alpha threshold value
    dsdx = 4<<10; dsdy = 1<<10;

    for (int y=0; y < height; y+=SPRITE_BLOCK_H) {
        int bh = MIN(SPRITE_BLOCK_H, height - y);
        int yc = y+sy;
        int t = y+t0;

        if (yc+bh <= 0) continue;
        if (yc < 0) {
            t -= yc;
            bh += yc;
            yc = 0;
        }

        for (int x=0; x < width; x+=SPRITE_BLOCK_W) {
            int bw = MIN(SPRITE_BLOCK_W, width - x);
            int xc = x+sx;
            int s = x+s0;

            if (xc+bw <= 0) continue;
            if (xc < 0) {
                s -= xc;
                bw += xc;
                xc = 0;
            }

            rdl_push(rdl, RdpLoadTileFX(0, s<<2, t<<2, (s+SPRITE_BLOCK_W-1)<<2, (t+SPRITE_BLOCK_H-1)<<2));
            rdl_push(rdl, RdpTextureRectangle1FX(0, xc<<2, yc<<2, (xc+bw-1)<<2, (yc+bh-1)<<2));
            rdl_push(rdl, RdpTextureRectangle2FX(s<<5, t<<5, dsdx, dsdy));
            rdl_push(rdl, RdpSyncTile());
        }
    }

    rdl_flush(rdl);
    return rdl;
}

void rdl_sprite_move(RdpDisplayList *rdl, int rdl_offset, int sx, int sy) {
    uint64_t *h = &rdl->__prims[rdl_offset];

    int width = h[0] & 0xFFF;
    int height = (h[0] >> 12) & 0xFFF;

    h += 5;
    for (int y=0; y < height; y+=SPRITE_BLOCK_H) {
        int bh = MIN(SPRITE_BLOCK_H, height - y);
        int yc = y+sy;

        if (yc+bh <= 0) continue;
        if (yc < 0) {
            bh += yc;
            yc = 0;
        }

        for (int x=0; x < width; x+=SPRITE_BLOCK_W) {
            int bw = MIN(SPRITE_BLOCK_W, width - x);
            int xc = x+sx;

            if (xc+bw <= 0) continue;
            if (xc < 0) {
                bw += xc;
                xc = 0;
            }

            h[1] = RdpTextureRectangle1FX(0, xc<<2, yc<<2, (xc+bw-1)<<2, (yc+bh-1)<<2);
            h += 4;
        }
    }
}

RdpDisplayList* rdl_fillrect(RdpDisplayList *rdl, RdpRect rect, RdpColor32 color) {
    if (rdl) rdl_assert_nempty(rdl, 5);
    else rdl = rdl_heap_alloc(5);

    if ((color & 0xFF) == 0) {
        uint8_t r = (color >> 24) & 0xFF;
        uint8_t g = (color >> 16) & 0xFF;
        uint8_t b = (color >>  8) & 0xFF;

        rdl_push(rdl, RdpSetOtherModes(SOM_CYCLE_FILL));
        rdl_push(rdl, RdpSetFillColor16(RDP_COLOR16(r,g,b,0)));
        rdl_push(rdl, RdpFillRectangleI(rect.x0, rect.y0, rect.x1, rect.y1));
    } else {
        rdl_push(rdl, RdpSetOtherModes(SOM_CYCLE_1 | SOM_BLENDING |
            (0<<28) | (1<<24) | (3<<20) | (0<<16) |
            (0<<30) | (1<<26) | (3<<22) | (0<<18)
        ));
        rdl_push(rdl, RdpSetFogColor(RDP_COLOR32(0x00,0x00,0x00,0x80)));
        rdl_push(rdl, RdpSetBlendColor(RDP_COLOR32(0x00,0x00,0x00,0x80)));
        //*rdl_push(rdl) = RdpSetFillColor(COLOR16(0,0,0,1));
        //*rdl_push(rdl) = RdpFillRectangle(x0<<2, y0<<2, x1<<2, y1<<2);
        assertf(0, "not implemented");
    }

    rdl_flush(rdl);
    return rdl;
}
