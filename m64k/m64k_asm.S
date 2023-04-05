#include <regdef.h>
#include "m64k_internal.h"
#include "m64k_config.h"

    .set noreorder

# "lal" macro. Similar to "la" but only support loading a label address.
# This allows the gas reorder engine to work better.
.macro lal reg, label
    lui \reg, %hi(\label)
    addi \reg, %lo(\label)
.endm

# "jal_and_j" macro. Compact version of "jal" followed by "j", with a tail-call
# for optimal icache usage.
.macro jal_and_j func, label
    lal ra, \label
    j \func
.endm

# "zx64" macro. Zero-extend a 32-bit value to 64-bit.
.macro zx64 dst, src
    .ifnb \src
    dsll \dst, \src, 32
    .else
    dsll \dst, 32
    .endif
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

optable0:
    .long   op_tbl8,  op_moveb, op_movel, op_movew, op_tbl4,  op_addq,  op0_0110, op_moveq
    .long   op_or,    op0_1001, op0_1010, op_eor,   op_and,   op_add,   op0_1110, op0_1111
optable8:
    .long   op_ori,   op8_0001, op_andi,  op8_0011, op8_0100, op8_0101, op_addi,  op8_0111
    .long   op8_1000, op8_1001, op_eori,  op8_1011, op8_1100, op8_1101, op8_1110, op8_1111
optable4:
    .long   op_mvfsr, op4_0001, op4_0010, op4_0011, op_mvccr, op4_0101, op_mvtsr, op4_0111
    .long   op4_1000, op4_1001, op4_1010, op4_1011, op4_1100, op4_1101, op_mvusp, op4_1111

    .text

#define OP(x)       .balign 32; op_##x
#define m64k        a0
#define m_cycles    a1    // cycles to execute (NOTE: must be kept caller-saved register)

#define opcode      s0
#define eaptr       s1
#define dptr        s2
#define opsize      s4
#define result      s5
#define flag_nv     s6    // bit 31: N; bit 63: N^V
#define flag_zc     s7    // bit 0..31: !Z; bit 32: C
#define flag_x      s8    // bit 0..31: X

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

    jal reload_sr
    nop

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
op_tbl8:
    srl t0, opcode, 6
    andi t0, 0xF << 2
    lw t0, optable8(t0)
    jr t0
    nop
op_tbl4:
    srl t0, opcode, 6
    andi t0, 0xF << 2
    lw t0, optable4(t0)
    jr t0
    nop

main_loop_exit:
    lw t0, pc_diff
    add m_pc, t0
    sw m_pc, M64K_OFF_PC(m64k)
    #if !M64K_CONFIG_ADDRERR
    sw opcode, M64K_OFF_IR(m64k)
    #endif

    jal flush_sr
    nop

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

flush_sr:
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

    sne flag_x, zero, flag_x
    sll flag_x, 4
    or t0, flag_x
    
    lw t1, M64K_OFF_SR(m64k)
    and t1, ~0x1f
    or t1, t0
    sw t1, M64K_OFF_SR(m64k)

    srl t0, t1, 11                        # Isolate supervisor bit
    andi t0, 4
    lw_m64k t2, M64K_OFF_AREGS+7*4        # Copy A7 to USP/SSP
    sw_m64k t2, M64K_OFF_USP,t0

    jr ra   # return SR in t1
    nop


reload_sr: # t0 = SR
    # Extract flags from SR into flag_nv / flag_zc
    # SR Format: ---X NZVC
    srl flag_x, t0, 4          # X to bit 0
    andi flag_x, 1

    sll t1, t0, 28         # N to bit 31 (and 63)
    dsll flag_nv, t0, 62   # V to bit 63 (and 0 to 31)
    xor flag_nv, t1       

    andi t1, t0, 4         # Isolate Z
    xori t1, 4             # !Z
    dsll flag_zc, t0, 32   # C to bit 32
    or flag_zc, t1

    srl t1, t0, 11                    # Isolate supervisor bit
    andi t1, 4
    lw_m64k t2, M64K_OFF_USP,t1       # Copy USP/SSP to A7
    sw_m64k t2, M64K_OFF_AREGS+7*4

    jr ra
    nop


