#include <rsp_queue.inc>
#include "lib/rdp_commands.h"
#define TILE0 0
#define TILE1 1

    .data

	RSPQ_BeginOverlayHeader
        RSPQ_DefineCommand cmd_fix_init, 4     # 0x0
		RSPQ_DefineCommand cmd_fix_draw, 8     # 0x1
	RSPQ_EndOverlayHeader

RSPQ_BeginSavedState
LAST_PALETTE: .long 0
RSPQ_EndSavedState


.data

# [0xa018b8b8] fd481c030018c5b0    SET_TEX_IMAGE    dram=0018c5b0 w=4 ci8
# [0xa018b8c0] f40000000100c01c    LOAD_TILE        tile=1 st=(0.00,0.00)-(3.00,7.00)
# [0xa018b8c8] f540020000000000    SET_TILE         tile=0 ci4 tmem[0x0,line=8] pal=0 mask=[0, 0]
# [0xa018b8d0] f20000000001c01c    SET_TILE_SIZE    tile=0 st=(0.00,0.00)-(7.00,7.00)
# [0xa018b8d8] e401c01c00000000    TEX_RECT         tile=0 xy=(0.00,0.00)-(7.00,7.00)
# [0xa018b8e0] 0000000010000400                     st=(0.00,0.00) dst=(4.00000,1.00000)

    .align 4
FIX_SPRITE_LOAD:
    .quad RdpSyncPipe()
    .quad RdpSyncLoad()
    .quad RdpSetTexImage(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_8BIT, 0, 4)
    .quad RdpLoadTileI(TILE1, 0, 0, 4-1, 8-1)
FIX_PALETTE_SET:
    .quad RdpSyncTile()
    .quad RdpSyncTile()
    .quad RdpSetTile(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_4BIT, 1, 0, TILE0)
    .quad RdpSetTileSizeI(TILE0, 0, 0, 8-1, 8-1)
FIX_SPRITE_DRAW:
    .quad RdpTextureRectangle1I(TILE0, 0, 0, 8-1, 8-1)   # x0,y1,x1,y1
    .quad RdpTextureRectangle2I(0, 0, 4, 1)              # s,t,ds,dt


    .text


    .func cmd_fix_init
cmd_fix_init:
    li t0, -1
    jr ra
    sw t0, %lo(LAST_PALETTE)
    .endfunc

    ######################################################
    # cmd_fix_draw - draw a sprite in the fix layer
    #
    # a0: sprite pointer (or NULL if same of last one)
    # a1: bit 0 .. 9: y position
    #     bit 10..19: x position
    #     bit 20..23: palette
    #
    ######################################################

    #define vspriteload0   $v01
    #define vspriteload1   $v02
    #define vpaletteset0   $v03
    #define vpaletteset1   $v04
    #define vfixdraw       $v05
    #define last_palette   t8
    #define ypos           t7
    #define xpos           t6

    .func cmd_fix_draw
cmd_fix_draw:
    li s0, %lo(FIX_SPRITE_LOAD)
    li s3, %lo(RDPQ_CMD_STAGING)
    lqv vspriteload0,  0,s0
    lqv vspriteload1, 16,s0
    lqv vpaletteset0, 32,s0
    lqv vpaletteset1, 48,s0
    lqv vfixdraw,     64,s0

check_sprite_load:
    # Isolate the sprite pointer. If not zero, we must draw it
    and a0, 0x003fffff
    beqz a0, check_palette_load
    lw last_palette, %lo(LAST_PALETTE)

    # Copy the sprite load sequence into the rdpq staging buffer (16 bytes)
    sqv vspriteload0,  0,s3
    sqv vspriteload1, 16,s3

    # Store the updated pointer
    sw a0, 20(s3)
    addi s3, 32

check_palette_load:
    # Isolate the palette number (4 bits) from a1. The palette number
    # is stored in the same position where it will go in the SET_TILE command.
    and t1, a1, 0xF << 20

    # Compare the palette with the last one we configured.
    # If it changed, we need to reconfigure the tile
    beq t1, last_palette, fix_draw
    sw t1, %lo(LAST_PALETTE)

    # Copy the palette set sequence into the rdpq staging buffer (16 bytes)
    sqv vpaletteset0,  0,s3
    sqv vpaletteset1, 16,s3
    
    # Store the updated palette number into the SET_TILE command
    lw t0, 20(s3)
    or t0, t1
    sw t0, 20(s3)
    addi s3, 32

fix_draw:
    # Store the draw command into the output buffer
    sqv vfixdraw, 0,s3

    # Extract x and y components from input, and store them
    # into bits 14..24 and 2..12 of t0. This is the format
    # expected for the set rectangle command.
    andi ypos, a1, 0x3ff
    srl xpos, a1, 10
    andi xpos, xpos, 0x3ff
    sll xpos, 14
    sll ypos, 2
    or t0, xpos, ypos

    lw t1, 0(s3)
    lw t2, 4(s3)
    add t1, t0
    add t2, t0
    sw t1, 0(s3)
    sw t2, 4(s3)

    addi s3, 16

    # Send to RDP and go back to main loop
    j RDPQ_Send
    li s4, %lo(RDPQ_CMD_STAGING)

    .endfunc

#include <rsp_rdpq.inc>
