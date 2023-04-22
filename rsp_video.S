#include <rsp_queue.inc>
#include "lib/rdp_commands.h"
#define TILE0 0
#define TILE1 1
#define TILE7 7

    .data

	RSPQ_BeginOverlayHeader
        RSPQ_DefineCommand cmd_fix_init, 4     # 0x0
		RSPQ_DefineCommand cmd_fix_draw, 8     # 0x1
        RSPQ_DefineCommand cmd_sprite_draw, 8  # 0x2
        RSPQ_DefineCommand cmd_pal_convert, 8  # 0x3
	RSPQ_EndOverlayHeader

RSPQ_BeginSavedState
LAST_PALETTE:  .long 0
PALETTE_CACHE: .half -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
RSPQ_EndSavedState

.macro vsll_acc vdstreg, vsrcreg, qty
    .if (\qty == 7)
        vmadn \vdstreg, \vsrcreg, vshift, e(0)
    .elseif (\qty == 6)
        vmadn \vdstreg, \vsrcreg, vshift, e(1)
    .elseif (\qty == 5)
        vmadn \vdstreg, \vsrcreg, vshift, e(2)
    .elseif (\qty == 4)
        vmadn \vdstreg, \vsrcreg, vshift, e(3)
    .elseif (\qty == 3)
        vmadn \vdstreg, \vsrcreg, vshift, e(4)
    .elseif (\qty == 2)
        vmadn \vdstreg, \vsrcreg, vshift, e(5)
    .elseif (\qty == 1)
        vmadn \vdstreg, \vsrcreg, vshift, e(6)
    .elseif (\qty == 0)
        vmadn \vdstreg, \vsrcreg, vshift, e(7)
    .elseif (\qty >= 8 && \qty <= 15)
        .error "Use vsll8_acc for quantities in range 8-15"
    .else
        .error "Invalid quantity in vsll_acc"
    .endif
.endm

.macro vsrl_acc vdstreg, vsrcreg, qty
    .if (\qty == 1)
        vmadl \vdstreg, \vsrcreg, vshift8, e(0)
    .elseif (\qty == 2)
        vmadl \vdstreg, \vsrcreg, vshift8, e(1)
    .elseif (\qty == 3)
        vmadl \vdstreg, \vsrcreg, vshift8, e(2)
    .elseif (\qty == 4)
        vmadl \vdstreg, \vsrcreg, vshift8, e(3)
    .elseif (\qty == 5)
        vmadl \vdstreg, \vsrcreg, vshift8, e(4)
    .elseif (\qty == 6)
        vmadl \vdstreg, \vsrcreg, vshift8, e(5)
    .elseif (\qty == 7)
        vmadl \vdstreg, \vsrcreg, vshift8, e(6)
    .elseif (\qty == 8)
        vmadl \vdstreg, \vsrcreg, vshift8, e(7)
    .elseif (\qty >= 9 && \qty <= 15)
        .error "Use vsrl8_acc for quantities in range 9-15"
    .else
        .error "Invalid quantity in vsrl_acc"
    .endif
.endm

.macro vsrl8_acc vdstreg, vsrcreg, qty
    .if (\qty == 9)
        vmadl \vdstreg, \vsrcreg, vshift, e(0)
    .elseif (\qty == 10)
        vmadl \vdstreg, \vsrcreg, vshift, e(1)
    .elseif (\qty == 11)
        vmadl \vdstreg, \vsrcreg, vshift, e(2)
    .elseif (\qty == 12)
        vmadl \vdstreg, \vsrcreg, vshift, e(3)
    .elseif (\qty == 13)
        vmadl \vdstreg, \vsrcreg, vshift, e(4)
    .elseif (\qty == 14)
        vmadl \vdstreg, \vsrcreg, vshift, e(5)
    .elseif (\qty == 15)
        vmadl \vdstreg, \vsrcreg, vshift, e(6)
    .elseif (\qty >= 1 && \qty <= 8)
        .error "Use vsrl_acc for quantities in range 1-8"
    .else
        .error "Invalid quantity in vsrl8_acc"
    .endif