###############################################################
# Address error handling
###############################################################
#
# Address errors require quite some code to exactly match the
# state of the processor at the time they are generated. In fact,
# they happen mid-instructions, but the interpreter favors speed
# and code size and doesn't bother to generate them with a consistent
# state. An example is moves to a post-increment address: if the
# write happens to cause an address error, the address register is
# supposedely not incremented yet, but in our interpreter we raise
# the address error after the increment has happened.
# 
# So in general, we need to perform several fixups here, including
# instruction-specific ones. We don't care if this is slow as
# address errors are *extremely* rare in 68000 code, so it is a super
# cold path.

#if M64K_CONFIG_ADDRERR
    .data 
    # Table to help setting up address error. Each entry is a pair of
    # 8-bit values, first is the PC offset to rollback, second is the
    # "FC" code which is put into the stack frame.
addrerr_pcoff:  .byte  0,0,  0,0, -2,5, -2,5, -2,5, -2,5, -2,5,  0,0
addrerr7_pcoff: .byte -2,5, -2,5, -2,5, -2,5,  0,0,  0,0,  0,0,  0,0

    .text
    .set reorder
read_address_error:
    li t2, 1<<4
    j address_error
write_address_error:
    li t2, 0
address_error:
    li t0, M64K_PENDINGEXC_ADDRERR
    sw_m64k t0, M64K_OFF_PENDINGEXC+0
    sw_m64k eaptr32, M64K_OFF_PENDINGEXC+4

    # Check if it's a MOVE. MOVE has 2 EAs and we need to understand whether
    # the read part or the write part is responsible for the address error.
    lw_m64k t0, M64K_OFF_IR       # Check original opcode
    andi t0, 0xC000
    beqz t0, addrerr_fixup_move   # Fixup for MOVE post-decrement

addrerr_fixup_ea:
    # Compute PC offset and FC code from the opcode. Notice that we used the
    # "current" opcode that might have been modified by the handler to force
    # a specific EA mode; but this is exactly the one that we should be parsing here.
    # Fetch the PC/FC table entry using EA
    srl t0, opcode, 2
    andi t0, 0x7<<1
    la t1, addrerr_pcoff
    bne t0, 7<<1, compute_fc
adderr_off7:
    andi t0, opcode, 7
    addi t0, 8
    sll t0, 1
compute_fc:
    add t1, t0
    lb t0, 0(t1)                         # PC offset
    lbu t1, 1(t1)                        # FC code
    add m_pc, t0                         # Rollback PC
    lw_m64k opcode, M64K_OFF_IR          # Restore original opcode
    andi t0, opcode, 0xFFE0
    or t0, t1                            # Merge FC (4 bits) with opcode
    or t0, t2                            # Merge read/write flag
    sw_m64k t0, M64K_OFF_PENDINGEXC+8    # Store FC

    # Check if we need fixups
    andi t0, opcode, 0xF138              # Fixup for ADDX pre-decrement
    beq t0, 0xD108, addrerr_fixup_addq
    andi t0, opcode, 0xFFC0
    beq t0, 0x40C0, addrerr_fixup_mvfsr
    j main_loop_exit

addrerr_fixup_addq:
    #define lhs eaptr
    #define rhs eaptr32

    li t1, 0               # compute address adjustment
    bne opsize, 4, 1f      # 16-bit: 0
    li t1, 2               # 32-bit: 2

    # Decode LHS operand (Ry)
1:  andi t0, opcode, 0x7
    sll t0, 2
    la_m64k lhs, M64K_OFF_AREGS,t0
    lw t0, 0(lhs)
    andi t2, t0, 1   # check if LHS is odd
    beqz t2, 1f      # jump to RHS odd

    add t0, t1       # adjust LHS to the expected error address
    sw t0, 0(lhs)
    sw_m64k t0, M64K_OFF_PENDINGEXC+4  # store LHS as address error

    addi m_pc, -2                      # adjust expected PC
    lw_m64k t0, M64K_OFF_PENDINGEXC+8  # read FC
    xori t0, 0x5                       # adjust given that this is a read address exception
    sw_m64k t0, M64K_OFF_PENDINGEXC+8  # write FC

    j main_loop_exit

