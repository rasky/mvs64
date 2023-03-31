#include <regdef.h>

#define M68K_MEMORY_BASE  0

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
.macro map_m68k reg
    sll \reg, 8
    srl \reg, 8
    #if M68K_MEMORY_BASE != 0
    add \reg, M68K_MEMORY_BASE
    #endif
.endm

.macro breakpoint
    tne ra,ra
.endm

    .globl _m64k_aregs
    .globl _m64k_dregs
    .globl _m64k_pc
    .globl _m64k_cycles
    .globl _m64k_sr

    .section .sdata
_m64k_cycles: .long 0
_m64k_pc:     .long 0
_m64k_sr:     .long 0
flag_x:       .byte 0
    .section .data
_m64k_dregs:  .long 0,0,0,0,0,0,0,0
_m64k_aregs:  .long 0,0,0,0,0,0,0,0

optable0:
    .long   op4_0000, op4_0001, op4_0010, op4_0011, op4_0100, op4_0101, op4_0110, op4_0111
    .long   op4_1000, op4_1001, op4_1010, op4_1011, op_and,   op4_1101, op4_1110, op4_1111
optable1:
    .long   op8_0000, op8_0001, op_andi,  op8_0011, op8_0100, op8_0101, op8_0110, op8_0111
    .long   op8_1000, op8_1001, op8_1010, op8_1011, op8_1100, op8_1101, op8_1110, op8_1111

#define OP(x)       .balign 32; op_##x

    .text

#define opcode      s0
#define eaptr       s1
#define dptr        s2
#define opsize      s4
#define result      s5
#define flag_nv     s6    // bit 31: N; bit 63: N^V
#define flag_zc     s7    // bit 0..31: !Z; bit 32: C

#define m_pc          t5
#define m_cycles      t6
#define rmw_op        t7
#define flag_bitsize  t8

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

    lw m_cycles, _m64k_cycles
    lw m_pc,     _m64k_pc
    map_m68k m_pc

    # Extract flags from SR into flag_nv / flag_zc
    lw t0,       _m64k_sr    # Format: ---X NZVC

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

main_loop_exit:
    sw m_cycles, _m64k_cycles
    sw m_pc,     _m64k_pc

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
    
    lw t1, _m64k_sr
    and t1, ~0x1f
    or t1, t0
    sw t1, _m64k_sr

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
imm_32:
    move dptr, m_pc
    addi m_pc, 4
    li opsize, 4
    jr ra
    addi m_cycles, -8
imm_16:
    addi dptr, m_pc, -2
    addi m_pc, 2
    li opsize, 2
    jr ra
    addi m_cycles, -4
imm_8:
    addi dptr, m_pc, -2
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
    la eaptr, _m64k_dregs+4(t1)
    sub eaptr, opsize
    jr ra
ea_001: # An
    decode_opsize
    la eaptr, _m64k_aregs+4(t1)
    sub eaptr, opsize
    jr ra
ea_010: # (An)
    lw eaptr, _m64k_aregs(t1)
    map_m68k eaptr
    addi m_cycles, -4
    jr ra
ea_011: # (An)+
    # FIXME: align sp byte access
    lw eaptr, _m64k_aregs(t1)
    decode_opsize
    add t0, eaptr, opsize
    bne t1, 7*4, 1f   # if A7 (stack pointer), align to word
    add t0, 1
    and t0, ~1
1:
    sw t0, _m64k_aregs(t1)
    map_m68k eaptr
    addi m_cycles, -4
    jr ra
ea_100: # -(An)
    lw eaptr, _m64k_aregs(t1)
    decode_opsize
    sub eaptr, opsize
    bne t1, 7*4, 1f    # if A7 (stack pointer), align to word
    and eaptr, ~1
1:
    sw eaptr, _m64k_aregs(t1)
    map_m68k eaptr
    addi m_cycles, -6
    jr ra
ea_101: # (d16,An)
    lw eaptr, _m64k_aregs(t1)
    lh t0, 0(m_pc)
    addi m_pc, 2
    addu eaptr, t0
    map_m68k eaptr
    addi m_cycles, -8
    jr ra
ea_110: # (d8,An,Xn)
    lw eaptr, _m64k_aregs(t1)   # read An
ea_110_inner:
    lbu t1, 0(m_pc)  # read extension word
    lb t0, 1(m_pc)   # read 8-bit displacement
    addi m_pc, 2
    addu eaptr, t0
    srl t0, t1, 2       # calculate pointer to Xn
    andi t0, 0xF << 2
    andi t2, t1, 0x8
    lw t1, _m64k_dregs(t0)
    bnez t2, 1f
    lh t1, _m64k_dregs+2(t0)
1:
    addu eaptr, t1
    map_m68k eaptr
    addi m_cycles, -10
    jr ra
ea7_000: # (xxx).W
    lh eaptr, 0(m_pc)
    addi m_pc, 2
    map_m68k eaptr
    addi m_cycles, -8
    jr ra
ea7_001: # (xxx).L
    lwl eaptr, 0(m_pc)
    lwr eaptr, 3(m_pc)
    addi m_pc, 4
    map_m68k eaptr
    addi m_cycles, -12
    jr ra
ea7_010: # (d16,PC)
    move eaptr, m_pc
    lh t0, 0(m_pc)
    addi m_pc, 2
    add eaptr, t0
    map_m68k eaptr
    addi m_cycles, -8
    jr ra
ea7_011: # (d8,PC,Xn)
    move eaptr, m_pc
    addi m_cycles, -10
    j ea_110_inner    

ea7_100: # #<data>
    move eaptr, m_pc
    and t0, opcode, 0xC0   # isolate opsize in opcode
    addi m_pc, 2
    bnez t0, 1f
    addi eaptr, 1           # if opsize is byte, skip a byte
1:
    addi m_cycles, -4
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
    li flag_bitsize, 16
    lh t0, 0(eaptr)
    lh t1, 2(dptr)
    jalr rmw_op
    sh result, 0(dptr)
    j main_loop
rmw32_easrc:
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
    li flag_bitsize, 16
    lh t1, 2(dptr)
    lh t0, 0(eaptr)
    jalr rmw_op
    sh result, 0(eaptr)
    j main_loop
rmw32_eadst:
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
    la dptr, _m64k_dregs(dptr)
    li opsize, 0
op_and_body:
    jal decode_ea             # get EA pointer
    addi m_cycles, -6         # FIXME!!!
    
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