.endm

#########################################################################
# PALETTES
#########################################################################

    .data
    .align 4
VPALCONST: 
    .half 0x0F00
    .half 0x00F0
    .half 0x000F
    .half 0x4000
    .half 0x2000
    .half 0x1000
    .half 0
    .half 0

VPALCONST2:
    .half 0xFFFE
    .half 0xFFFF
    .half 0xFFFF
    .half 0xFFFF
    .half 0xFFFF
    .half 0xFFFF
    .half 0xFFFF
    .half 0xFFFF

    .bss
    .align 4
PAL_BUFFER: .dcb.b 0x800


    .text

    #######################################
    # a0: RDRAM src
    # a1: RDRAM dst
    #######################################

    #define vconst     $v16
    #define valphamask $v17
    #define kR1    vconst.e0
    #define kG1    vconst.e1
    #define kB1    vconst.e2
    #define kR2    vconst.e3
    #define kG2    vconst.e4
    #define kB2    vconst.e5

    #define vcolor  $v01
    #define vr1     $v02
    #define vg1     $v03
    #define vb1     $v04
    #define vr2     $v05
    #define vg2     $v06
    #define vb2     $v07


    .func cmd_pal_convert
cmd_pal_convert:
    li s0, %lo(VPALCONST)
    lqv vconst,      0,s0
    lqv valphamask, 16,s0

    li s4, %lo(PAL_BUFFER)
    move s0, a0
    jal DMAIn
    li t0, DMA_SIZE(0x800, 1)

    li t0, 0x800
pal_loop:
    lqv vcolor, 0,s4
    vand vr1, vcolor, kR1
    vand vg1, vcolor, kG1
    vand vb1, vcolor, kB1
    vand vr2, vcolor, kR2
    vand vg2, vcolor, kG2
    vand vb2, vcolor, kB2
    vsll     vcolor, vr1, 4
    vsll_acc vcolor, vg1, 3
    vsll_acc vcolor, vb1, 2
    vsrl_acc vcolor, vr2, 3
    vsrl_acc vcolor, vg2, 7
    vsrl8_acc vcolor, vb2, 11

    vor vcolor, vcolor, K1
    vand vcolor, valphamask
    sqv vcolor, 0,s4

    lqv vcolor, 16,s4
    vand vr1, vcolor, kR1
    vand vg1, vcolor, kG1
    vand vb1, vcolor, kB1
    vand vr2, vcolor, kR2
    vand vg2, vcolor, kG2
    vand vb2, vcolor, kB2
    vsll     vcolor, vr1, 4
    vsll_acc vcolor, vg1, 3
    vsll_acc vcolor, vb1, 2
    vsrl_acc vcolor, vr2, 3
    vsrl_acc vcolor, vg2, 7
    vsrl8_acc vcolor, vb2, 11

    vor vcolor, vcolor, K1
    sqv vcolor, 16,s4

    addiu t0, -32
    bnez t0, pal_loop
    addiu s4, 32

    li s4, %lo(PAL_BUFFER)
    move s0, a1
    jal DMAOut
    li t0, DMA_SIZE(0x800, 1)

    j RSPQ_Loop
    nop
    
    .endfunc


#########################################################################
# FIX LAYER
#########################################################################

# [0xa018b8b8] fd481c030018c5b0    SET_TEX_IMAGE    dram=0018c5b0 w=4 ci8
# [0xa018b8c0] f40000000100c01c    LOAD_TILE        tile=1 st=(0.00,0.00)-(3.00,7.00)
# [0xa018b8c8] f540020000000000    SET_TILE         tile=0 ci4 tmem[0x0,line=8] pal=0 mask=[0, 0]
# [0xa018b8d0] f20000000001c01c    SET_TILE_SIZE    tile=0 st=(0.00,0.00)-(7.00,7.00)
# [0xa018b8d8] e401c01c00000000    TEX_RECT         tile=0 xy=(0.00,0.00)-(7.00,7.00)
# [0xa018b8e0] 0000000010000400                     st=(0.00,0.00) dst=(4.00000,1.00000)

    .data
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