1:  # rhs is odd and causing exception
    # Decode RHS operand (Ry)
    srl t0, opcode, 7
    andi t0, 0x7 << 2
    la_m64k rhs, M64K_OFF_AREGS,t0
    lw t0, 0(rhs)                      # read RHS
    add t0, t1                         # adjust RHS to the expected error address
    sw t0, 0(rhs)                      # write RHS
    sw_m64k t0, M64K_OFF_PENDINGEXC+4  # store RHS as address error
    j main_loop_exit
    #undef lhs
    #undef rhs

addrerr_fixup_move:
    andi t2, 1<<4                      # Check if it's a read
    bnez t2, addrerr_fixup_ea          # If it's a read, do the standard EA fixup

    # This is a write exception. We must do fixup based *mainly* on the
    # destination EA, but there are some cases in which the source EA also
    # affects the stack frame.

    # Restore original opcode and prepare standard FC code.
    lw_m64k opcode, M64K_OFF_IR
    andi t0, opcode, 0xFFE0
    or t0, 0x5
    sw_m64k t0, M64K_OFF_PENDINGEXC+8    # Store FC

addrerr_fixup_move_dst:
    # Check destination EA
    andi t0, opcode, 0x7 << 6
    beq t0, 2<<6, addrerr_fixup_move_areg      # (An)
    beq t0, 3<<6, addrerr_fixup_move_postincr  # (An)+
    beq t0, 4<<6, addrerr_fixup_move_predecr   # -(An)
    beq t0, 5<<6, addrerr_fixup_move_disp16    # (d16,An)
    beq t0, 6<<6, addrerr_fixup_move_disp8     # (d8,An,Xn)

    andi t0, opcode, 0x3F << 6
    beq t0, 0x07<<6, addrerr_fixup_move_wordabs  # (xxx).W
    beq t0, 0x0F<<6, addrerr_fixup_move_longabs  # (xxx).L

1:  j main_loop_exit

    add m_pc, -2
    j main_loop_exit

addrerr_fixup_move_postincr: # (An)+
    srl t0, opcode, 7                  # Extract An register number
    andi t0, 0x7 << 2
    la_m64k t1, M64K_OFF_AREGS,t0      # Revert the post-increment
    lw t0, 0(t1)
    sub t0, opsize
    sw t0, 0(t1)
    addi m_pc, -2                      # Adjust PC
    j main_loop_exit

addrerr_fixup_move_predecr: # -(An)
    bne opsize, 4, main_loop_exit      # 16-bit: no fixup
    srl t0, opcode, 7                  # Extract An register number
    andi t0, 0x7 << 2
    la_m64k t1, M64K_OFF_AREGS,t0      # Revert half of the pre-decrement
    lw t0, 0(t1)
    add t0, 2
    sw t0, 0(t1)
    lw_m64k t0, M64K_OFF_PENDINGEXC+4  # Adjust also the exception address
    add t0, 2
    sw_m64k t0, M64K_OFF_PENDINGEXC+4
    j main_loop_exit

addrerr_fixup_move_areg:    # (An)
addrerr_fixup_move_disp8:   # (d8,An,Xn)
addrerr_fixup_move_disp16:  # (d16,An)
addrerr_fixup_move_wordabs: # (xxx).W
    addi m_pc, -2
    j main_loop_exit

addrerr_fixup_move_longabs: # (xxx).L
    addi m_pc, -4

    # Check source EA. It has an impact has the (xxx.L) decoding of dest 
    # is partly overlap to EA source. We check for the source EA mode
    # that don't access memory.
    andi t0, opcode, 0x38
    beq t0, 0b000000, addrerr_fixup_move_longabs2    # Dn
    beq t0, 0b001000, addrerr_fixup_move_longabs2    # An
    andi t0, opcode, 0x3F
    beq t0, 0b111100, addrerr_fixup_move_longabs2    # #<data>
    j main_loop_exit
