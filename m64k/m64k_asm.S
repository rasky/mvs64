#include <regdef.h>

    .set noreorder

.macro jal_and_j func, label
    j \func
    li ra, %lo(\label)
.endm

.macro zx64 dst, src
    dsll \dst, \src, 32
    dsrl \dst, 32
.endm

# Currently, we use TLBs for memory accesses, so we can access
# the 68k memory map directly.
# Notice that 68k memory accesses are alwasy 16-bit aligned for
# 16-bit and 32-bit sizes, so in general we can never use a direct
# lw/sw to access memory.
#define lb_m68k     lb
#define lbu_m68k    lbu
#define lh_m68k     lh
#define lhu_m68k    lhu
#define lwl_m68k    lwl
#define lwr_m68k    lwr
#define sb_m68k     sb
#define sh_m68k     sh
#define swl_m68k    swl
#define swr_m68k    swr

    .section .sdata

dregs: .long 0,0,0,0,0,0,0,0
aregs: .long 0,0,0,0,0,0,0,0

optable0:
    .long   op4_0000, op4_0001, op4_0010, op4_0011, op4_0100, op4_0101, op4_0110, op4_0111
    .long   op4_1000, op4_1001, op4_1010, op4_1011, op_and,   op4_1101, op4_1110, op4_1111
optable1:
    .long   op8_0000, op8_0001, op_andi,  op8_0011, op8_0100, op8_0101, op8_0110, op8_0111
    .long   op8_1000, op8_1001, op8_1010, op8_1011, op8_1100, op8_1101, op8_1110, op8_1111

#define OP(x)       .balign 32; op_##x

    .text

#define m_pc        a0
#define opcode      s0
#define eaptr       s1
#define dptr        s2
#define m_cycles    s3
#define opsize      s4
#define result      s5
#define flag_nv     s6    // bit 31: N; bit 63: N^V
#define flag_zc     s7    // bit 0..31: !Z; bit 32: C

#define rmw_op        t7
#define flag_bitsize  t8

    .globl m64k_run_internal
m64k_run_internal:
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

main_loop:
    lhu_m68k opcode, 0(m_pc)
    srl t0, opcode, 12
    sll t0, 2
    lw t0, optable0(t0)
    jr t0
    addi m_pc, 2
op4_0000:
    srl t0, opcode, 6
    andi t0, 0xF << 2
    lw t0, optable1(t0)
    jr t0
    nop

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
    move dptr, m_pc
imm_32:
    addi m_pc, 4
    li opsize, 4
    jr ra
    addi m_cycles, -8
imm_16:
    addi m_pc, 2
    li opsize, 2
    jr ra
    addi m_cycles, -4
imm_8:
    addi dptr, m_pc, 1
    addi m_pc, 2
    li opsize, 1
    jr ra
    addi m_cycles, -4

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
    andi t1, t0, 0x7
    lw t0, ea_table(t0)
    jr t0
    sll t1, 2
ea_111: # Register-less EAs
    lw t1, ea7_table(t1)
    jr t1
    nop

    .set reorder
ea_000: # Dn
    la eaptr, dregs(t1)
    jr ra
ea_001: # An
    la eaptr, aregs(t1)
    jr ra
ea_010: # (An)
    lw eaptr, aregs(t1)
    addi m_cycles, -4
    jr ra
ea_011: # (An)+
    # FIXME: align sp byte access
    lw eaptr, aregs(t1)
    beqz opsize, 1f        # if opsize was not specified
    srl opsize, opcode, 6  # extract it from opcode bit 6-7
    andi opsize, 0x3
    li t0, 1
    sllv opsize, t0, opsize
1:
    add t0, eaptr, opsize
    sw t0, aregs(t1)
    addi m_cycles, -4
    jr ra
ea_100: # -(An)
    # FIXME: align sp byte access
    lw eaptr, aregs(t1)
    beqz opsize, 1f        # if opsize was not specified
    srl opsize, opcode, 6  # extract it from opcode bit 6-7
    andi opsize, 0x3
    li t0, 1
    sllv opsize, t0, opsize
1:
    sub eaptr, opsize
    sw eaptr, aregs(t1)
    addi m_cycles, -6
    jr ra
ea_101: # (d16,An)
    lw eaptr, aregs(t1)
    lh_m68k t0, 0(m_pc)
    addi m_pc, 2
    add eaptr, t0
    addi m_cycles, -8
    jr ra
ea_110: # (d8,An,Xn)
    lw eaptr, aregs(t1)   # read An
