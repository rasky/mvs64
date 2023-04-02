#include <regdef.h>
#include "m64k_internal.h"
#include "m64k_config.h"

    .set noreorder

.macro jal_and_j func, label
    lui ra, %hi(\label)
    j \func
    addi ra, %lo(\label)
.endm

.macro zx64 dst, src
    dsll \dst, \src, 32
    dsrl \dst, 32
.endm

# Currently, we use TLBs for memory accesses, so we can access
# the 68k memory map directly.
.macro map_m68k reg, src
    .ifnb \src
    sll \reg, \src, 8
    .else
    sll \reg, 8
    .endif
    srl \reg, 8
    #if M64K_MEMORY_BASE != 0
    add \reg, M64K_MEMORY_BASE
    #endif
.endm

.macro check_addr_error reg, label
    #if M64K_CONFIG_ADDRERR
    .set noat
    andi $1, \reg, 1
    bnez $1, \label
    .set at
    #endif
.endm

.macro breakpoint
    tne ra,ra
.endm

     .section .sdata
pc_diff:      .long 0
flag_x:       .byte 0

optable0:
    .long   op4_0000, op4_0001, op4_0010, op4_0011, op4_0100, op4_0101, op4_0110, op4_0111
    .long   op4_1000, op4_1001, op4_1010, op4_1011, op_and,   op4_1101, op4_1110, op4_1111
optable1:
    .long   op8_0000, op8_0001, op_andi,  op8_0011, op8_0100, op8_0101, op8_0110, op8_0111
    .long   op8_1000, op8_1001, op8_1010, op8_1011, op8_1100, op8_1101, op8_1110, op8_1111

    .text

#define OP(x)       .balign 32; op_##x
#define m64k        a0
#define m_cycles    a1

#define opcode      s0
#define eaptr       s1
#define dptr        s2
#define opsize      s4
#define result      s5
#define flag_nv     s6    // bit 31: N; bit 63: N^V
#define flag_zc     s7    // bit 0..31: !Z; bit 32: C

#define eaptr32       t4
#define m_pc          t5
#define m_cyc_table   t6
#define rmw_op        t7
#define flag_bitsize  t8

.macro la_m64k  reg, offset,base
    add \reg, m64k, \base
    .if \offset
        addi \reg, \offset
    .endif
.endm

.macro lw_m64k  reg, offset,base
    .ifnb \base
        .set noat
        add $1, m64k, \base
        lw \reg, \offset($1)
        .set at
    .else
        lw \reg, \offset(m64k)
    .endif
.endm

.macro sw_m64k  reg, offset,base
    .ifnb \base
        .set noat
        add $1, m64k, \base
        sw \reg, \offset($1)
        .set at
    .else
        sw \reg, \offset(m64k)
    .endif
.endm

    .globl _m64k_asmrun
_m64k_asmrun:
    addiu sp,sp,-128
    sd s0,32(sp)
    sd s1,40(sp)
    sd s2,48(sp)
    sd s3,56(sp)
    sd s4,64(sp)
    sd s5,72(sp)
    sd s6,80(sp)
    sd s7,88(sp)
    sd s8,96(sp)
    sd ra,104(sp)

    la m_cyc_table, __m64k_cycle_table
    lw t1, M64K_OFF_PC(m64k)
    lw t0, M64K_OFF_SR(m64k)

    # Convert PC to the N64 address space.
    map_m68k m_pc, t1
    sub t1, m_pc
    sw t1, pc_diff                    # Save difference between m_pc and PC

    srl t1, t0, 11                    # Isolate supervisor bit
    andi t1, 4
    lw_m64k t2, M64K_OFF_USP,t1       # Copy USP/SSP to A7
    sw_m64k t2, M64K_OFF_AREGS+7*4

    # Extract flags from SR into flag_nv / flag_zc
    # SR Format: ---X NZVC
    srl t1, t0, 4          # X to bit 0
    andi t1, 1
    sb t1, flag_x

    sll t1, t0, 28         # N to bit 31 (and 63)
    dsll flag_nv, t0, 62   # V to bit 63 (and 0 to 31)
    xor flag_nv, t1       

    andi t1, t0, 4         # Isolate Z
    xori t1, 4             # !Z
    dsll flag_zc, t0, 32   # C to bit 32
    or flag_zc, t1