addrerr_fixup_move_longabs2:
    addi m_pc, 2
    j main_loop_exit

addrerr_fixup_mvfsr:
    lw_m64k t0, M64K_OFF_PENDINGEXC+8  # read FC
    ori t0, 0x10
    sw_m64k t0, M64K_OFF_PENDINGEXC+8  # write FC
    j main_loop_exit

    .set noreorder
#endif


###############################################################
# Other errors
###############################################################

privilege_violation_error:
    move a0, opcode
    jal __m64k_assert_privilege_violation
    addiu a1, m_pc, -2


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
    andi t1, opcode, 0x7
decode_ea_customreg:    # Like decode_ea, but t1 is already set to the register to use in decoding
    srl t0, opcode, 1
    andi t0, 0x7 << 2
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
    lw_m64k eaptr32, M64K_OFF_AREGS,t1
    decode_opsize
    add t0, eaptr32, opsize
    bne t1, 7*4, 1f   # if A7 (stack pointer), align to word
    add t0, 1
    and t0, ~1
1:  sw_m64k t0, M64K_OFF_AREGS,t1
    map_m68k eaptr, eaptr32
    jr ra
ea_100: # -(An)
    lw_m64k eaptr32, M64K_OFF_AREGS,t1   # read address register
    decode_opsize                        # decode opsize (if wasn't already)
    sub eaptr32, opsize                  # decrement address register
    bne t1, 7*4, 1f                      # if A7 (stack pointer), align to word
    and eaptr32, ~1
1:  sw_m64k eaptr32, M64K_OFF_AREGS,t1   # write back address register
    map_m68k eaptr, eaptr32              # map address to N64 space
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
    decode_opsize
    add m_pc, opsize
    bne opsize, 1, 1f
    addi eaptr32, 1           # if opsize is byte, skip a byte
    addi m_pc, 1
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

.macro rmw opmode, opfunc
    .set reorder
    lw t0, rmw_table(\opmode)
    la rmw_op, \opfunc
    jr t0
    .set noreorder
.endm

    .data
rmw_table:
    .long   rmw8_easrc, rmw16_easrc, rmw32_easrc, invalid_opmode
    .long   rmw8_eadst, rmw16_eadst, rmw32_eadst, invalid_opmode

    .text
    .set noreorder
rmw:
    jal decode_ea             # decode EA into eaptr/eaptr32
rmw_no_ea:
    srl t3, opcode, 4         # extract size+src/dst flag
    andi t3, 0x7 << 2
    lw t3, rmw_table(t3)
    jr t3                     # jump to the rmw function
    nop
    .set reorder
rmw8_easrc:
    li flag_bitsize, 24
    lbu t0, 0(eaptr)
    lbu t1, 3(dptr)
    jalr rmw_op
    sb result, 3(dptr)
    j main_loop
rmw16_easrc:
    check_addr_error eaptr32, read_address_error
    li flag_bitsize, 16
    lhu t0, 0(eaptr)
    lhu t1, 2(dptr)
    jalr rmw_op
    sh result, 2(dptr)
    j main_loop
rmw32_easrc:
    check_addr_error eaptr32, read_address_error
    li flag_bitsize, 0
    lwl t0, 0(eaptr)
    lwr t0, 3(eaptr)
    lwu t1, 0(dptr)
    zx64 t0
    jalr rmw_op
    sw result, 0(dptr)
    j main_loop
rmw8_eadst:
    li flag_bitsize, 24
    lbu t1, 3(dptr)
    lbu t0, 0(eaptr)
    jalr rmw_op
    sb result, 0(eaptr)
    j main_loop
rmw16_eadst:
    check_addr_error eaptr32, read_address_error
    li flag_bitsize, 16
    lhu t1, 2(dptr)
    lhu t0, 0(eaptr)
    jalr rmw_op
    sh result, 0(eaptr)
    j main_loop