ea_110_inner:
    lbu_m68k t1, 0(m_pc)  # read extension word
    lb_m68k t0, 1(m_pc)   # read 8-bit displacement
    addi m_pc, 2
    add eaptr, t0
    srl t0, t1, 2       # calculate pointer to Xn
    andi t0, 0xF << 2
    andi t2, t1, 0x800
    lw t0, dregs(t0)
    bnez t2, 1f
    lh t0, dregs+2(t0)
1:
    add eaptr, t0
    addi m_cycles, -10
    jr ra
ea7_000: # (xxx).W
    lh_m68k eaptr, 0(m_pc)
    addi m_pc, 2
    addi m_cycles, -8
    jr ra
ea7_001: # (xxx).L
    lwl_m68k eaptr, 0(m_pc)
    lwr_m68k eaptr, 3(m_pc)
    addi m_pc, 4
    addi m_cycles, -12
    jr ra
ea7_010: # (d16,PC)
    addi eaptr, m_pc, -2
    lh_m68k t0, 0(m_pc)
    addi m_pc, 2
    add eaptr, t0
    addi m_cycles, -8
    jr ra
ea7_011: # (d8,PC,Xn)
    addi eaptr, m_pc, -2
    addi m_cycles, -10
    j ea_110_inner    

 ea7_100: ea7_101: ea7_110: ea7_111:
    jr ra # FIXMe complete
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
    lbu_m68k t0, 0(eaptr)
    lbu t1, 0(dptr)
    jalr rmw_op
    sb result, 0(dptr)
    j main_loop
rmw16_easrc:
    li flag_bitsize, 16
    lhu_m68k t0, 0(eaptr)
    lhu t1, 0(dptr)
    jalr rmw_op
    sh result, 0(dptr)
    j main_loop
rmw32_easrc:
    li flag_bitsize, 0
    lwl_m68k t0, 0(eaptr)
    lwr_m68k t0, 3(eaptr)
    lw t1, 0(dptr)
    jalr rmw_op
    sw result, 0(dptr)
    j main_loop
rmw8_eadst:
    li flag_bitsize, 24
    lbu t1, 0(dptr)
    lbu_m68k t0, 0(eaptr)
    jalr rmw_op
    sb_m68k result, 0(eaptr)
    j main_loop
rmw16_eadst:
    li flag_bitsize, 16
    lhu t1, 0(dptr)
    lhu_m68k t0, 0(eaptr)
    jalr rmw_op
    sh_m68k result, 0(eaptr)
    j main_loop
rmw32_eadst:
    li flag_bitsize, 0
    lwl t1, 0(dptr)   # NOTE: this is to handle immediate case (where dptr is PC-relative)
    lwr t1, 3(dptr)
    lwl_m68k t0, 0(eaptr)
    lwr_m68k t0, 0(eaptr)
    jalr rmw_op
    swl_m68k result, 0(eaptr)
    swr_m68k result, 3(eaptr)
    j main_loop
    .set noreorder

###############################################################
# Flags evaluation (lazy or immediate)
###############################################################
#if 0
    .section .sdata

flags: .byte 0,0,0,0,0
#define flag_x  (flags+0)     
#define flag_n  (flags+1)    // Flag is set if byte is negative
#define flag_z  (flags+2)    // Flag is set if byte is zero (reverse logic!)
#define flag_v  (flags+3)
#define flag_c  (flags+4)

    .section .data

    .section .text

    .balign 32
flag_bitlog:
    sb zero, flag_v
    sb zero, flag_c
    sb result, flag_n
    j main_loop
    sb result, flag_z
#endif

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
    jal_and_j decode_imm, op_and_body
OP(and):
    srl dptr, opcode, 7       # get data register pointer
    andi dptr, 0x7 << 2
    li opsize, 0
op_and_body:
    jal decode_ea             # get EA pointer
    nop
    
    srl t0, opcode, 4         # do RMW
    andi t0, 0x3 << 2
    rmw t0, op_and_rmwimpl
op_and_rmwimpl:
    and result, t0, t1
    sllv flag_nv, result, flag_bitsize
    jr ra
    dsllv flag_zc, result, flag_bitsize


          op4_0001: op4_0010: op4_0011: op4_0100: op4_0101: op4_0110: op4_0111:
op4_1000: op4_1001: op4_1010: op4_1011:           op4_1101: op4_1110: op4_1111:
    jr ra
    nop

op8_0000: op8_0001:           op8_0011: op8_0100: op8_0101: op8_0110: op8_0111:
op8_1000: op8_1001: op8_1010: op8_1011: op8_1100: op8_1101: op8_1110: op8_1111:
    jr ra
    nop