main_loop:
    blez m_cycles, main_loop_exit
    lhu opcode, 0(m_pc)
    #if M64K_CONFIG_ADDRERR
    sw opcode, M64K_OFF_IR(m64k)
    #endif
    add t1, opcode, m_cyc_table
    lbu t1, 0(t1)
    srl t0, opcode, 12
    sll t0, 2
    lw t0, optable0(t0)
    addi m_pc, 2
    jr t0
    sub m_cycles, t1
op4_0000:
    srl t0, opcode, 6
    andi t0, 0xF << 2
    lw t0, optable1(t0)
    jr t0
    nop

main_loop_exit:
    lw t0, pc_diff
    add m_pc, t0
    sw m_pc, M64K_OFF_PC(m64k)
    #if !M64K_CONFIG_ADDRERR
    sw opcode, M64K_OFF_IR(m64k)
    #endif

    # Store flags into SR
    dsrl t0, flag_nv, 63    # N^V
    srl t1, flag_nv, 31     # N
    xor t0, t1              # V
    sll t0, 1
    sll t1, 3
    or t0, t1

    dsrl t1, flag_zc, 32     # C
    andi t1, 1
    or t0, t1
    dsll t1, flag_zc, 32
    seq t1, zero             # Z
    sll t1, 2
    or t0, t1

    lbu t1, flag_x           # X
    sne t1, zero, t1
    sll t1, 4
    or t0, t1
    
    lw t1, M64K_OFF_SR(m64k)
    and t1, ~0x1f
    or t1, t0
    sw t1, M64K_OFF_SR(m64k)

    srl t1, 11                        # Isolate supervisor bit
    andi t1, 4
    lw_m64k t2, M64K_OFF_AREGS+7*4    # Copy A7 to USP/SSP
    sw_m64k t2, M64K_OFF_USP,t1       

    ld s0,32(sp)
    ld s1,40(sp)
    ld s2,48(sp)
    ld s3,56(sp)
    ld s4,64(sp)
    ld s5,72(sp)
    ld s6,80(sp)
    ld s7,88(sp)
    ld s8,96(sp)
    ld ra,104(sp)
    addiu sp,sp,128
    jr ra
    move v0, m_cycles

#if M64K_CONFIG_ADDRERR
    .data 
    # Table to help setting up address error. Each entry is a pair of
    # 8-bit values, first is the PC offset to rollback, second is the
    # "FC" code which is put into the stack frame.
adderr_pcoff:  .byte  0,0,  0,0, -2,5, -2,5, -2,5, -2,5, -2,5,  0,0
adderr7_pcoff: .byte -2,5, -2,5, -2,5, -2,5,  0,0,  0,0,  0,0,  0,0

    .text
    .set reorder
read_address_error_exit:
    li t2, 1<<4
    j address_error_exit
write_address_error_exit:
    li t2, 0
address_error_exit:
    li t0, M64K_PENDINGEXC_ADDRERR
    sw_m64k t0, M64K_OFF_PENDINGEXC+0
    sw_m64k eaptr32, M64K_OFF_PENDINGEXC+4

    # Fetch the PC/FC table entry using EA
    lw opcode, M64K_OFF_IR(m64k)
    srl t0, opcode, 2
    andi t0, 0x7<<1
    la t1, adderr_pcoff
    bne t0, 7<<1, compute_fc
adderr_off7:
    andi t0, opcode, 7
    addi t0, 8
    sll t0, 1
compute_fc:
    add t1, t0
    lb t0, 0(t1)    # PC offset
    lbu t1, 1(t1)   # FC code
    add m_pc, t0    # Rollback PC
    andi t0, opcode, 0xFFE0
    or t0, t1                            # Merge FC (4 bits) with opcode
    or t0, t2                            # Merge read/write flag
    sw_m64k t0, M64K_OFF_PENDINGEXC+8    # Store FC

    j main_loop_exit
    .set noreorder
#endif


###############################################################
# Immediate computation
###############################################################

    .text
    .balign 32
decode_imm:
    srl t0, opcode, 6
    andi t0, 0x3
    beqz t0, imm_8
    addi t0, -1
    beqz t0, imm_16
imm_32:
    move dptr, m_pc
    addi m_pc, 4
    jr ra
    li opsize, 4