rmw32_eadst:
    check_addr_error eaptr32, read_address_error
    li flag_bitsize, 0
    lwl t1, 0(dptr)   # NOTE: this is to handle immediate case (where dptr is PC-relative)
    lwr t1, 3(dptr)
    lwl t0, 0(eaptr)
    lwr t0, 3(eaptr)
    zx64 t1
    zx64 t0
    jalr rmw_op
    swl result, 0(eaptr)
    swr result, 3(eaptr)
    j main_loop

invalid_opmode:
    move a0, opcode
    addiu a1, m_pc, -2
    jal __m64k_assert_invalid_opmode
    .set noreorder

###############################################################
# Opcodes
###############################################################

OP(addq):
    la rmw_op, op_addq_rmwimpl
    srl t1, opcode, 9   # extract immediate value
    andi t1, 0x7
    bnez t1, 1f         # if immediate is not 0, skip
    ori opcode, 1<<8    # force rmw with eadst
    li t1, 8            # if immediate is 0, use 8
1:  sw t1, M64K_OFF_PENDINGEXC+4(m64k)      # store imediate into dummy space
    la dptr, M64K_OFF_PENDINGEXC+4(m64k)    # set dptr to dummy space
    j rmw
    li opsize, 0        # opsize not calculated yet
op_addq_rmwimpl:
    srl t2, opcode, 3            # check if EA is An
    andi t2, 7
    bne t2, 1, op_add_rmwimpl    # if not, do normal add
    daddu result, t0, t1         # do add but leave flags unchanged
    jr ra
    nop

OP(addi):
    .set reorder
    lal rmw_op, op_add_rmwimpl
    jal decode_imm
    ori opcode, 1<<8    # force rmw with eadst
    j rmw
    nop
OP(add):
    # check if the opcode is addx instead
    lal rmw_op, op_add_rmwimpl
    srl dptr, opcode, 7       # get data register pointer
    andi dptr, 0x7 << 2       # isolate data register pointer
    la_m64k dptr, M64K_OFF_DREGS,dptr
    andi t0, opcode, 0xC0
    beq t0, 0xC0, op_adda     # if adda jump to implementation
    andi t0, opcode, 0x130
    xori t0, 0x100
    li opsize, 0              # opsize not calculated yet
    beqz t0, op_addx          # if addx, jump to implementation
    j rmw
op_add_rmwimpl:
    daddu result, t0, t1
    dsll flag_zc, result, flag_bitsize
    dsrl flag_x, flag_zc, 32
    sllv t0, t0, flag_bitsize
    sllv t1, t1, flag_bitsize
    daddu flag_nv, t0, t1
    jr ra
    .set noreorder

OP(addx):
    .set reorder
    lal rmw_op, op_addx_rmwimpl
    andi t0, opcode, 0x8         # isolate R/M bit
    andi t1, opcode, 0x7         # extract Ry*4
    sll t1, 2
    beqz t0, op_addx_data        # checl if R/M is data or address
op_addx_addr:
    jal ea_100                   # decode Rx as pre-decrement address register
    #if M64K_CONFIG_ADDRERR
    beq opsize, 1, 1f
    check_addr_error eaptr32, read_address_error
    #endif
1:  xori opcode, 0x28            # force EA mode to predecrement
    j op_addx_finish
op_addx_data:
    jal ea_000                   # decode Rx as data register
op_addx_finish:
    add dptr, eaptr, opsize      # set dptr to decoded address
    srl t1, opcode, 9            # extract Ry
    andi t1, 0x7
    jal decode_ea_customreg      # decode EA with Ry
    addi dptr, -4                # finish adjusting dptr (so that rmw does the right thing)
    j rmw_no_ea
op_addx_rmwimpl:
    sne flag_x, 0                        # set flag_x to 0/1
    zx64 t2, flag_zc                     # put current Z flag (without C) into t2
    daddu result, t0, t1                 # compute full size result
    daddu result, flag_x                 # add flag_x
    dsll flag_zc, result, flag_bitsize   # compute new flag_zc
    or flag_zc, t2                       # combine with old Z flag
    sllv t0, t0, flag_bitsize            # put operands into MSB
    sllv t1, t1, flag_bitsize
    sllv flag_x, flag_x, flag_bitsize
    daddu flag_nv, t0, t1                # compute flag_nv
    daddu flag_nv, flag_x
    dsrl flag_x, flag_zc, 32             # copy new carry bit into flag_x
    jr ra
    .set noreorder