#########################################################################
# SPRITES
#########################################################################

    .data

    .align 4
SPRITE_LOAD_PIXELS:
    .quad RdpSyncPipe()
    .quad RdpSyncLoad()
    .quad RdpSyncTile()
    .quad RdpSetTexImage(RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_16BIT, 0, 4)
    .quad RdpSetTile(RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_16BIT, 0, 0, TILE1)
    .quad RdpSetTile(RDP_TILE_FORMAT_INDEX, RDP_TILE_SIZE_4BIT, 8/8, 0, TILE0)
    .quad RdpSetTileSizeI(TILE0, 0, 0, 16-1, 16-1)
#    .quad RdpLoadBlock(

SPRITE_LOAD_PALETTE:
    #rdpq_set_texture_image_raw(0, PhysicalAddr(tlut), FMT_RGBA16, num_colors, 1);
    #rdpq_set_tile(RDPQ_TILE_INTERNAL, FMT_I4, TMEM_PALETTE_ADDR + color_idx*2*4, num_colors, 0);
    #rdpq_load_tlut_raw(RDPQ_TILE_INTERNAL, 0, num_colors);
    .quad RdpSyncPipe()
    .quad RdpSyncLoad()
    .quad RdpSetTexImage(RDP_TILE_FORMAT_RGBA, RDP_TILE_SIZE_16BIT, 0, 16)
    .quad RdpSetTile(RDP_TILE_FORMAT_I, RDP_TILE_SIZE_4BIT, 0x800, 16/8, TILE7)
    .quad RdpLoadTlut(TILE7, 0, 16)

SPRITE_DRAW:
    .quad RdpTextureRectangle1I(TILE0, 0, 0, 16-1, 16-1)   # x0,y1,x1,y1
    .quad RdpTextureRectangle2I(0, 0, 1, 1)                # s,t,ds,dt

CACHE_INDICES:
    .byte 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16

    .text

    ######################################################
    # cmd_sprite_draw - draw a sprite
    #
    # a0: sprite pointer
    # a1: bit 0 .. 9: y position
    #     bit 10..19: x position
    #     bit 20..27: palette
    #
    ######################################################

    #define palettenum    t8
    #define palslot       t7

    #define vcache0       $v01
    #define vcache1       $v02
    #define kpalettenum   $v03.e0
    #define vindex0       $v04
    #define vindex1       $v05
    #define vsearch0      $v06
    #define vsearch1      $v07
    #define v___          $v27

    .func cmd_sprite_draw
cmd_sprite_draw:
    li s3, %lo(RDPQ_CMD_STAGING)

    li s0, %lo(PALETTE_CACHE)
    li s1, %lo(CACHE_INDICES)
    lqv vcache0,  0,s0
    lqv vcache1, 16,s0
    luv vindex0,  0,s1
    luv vindex1,  8,s1

    srl palettenum, a1, 20
    mtc2 palettenum, kpalettenum

    # Check if the palette is already in the cache
    veq v___, vcache0, kpalettenum
    vmrg vsearch0, vzero, vindex0 
    veq v___, vcache1, kpalettenum
    vmrg vsearch1, vzero, vindex1
    vaddc vsearch0, vsearch1
    vaddc vsearch0, vsearch0.q1
    vaddc vsearch0, vsearch0.h2
    vaddc vsearch0, vsearch0.e4
    mfc2 palslot, vsearch0.e0
    bnez palslot, load_pixels
    addi palslot, -1

    # Palette not found, we need to load it.
    andi palslot, palettenum, 0xF

load_pixels:




    .endfunc



#include <rsp_rdpq.inc>