imm_16:
    addi dptr, m_pc, -2
    addi m_pc, 2
    jr ra
    li opsize, 2
imm_8:
    addi dptr, m_pc, -2
    addi m_pc, 2
    jr ra
    li opsize, 1

###############################################################
# EA computation
###############################################################

    .data
ea_table:
    .long   ea_000, ea_001, ea_010, ea_011, ea_100, ea_101, ea_110, ea_111
ea7_table:
    .long   ea7_000, ea7_001, ea7_010, ea7_011, ea7_100, ea7_101, ea7_110, ea7_111

    .text
    .balign 32
     # Decode EA. opsize must be set to 1, 2 or 4. 
     # When 0, extracts from opcode
decode_ea:
    srl t0, opcode, 1
    andi t0, 0x7 << 2
    andi t1, opcode, 0x7
    lw t0, ea_table(t0)
    jr t0
    sll t1, 2
ea_111: # Register-less EAs
    lw t1, ea7_table(t1)
    jr t1
    nop

.macro decode_opsize
    bnez opsize, 1f        # if opsize was not specified
    srl opsize, opcode, 6  # extract it from opcode bit 6-7
    andi opsize, 0x3
    li t0, 1
    sllv opsize, t0, opsize
1:
.endm

    .set reorder
ea_000: # Dn
    decode_opsize
    la_m64k eaptr, M64K_OFF_DREGS+4,t1
    sub eaptr, opsize
    li eaptr32, 0
    jr ra
ea_001: # An
    decode_opsize
    la_m64k eaptr, M64K_OFF_AREGS+4,t1
    sub eaptr, opsize
    li eaptr32, 0
    jr ra
ea_010: # (An)
    lw_m64k eaptr32, M64K_OFF_AREGS,t1
    map_m68k eaptr, eaptr32
    jr ra
ea_011: # (An)+
    # FIXME: align sp byte access
    lw_m64k eaptr32, M64K_OFF_AREGS,t1
    decode_opsize
    add t0, eaptr32, opsize
    bne t1, 7*4, 1f   # if A7 (stack pointer), align to word
    add t0, 1
    and t0, ~1
1:
    sw_m64k t0, M64K_OFF_AREGS,t1
    map_m68k eaptr, eaptr32
    jr ra
ea_100: # -(An)
    lw_m64k eaptr32, M64K_OFF_AREGS,t1
    decode_opsize
    sub eaptr32, opsize
    bne t1, 7*4, 1f    # if A7 (stack pointer), align to word
    and eaptr32, ~1
1:
    sw_m64k eaptr32, M64K_OFF_AREGS,t1
    map_m68k eaptr, eaptr32
    jr ra
ea_101: # (d16,An)
    lw_m64k eaptr32, M64K_OFF_AREGS,t1
    lh t0, 0(m_pc)
    addi m_pc, 2
    addu eaptr32, t0
    map_m68k eaptr, eaptr32
    jr ra
ea7_011: # (d8,PC,Xn)
    #if M64K_CONFIG_ADDRERR
    lw t0, pc_diff
    add eaptr32, m_pc, t0     # Compute exact eaptr32 in case it's pushed on the stack for the exception
    #else
    move eaptr32, m_pc
    #endif
    j ea_110_inner    
ea_110: # (d8,An,Xn)
    lw_m64k eaptr32, M64K_OFF_AREGS,t1   # read An
ea_110_inner:
    lbu t1, 0(m_pc)  # read extension word
    lb t0, 1(m_pc)   # read 8-bit displacement
    addi m_pc, 2
    addu eaptr32, t0
    srl t0, t1, 2       # calculate pointer to Xn
    andi t0, 0xF << 2
    andi t2, t1, 0x8
    add t0, m64k
    lw t1, M64K_OFF_DREGS(t0)
    bnez t2, 1f
    lh t1, M64K_OFF_DREGS+2(t0)
1:  addu eaptr32, t1
    map_m68k eaptr, eaptr32
    jr ra
ea7_000: # (xxx).W
    lh eaptr32, 0(m_pc)
    addi m_pc, 2
    map_m68k eaptr, eaptr32
    jr ra
ea7_001: # (xxx).L
    lwl eaptr32, 0(m_pc)
    lwr eaptr32, 3(m_pc)
    addi m_pc, 4
    map_m68k eaptr, eaptr32
    jr ra