OP(adda):
    .set reorder
    addi dptr, 32                   # Point to AREG instead of DREG
    andi t0, opcode, 0x100          # Check W/L bit
    bnez t0, 1f                     # Jump to correct body
    la rmw_op, op_adda16_rmwimpl    # ADDA 16-bit
    li opsize, 2                      # Force word size in EA decoding
    jal decode_ea                     # Decode EA
    addi eaptr, -2                    # Convert eaptr to long (for rmw32)
    j rmw32_easrc                     # Do RMW
1:  la rmw_op, op_adda32_rmwimpl    # ADDA 32-bit
    li opsize, 4                      # Force long size in EA decoding
    jal decode_ea                     # Decode EA
    j rmw32_easrc                     # Do RMW
op_adda16_rmwimpl:
    sll t0, 16
    sra t0, 16
op_adda32_rmwimpl:
    daddu result, t0, t1
    jr ra
    .set noreorder


OP(group_logical):
    .set reorder
    srl dptr, opcode, 7       # get data register pointer
    andi dptr, 0x7 << 2
    la_m64k dptr, M64K_OFF_DREGS,dptr
    li opsize, 0
    j rmw
    .set noreorder

OP(andi):
    .set reorder
    ori opcode, 1<<8    # force rmw with eadst
    lal rmw_op, op_and_rmwimpl
    jal_and_j decode_imm, rmw
OP(and):
    lal rmw_op, op_and_rmwimpl
    j op_group_logical
op_and_rmwimpl:
    and result, t0, t1
    sllv flag_nv, result, flag_bitsize
    zx64 flag_zc, flag_nv
    jr ra
    .set noreorder

OP(ori):
    .set reorder
    ori opcode, 1<<8    # force rmw with eadst
    lal rmw_op, op_or_rmwimpl
    jal_and_j decode_imm, rmw
OP(or):
    lal rmw_op, op_or_rmwimpl
    j op_group_logical
op_or_rmwimpl:
    or result, t0, t1
    sllv flag_nv, result, flag_bitsize
    zx64 flag_zc, flag_nv
    jr ra
    .set noreorder

OP(eori):
    .set reorder
    ori opcode, 1<<8    # force rmw with eadst
    lal rmw_op, op_eor_rmwimpl
    jal_and_j decode_imm, rmw
OP(eor):
    lal rmw_op, op_eor_rmwimpl
    j op_group_logical
op_eor_rmwimpl:
    xor result, t0, t1
    sllv flag_nv, result, flag_bitsize
    zx64 flag_zc, flag_nv
    jr ra
    .set noreorder




OP(moveb):
    .set reorder
    li opsize, 1
    jal decode_ea
    lbu flag_zc, 0(eaptr)
    srl opcode, 3
    srl t1, opcode, 6
    andi t1, 7
    jal decode_ea_customreg
    sb flag_zc, 0(eaptr)
    sll flag_nv, flag_zc, 24
    j main_loop
    .set noreorder

OP(movew):
    .set reorder
    li opsize, 2
    jal decode_ea
    check_addr_error eaptr32, read_address_error
    and t0, opcode, 0x1C0
    beq t0, 0x040, op_movea16
    lhu flag_zc, 0(eaptr)           # read word; this is already good as flag_zc (as C is always cleared)
    sll flag_nv, flag_zc, 16        # flag_nv is the sign extension of the 16 bit value
    srl opcode, 3                   # move the EA dst mode into the EA src mode field
    srl t1, opcode, 6               # decode the EA dst register
    andi t1, 7
    jal decode_ea_customreg         # decode dst EA
    check_addr_error eaptr32, write_address_error
    sh flag_zc, 0(eaptr)             # store the word into the destination
    j main_loop
    .set noreorder

OP(movel):
    .set reorder
    li opsize, 4
    jal decode_ea
    check_addr_error eaptr32, read_address_error
    and t0, opcode, 0x1C0
    beq t0, 0x040, op_movea32
    lwl flag_nv, 0(eaptr)           # read unaligned 32-bit word (sign-extended)
    lwr flag_nv, 3(eaptr)           # (it is already good as flag_nv, so store it there).
    zx64 flag_zc, flag_nv           # flag_zc is the zero-extended copy (as C is always cleared)
    srl opcode, 3
    srl t1, opcode, 6
    andi t1, 7
    jal decode_ea_customreg
    check_addr_error eaptr32, write_address_error
    swl flag_nv, 0(eaptr)
    swr flag_nv, 3(eaptr)
    j main_loop
    .set noreorder

OP(moveq):
    .set reorder
    sll flag_nv, opcode, 24
    sra flag_nv, 24
    andi flag_zc, opcode, 0xFF
    srl t1, opcode, 7
    andi t1, 0x7 << 2
    sw_m64k flag_nv, M64K_OFF_DREGS,t1
    j main_loop
    .set noreorder

OP(movea16):
    .set reorder
    lh t1, 0(eaptr)
    srl t0, opcode, 7
    andi t0, 0x7<<2
    sw_m64k t1, M64K_OFF_AREGS,t0
    j main_loop
    .set noreorder

OP(movea32):
    .set reorder
    lwl t1, 0(eaptr)
    lwr t1, 3(eaptr)
    srl t0, opcode, 7
    andi t0, 0x7<<2
    sw_m64k t1, M64K_OFF_AREGS,t0
    j main_loop
    .set noreorder

OP(mvfsr):
    .set reorder
    li opsize, 2
    jal decode_ea
    check_addr_error eaptr32, write_address_error
    jal flush_sr
    sh t1, 0(eaptr)
    j main_loop
    .set noreorder

OP(mvccr):
    .set reorder
    li opsize, 2
    jal decode_ea
    check_addr_error eaptr32, read_address_error
    jal flush_sr
    lw_m64k t1, M64K_OFF_SR
    lhu t0, 0(eaptr)
    and t1, 0xA700
    and t0, 0x1F
    or t0, t1
    sw_m64k t0, M64K_OFF_SR
    jal reload_sr
    j main_loop
    .set noreorder

OP(mvtsr):
    .set reorder
    li opsize, 2
    jal decode_ea
    check_addr_error eaptr32, read_address_error
    jal flush_sr
    lhu t0, 0(eaptr)
    and t0, 0xA71F
    sw_m64k t0, M64K_OFF_SR
    jal reload_sr
    j main_loop
    .set noreorder

OP(mvusp):
    .set reorder
    lw_m64k t0, M64K_OFF_SR    # Read current SR
    andi t0, 0x2000            # Check supervisor bit
    beqz t0, privilege_violation_error
    andi t0, opcode, 8         # DR bit
    sll t1, opcode, 2          # Isolate register offset
    andi t1, 0x7<<2
    bnez t0, mvusp_read        # See if it's read or write
    lw_m64k t2, M64K_OFF_AREGS,t1
    sw_m64k t2, M64K_OFF_USP
    j main_loop
mvusp_read:
    lw_m64k t2, M64K_OFF_USP
    sw_m64k t2, M64K_OFF_AREGS,t1
    j main_loop

    .set noreorder



                                        op0_0100: op0_0101: op0_0110: op0_0111:
op0_1000: op0_1001: op0_1010: op0_1011:           op0_1101: op0_1110: op0_1111:

op8_0000: op8_0001:           op8_0011: op8_0100: op8_0101: op8_0110: op8_0111:
op8_1000: op8_1001: op8_1010: op8_1011: op8_1100: op8_1101: op8_1110: op8_1111:

op4_0000: op4_0001: op4_0010: op4_0011: op4_0100: op4_0101: op4_0110: op4_0111:
op4_1000: op4_1001: op4_1010: op4_1011: op4_1100: op4_1101: op4_1110: op4_1111:

    move a0, opcode
    jal __m64k_assert_invalid_opcode
    addiu a1, m_pc, -2