ea7_010: # (d16,PC)
    move eaptr32, m_pc   # FIXME! this should use the update 32-bit PC!
    lh t0, 0(m_pc)
    addi m_pc, 2
    add eaptr32, t0
    map_m68k eaptr, eaptr32
    jr ra
ea7_100: # #<data>
    move eaptr32, m_pc
    and t0, opcode, 0xC0   # isolate opsize in opcode
    addi m_pc, 2
    bnez t0, 1f
    addi eaptr32, 1           # if opsize is byte, skip a byte
1:
    map_m68k eaptr, eaptr32
    jr ra

ea7_101: ea7_110: ea7_111:
    move a0, opcode
    jal __m64k_assert_invalid_ea
    addiu a1, m_pc, -2
    .set noreorder

###############################################################
# RMW accesses
###############################################################

    .data
rmw_table:
    .long   rmw8_easrc, rmw16_easrc, rmw32_easrc, rmw32_easrc
    .long   rmw8_eadst, rmw16_eadst, rmw32_eadst, rmw32_eadst

    .text
    .set reorder
rmw8_easrc:
    li flag_bitsize, 24
    lb t0, 0(eaptr)
    lb t1, 3(dptr)
    jalr rmw_op
    sb result, 3(dptr)
    j main_loop
rmw16_easrc:
    check_addr_error eaptr32, read_address_error_exit
    li flag_bitsize, 16
    lh t0, 0(eaptr)
    lh t1, 2(dptr)
    jalr rmw_op
    sh result, 2(dptr)
    j main_loop
rmw32_easrc:
    check_addr_error eaptr32, read_address_error_exit
    li flag_bitsize, 0
    lwl t0, 0(eaptr)
    lwr t0, 3(eaptr)
    lw t1, 0(dptr)
    jalr rmw_op
    sw result, 0(dptr)
    j main_loop
rmw8_eadst:
    li flag_bitsize, 24
    lb t1, 3(dptr)
    lb t0, 0(eaptr)
    jalr rmw_op
    sb result, 0(eaptr)
    j main_loop
rmw16_eadst:
    check_addr_error eaptr32, read_address_error_exit
    li flag_bitsize, 16
    lh t1, 2(dptr)
    lh t0, 0(eaptr)
    jalr rmw_op
    sh result, 0(eaptr)
    j main_loop
rmw32_eadst:
    check_addr_error eaptr32, read_address_error_exit
    li flag_bitsize, 0
    lwl t1, 0(dptr)   # NOTE: this is to handle immediate case (where dptr is PC-relative)
    lwr t1, 3(dptr)
    lwl t0, 0(eaptr)
    lwr t0, 3(eaptr)
    jalr rmw_op
    swl result, 0(eaptr)
    swr result, 3(eaptr)
    j main_loop
    .set noreorder

###############################################################
# Opcodes
###############################################################

.macro rmw opmode, opfunc
    .set reorder
    lw t0, rmw_table(\opmode)
    la rmw_op, \opfunc
    jr t0
    .set noreorder
.endm

OP(andi):
    ori opcode, 1<<8    # force rmw with eadst
    jal_and_j decode_imm, op_and_body
OP(and):
    srl dptr, opcode, 7       # get data register pointer
    andi dptr, 0x7 << 2
    la_m64k dptr, M64K_OFF_DREGS,dptr
    li opsize, 0
op_and_body:
    jal decode_ea             # get EA pointer
    nop
    
    srl t0, opcode, 4         # do RMW
    andi t0, 0x7 << 2
    rmw t0, op_and_rmwimpl
op_and_rmwimpl:
    .set reorder
    and result, t0, t1
    sllv flag_nv, result, flag_bitsize
    zx64 flag_zc, flag_nv
    jr ra
    .set noreorder


          op4_0001: op4_0010: op4_0011: op4_0100: op4_0101: op4_0110: op4_0111:
op4_1000: op4_1001: op4_1010: op4_1011:           op4_1101: op4_1110: op4_1111:

op8_0000: op8_0001:           op8_0011: op8_0100: op8_0101: op8_0110: op8_0111:
op8_1000: op8_1001: op8_1010: op8_1011: op8_1100: op8_1101: op8_1110: op8_1111:

    move a0, opcode
    jal __m64k_assert_invalid_opcode
    addiu a1, m_pc, -2
