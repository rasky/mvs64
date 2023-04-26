#include <regdef.h>
#include "m64k_internal.h"
#include "m64k_config.h"

#define SR_SUPERV  0x2000
#define SR_MASK    0xA71F
#define CCR_MASK   0x1F

    .set noreorder

.macro check_addr_error reg, label
    #if M64K_CONFIG_ADDRERR
    .set noat
    andi $1, \reg, 1
    bnez $1, \label
    .set at
    #endif
.endm

.macro check_supervisor
    #if M64K_CONFIG_PRIVERR
    jal check_supervisor
    #endif
.endm

.macro breakpoint
    tne ra,ra
.endm

     .section .sdata
pc_diff:      .long 0

optable:
    .long   op_tbl0,  op_moveb, op_movel, op_movew, op_tbl4,  op_addq,  op_bcc,   op_moveq
    .long   op_or,    op_sub,   op_unk,   op_cmp,   op_and,   op_add,   op_shifts,op_unk
optable0:
    .long   op_ori,   op_bitsr, op_andi,  op_bitsr, op_subi,  op_bitsr, op_addi,  op_bitsr
    .long   op_bitsi, op_bitsr, op_eori,  op_bitsr, op_cmpi,  op_bitsr, op_unk,   op_bitsr
optable4:
    .long   op_negx,  op_lea,   op_clr,   op_lea,   op_neg,   op_lea,   op_not,   op_lea
    .long   op_pea,   op_lea,   op_tst,   op_lea, op_movem_mr,op_lea,   op_tbl4e, op_lea
optable4e:
    .long   op_unk,    op_unk,    op_unk,    op_unk,    op_trap,   op_link,   op_mvusp,  op_tbl4e7    
    .long   op_jsr,    op_jsr,    op_jsr,    op_jsr,    op_jmp,    op_jmp,    op_jmp,    op_jmp
optable4e7:
    .long   op_reset,  op_nop,    op_stop,   op_rte,    op_rtd,    op_rts,    op_trapv,  op_rtr
    .long   op_unk,    op_unk,    op_movec,  op_movec,  op_unk,    op_unk,    op_unk,    op_unk

opshifts:
    .long   op_asli,  op_lsli,  op_roxli,  op_roli
    .long   op_asl,   op_lsl,   op_roxl,   op_rol
opshiftsmem:
    .long   op_aslm,  op_asrm,  op_lslm,  op_lsrm,  op_roxlm, op_roxrm, op_rolm,  op_rorm
    .long   op_bftst, op_bfextu,op_bfchg, op_bfexts,op_bfclr, op_bfffo, op_bfset, op_bfins

    .text

#define ALIGN(lbl)  .balign 32; lbl

#define OP(x)       .balign 32; op_##x
#define ctx         a0
#define m_cycles    a1    // cycles to execute (NOTE: must be kept caller-saved register)
#define mmap_mask   a2
#define _________   a3    // warning: used by move.m, check there before using it

#define lax_base    v0
#define zx64_mask   v1

#define opcode      s0
#define eaptr       s1
#define dptr        s2
#define opsize      s4
#define m_cyc_table s5
#define flag_nv     s6    // bit 31: N; bit 63: N^V
#define flag_zc     s7    // bit 0..31: !Z; bit 32: C
#define flag_x      s8    // bit 0: X

#define eaptr32       t4
#define m_pc          t5
#define result        t6
#define rmw_op        t7    // RMW pointer to implementation function
#define rmw_bitsize   t8    // RMW bitsize value useful in flag calcuation (24, 16, 0)
#define rmw_srmask    t8    // for SR RMWs, it contains the mask for SR bits to be written

.macro la_ctx  reg, offset,base
    .ifnb \base
        .set noat
        add \reg, ctx, \base
        addi \reg, \offset
        .set at
    .else
        add \reg, ctx, \offset
    .endif
.endm

.macro lw_ctx  reg, offset,base
    .ifnb \base
        .set noat
        add $1, ctx, \base
        lw \reg, \offset($1)
        .set at
    .else
        lw \reg, \offset(ctx)
    .endif
.endm

.macro sw_ctx  reg, offset,base
    .ifnb \base
        .set noat
        add $1, ctx, \base
        sw \reg, \offset($1)
        .set at
    .else
        sw \reg, \offset(ctx)
    .endif
.endm

#define LAX_BASE_ADDR main_loop

.macro lax reg, label
    addiu \reg, lax_base, \label - LAX_BASE_ADDR
.endm

# "zx64" macro. Zero-extend a 32-bit value to 64-bit.
.macro zx64 dst, src
    .ifnb \src
    and \dst, \src, zx64_mask
    .else
    and \dst, zx64_mask
    .endif
.endm

# "not32sx" macro. Like "not", but only complements the lower
# 32-bits of the register (and then sign extends).
.macro not32sx dst, src
    .ifnb \src
    subu \dst, zx64_mask, \src
    .else
    subu \dst, zx64_mask, \dst
    .endif
.endm

# "not32" macro. Like "not", but only complements the lower
# 32-bits of the register (and keep the upper half unchanged).
.macro not32 dst, src
    .ifnb \src
    xor \dst, zx64_mask, \src
    .else
    xor \dst, zx64_mask, \dst
    .endif
.endm


# "jal_and_j" macro. Compact version of "jal" followed by "j", with a tail-call
# for optimal icache usage.
.macro jal_and_j func, label
    lax ra, \label
    j \func
.endm

# "jalr_and_j" macro. Similar to jal_and_j, but uses jalr instead.
.macro jalr_and_j reg, label
    lax ra, \label
    jr \reg
.endm

# "map_m68k" macro. Maps a 32-bit M68K address to a 32-bit MIPS address.
# This macro implements different codepaths depending on M64K_CONFIG_MEMORY_BASE.
# A fixed register mmap_mask is used to implement the mapping more compactly,
# and it must be initialized before using this macro.
.macro map_m68k dst, src
    #if M64K_CONFIG_MEMORY_BASE == 0x00000000
        .ifnb \src
        and \dst, \src, mmap_mask
        .else
        and \dst, mmap_mask
        .endif
    #elif M64K_CONFIG_MEMORY_BASE == 0xFF000000
        .ifnb \src
        or \dst, \src, mmap_mask
        .else
        or \dst, mmap_mask
        .endif
    #else
        .ifnb \src
        sll \dst, \src, 8
        .else
        sll \dst, 8
        .endif
        srl \reg, 8
        add \reg, mmap_mask
    #endif
.endm

# Macros to store to the M68K memory space (after mapping).
# These macros are meant to explicitly mark m68k memory space writes,
# and to enforce the convention that all writes to the M68K memory space
# must have the value to be stored in the "result" register. 
# This simplifies the implementation of MMIO handlers.
.macro sw_m68k reg, address
    .if (\reg != result)
        .error "sw_m68k: register used to store into m68k memory must be 'result'"
    .endif
    swl \reg, 0(\address)
    swr \reg, 3(\address)
.endm

.macro sh_m68k reg, address
    .if (\reg != result)
        .error "sh_m68k: register used to store into m68k memory must be 'result'"
    .endif
    sh result, 0(\address)
.endm

.macro sb_m68k reg, address
    .if (\reg != result)
        .error "sb_m68k: register used to store into m68k memory must be 'result'"
    .endif
    sb \reg, 0(\address)
.endm

# Macros to load from the M68K memory space (after mapping).
.macro lw_m68k reg, address
    .if (\reg != t0)
        .error "lw_m68k: register used to store into m68k memory must be 't0'"
    .endif
    lwl \reg, 0(\address)
    lwr \reg, 3(\address)
.endm

.macro lhu_m68k reg, address
    .if (\reg != t0)
        .error "lhu_m68k: register used to store into m68k memory must be 't0'"
    .endif
    lhu \reg, 0(\address)
.endm

.macro lh_m68k reg, address
    .if (\reg != t0)
        .error "lh_m68k: register used to store into m68k memory must be 't0'"
    .endif
    lh \reg, 0(\address)
.endm

.macro lbu_m68k reg, address
    .if (\reg != t0)
        .error "lhu_m68k: register used to store into m68k memory must be 't0'"
    .endif
    lbu \reg, 0(\address)
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

    #if M64K_CONFIG_MEMORY_BASE == 0x00000000 
    li mmap_mask, 0x00FFFFFF
    #else
    li mmap_mask, M64K_CONFIG_MEMORY_BASE
    #endif
    la lax_base, LAX_BASE_ADDR
    dli zx64_mask, 0x00000000FFFFFFFF
    la m_cyc_table, __m64k_cycle_table
    lw t1, M64K_OFF_PC(ctx)
    lw t0, M64K_OFF_SR(ctx)

    # Convert PC to the N64 address space.
    map_m68k m_pc, t1
    sub t1, m_pc
    sw t1, pc_diff                    # Save difference between m_pc and PC

    # Initialize dptr to a valid pointer, for ops that don't set it
    # and then ignore the read value.
    move dptr, ctx

    jal reload_sr
    nop

    sw m_cycles, M64K_OFF_TS_START(ctx)

ALIGN(main_loop): OP(nop):
    blez m_cycles, main_loop_exit
    sw m_cycles, M64K_OFF_TS_CUR(ctx)
    lbu t0, M64K_OFF_CHECK_INTERRUPTS(ctx)
    lhu opcode, 0(m_pc)
    bnez t0, check_interrupt
    #if M64K_CONFIG_ADDRERR
    sw opcode, M64K_OFF_IR(ctx)
    #endif
    addu t1, opcode, m_cyc_table
    lbu t1, 0(t1)
    srl t0, opcode, 12
    sll t0, 2
    lw t0, optable(t0)
    addi m_pc, 2
    jr t0
    sub m_cycles, t1
op_tbl0:
    srl t0, opcode, 6
    andi t0, 0xF << 2
    lw t0, optable0(t0)
    jr t0
    li opsize, 0
OP(tbl4):
    srl t0, opcode, 6
    andi t0, 0xF << 2
    lw t0, optable4(t0)
    jr t0
    li opsize, 0
op_tbl4e:
    srl t0, opcode, 2
    andi t0, 0xF << 2
    lw t0, optable4e(t0)
    jr t0
    nop
op_tbl4e7:
    sll t0, opcode, 2
    andi t0, 0xF << 2
    lw t0, optable4e7(t0)
    jr t0
    nop
OP(shifts):
    andi t0, opcode, 0x3<<6
    beq t0, 0x3<<6, 1f
    srl t0, opcode, 1
    andi t0, 0x7<<2
    lw t0, opshifts(t0)
    jr t0
    li opsize, 0
1:  srl t0, opcode, 6
    andi t0, 0xF<<2
    lw t0, opshiftsmem(t0)
    jr t0
    li opsize, 2

check_interrupt:
    beq t0, 2, main_loop_exit
    sb zero, M64K_OFF_CHECK_INTERRUPTS(ctx)
    # Check if a NMI is pending
    lbu t0, M64K_OFF_NMI_PENDING(ctx)
    bnez t0, raise_irq
    li t1, 7
    # Check if IPL > SR.irq_mask
    lbu t0, M64K_OFF_SR+2(ctx)
    lbu t1, M64K_OFF_IPL(ctx)
    andi t0, 7
    ble t1, t0, main_loop
raise_irq:
    # Raise an IRQ (level in t1)
    li t0, M64K_PENDINGEXC_IRQ
    sw_ctx t0, M64K_OFF_PENDINGEXC+0
    j main_loop_exit
    sw_ctx t1, M64K_OFF_PENDINGEXC+4


main_loop_exit:
    lw t0, pc_diff
    addu m_pc, t0
    sw m_pc, M64K_OFF_PC(ctx)
    #if !M64K_CONFIG_ADDRERR
    sw opcode, M64K_OFF_IR(ctx)
    #endif

    sw zero, M64K_OFF_TS_START(ctx)

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

    # Store flags into SR (preserving other SR bits)
    # Also store the current stack pointer (A7) into USP/SSP
    # Destroy: t0, t1, t2
    # Return: t1 = SR
flush_sr:
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

    andi flag_x, 1
    sll t1, flag_x, 4
    or t0, t1
    
    lw t1, M64K_OFF_SR(ctx)
    and t1, ~CCR_MASK
    or t1, t0
    sw t1, M64K_OFF_SR(ctx)

flush_sr_chstack:
    srl t0, t1, 11                        # Isolate supervisor bit
    andi t0, 4
    lw_ctx t2, M64K_OFF_AREGS+7*4        # Copy A7 to USP/SSP
    sw_ctx t2, M64K_OFF_USP,t0

    jr ra   # return SR in t1
    nop

    # Store the current stack pointer (A7) into USP/SSP
    # Return: t1 = SR
flush_stack: 
    j flush_sr_chstack
    lw t1, M64K_OFF_SR(ctx)


    # Reload flag registers from SR bits.
    # Also load the current stack pointer (A7) from USP/SSP
    # Input: t0: SR
    # Destroy: t0, t1, t2
reload_sr: 
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

reload_sr_chstack:
    srl t1, t0, 11                    # Isolate supervisor bit
    andi t1, 4
    lw_ctx t2, M64K_OFF_USP,t1       # Copy USP/SSP to A7
    sw_ctx t2, M64K_OFF_AREGS+7*4

    li t1, 1
    sb t1, M64K_OFF_CHECK_INTERRUPTS(ctx)

    jr ra
    nop

reload_stack: 
    j reload_sr_chstack
    lw t0, M64K_OFF_SR(ctx)


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
    # Table to help setting up address error. Each entry is the PC offset to rollback.
addrerr_pcoff:  .byte  0,  0, -2, -2, -2, -2, -2,  0
addrerr7_pcoff: .byte -2, -2, -2, -2,  0,  0,  0,  0

#define FC_DATA     1         // Error while accessing data
#define FC_PROGRAM  2         // Error while fetching code
#define FC_SUPERV   4         // Error while in supervisor mode
#define FC_NONOP    8         // Error happened outside of opcode execution
#define FC_READ     16        // Error while reading from the bus

    .text
    .set reorder
pc_address_error:
    li t2, FC_READ | FC_PROGRAM | FC_NONOP
    j address_error
read_address_error:
    li t2, FC_READ | FC_DATA
    j address_error
write_address_error:
    li t2, FC_DATA
address_error:
    li t0, M64K_PENDINGEXC_ADDRERR
    sw_ctx t0, M64K_OFF_PENDINGEXC+0
    sw_ctx eaptr32, M64K_OFF_PENDINGEXC+4

    # Most opcodes that generate an address error have a standard EA
    # encoding in bits 0..6, and we handle those just below. Some of them,
    # though, don't have a standard EA, so we need to handle with separate
    # code.
    lw_ctx t1, M64K_OFF_IR       # Check original opcode
    andi t0, t1, 0xF000
    beq t0, 0x1000, addrerr_fixup_move # Fixup for MOVE (src/dst EA)
    beq t0, 0x2000, addrerr_fixup_move # Fixup for MOVE (src/dst EA)
    beq t0, 0x3000, addrerr_fixup_move # Fixup for MOVE (src/dst EA)
    beq t0, 0x6000, addrerr_fixup_bcc  # Fixup for BCC (which doesn't have EA)
    andi t0, t1, 0xF0F8
    beq t0, 0x50C8, addrerr_fixup_dbcc # Fixup for DBCC (which doesn't have EA)
    beq t1, 0x4e73, addrerr_fixup_rte  # Fixup for RTE (which doesn't have EA)
    beq t1, 0x4e75, addrerr_fixup_rts  # Fixup for RTS (which doesn't have EA)
    beq t1, 0x4e77, addrerr_fixup_rtr  # Fixup for RTR (which doesn't have EA)
    andi t0, t1, 0xF138
    beq t0, 0xB108, address_fixup_cmpm # Fixup for CMPM (two registers post-increment)

addrerr_fixup_standard_ea:
    # Compute PC offset from the opcode. Notice that we used the
    # "current" opcode that might have been modified by the handler to force
    # a specific EA mode; but this is exactly the one that we should be parsing here.
    # Fetch the PC table entry using EA
    srl t0, opcode, 3
    andi t0, 0x7
    bne t0, 7, addrerr_adjust_pc
adderr_off7:
    andi t0, opcode, 7
    addi t0, 8
addrerr_adjust_pc:
    lb t0, addrerr_pcoff(t0)             # PC offset
    add m_pc, t0                         # Rollback PC
addrerr_finish:
    lw_ctx opcode, M64K_OFF_IR          # Restore original opcode
    andi t0, opcode, 0xFFE0
    or t0, t2                            # Merge FC code (with read/write flag)
    lw_ctx t1, M64K_OFF_SR              # Check if we were running in supervisor mode
    andi t1, SR_SUPERV
    beqz t1, 1f
    ori t0, FC_SUPERV                    # Mark supervisor FC bit
1:  sw_ctx t0, M64K_OFF_PENDINGEXC+8    # Store FC

    # Check if we need fixups
    andi t0, opcode, 0xF138              
    beq t0, 0xD108, addrerr_fixup_addq  # Fixup for ADDX pre-decrement
    beq t0, 0x9108, addrerr_fixup_addq  # Fixup for SUBX pre-decrement
    andi t0, opcode, 0xFFC0
    beq t0, 0x4EC0, addrerr_fixup_jmp   # Fixup for JMP
    beq t0, 0x4E80, addrerr_fixup_jsr   # Fixup for JSR
    andi t0, opcode, 0xFF80
    beq t0, 0x4880, addrerr_fixup_movem_rm   # Fixup for MOVEM reg->mem
    beq t0, 0x4C80, addrerr_fixup_movem_mr   # Fixup for MOVEM mem->reg
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
    la_ctx lhs, M64K_OFF_AREGS,t0
    lw t0, 0(lhs)
    andi t2, t0, 1   # check if LHS is odd
    beqz t2, 1f      # jump to RHS odd

    add t0, t1       # adjust LHS to the expected error address
    sw t0, 0(lhs)
    sw_ctx t0, M64K_OFF_PENDINGEXC+4  # store LHS as address error
    addi m_pc, -2                      # adjust expected PC
    j main_loop_exit

1:  # rhs is odd and causing exception
    # Decode RHS operand (Ry)
    srl t0, opcode, 7
    andi t0, 0x7 << 2
    la_ctx rhs, M64K_OFF_AREGS,t0
    lw t0, 0(rhs)                      # read RHS
    add t0, t1                         # adjust RHS to the expected error address
    sw t0, 0(rhs)                      # write RHS
    sw_ctx t0, M64K_OFF_PENDINGEXC+4  # store RHS as address error
    j main_loop_exit
    #undef lhs
    #undef rhs

addrerr_fixup_move:
    andi t0, t2, 1<<4                  # Check if it's a read
    bnez t0, addrerr_fixup_standard_ea # If it's a read, do the standard EA fixup

    # This is a write exception. We must do fixup based *mainly* on the
    # destination EA, but there are some cases in which the source EA also
    # affects the stack frame.

    # Restore original opcode and prepare standard FC code.
    lw_ctx opcode, M64K_OFF_IR
    andi t0, opcode, 0xFFE0
    or t0, 0x5
    sw_ctx t0, M64K_OFF_PENDINGEXC+8    # Store FC

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
    la_ctx t1, M64K_OFF_AREGS,t0      # Revert the post-increment
    lw t0, 0(t1)
    sub t0, opsize
    sw t0, 0(t1)
    addi m_pc, -2                      # Adjust PC
    j main_loop_exit

addrerr_fixup_move_predecr: # -(An)
    bne opsize, 4, main_loop_exit      # 16-bit: no fixup
    srl t0, opcode, 7                  # Extract An register number
    andi t0, 0x7 << 2
    la_ctx t1, M64K_OFF_AREGS,t0      # Revert half of the pre-decrement
    lw t0, 0(t1)
    add t0, 2
    sw t0, 0(t1)
    lw_ctx t0, M64K_OFF_PENDINGEXC+4  # Adjust also the exception address
    add t0, 2
    sw_ctx t0, M64K_OFF_PENDINGEXC+4
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

addrerr_fixup_dbcc:
addrerr_fixup_bcc:
addrerr_fixup_rte:
addrerr_fixup_rts:
addrerr_fixup_rtr:
    addi m_pc, -4                            # adjust current PC
    j addrerr_finish

addrerr_fixup_jsr:
    lw_ctx t0, M64K_OFF_AREGS+7*4     # revert SP-=4 performed by JSR
    addiu t0, 4
    sw_ctx t0, M64K_OFF_AREGS+7*4
addrerr_fixup_jmp:
    addiu m_pc, -2
    j main_loop_exit

addrerr_fixup_movem_rm:
    # We use a shared implementation between reg->mem and mem->reg, so
    # we always generate a read_address_error. Change to write when the
    # opcode is reg->mem.
    lw_ctx t0, M64K_OFF_PENDINGEXC+8
    xori t0, FC_READ
    sw_ctx t0, M64K_OFF_PENDINGEXC+8

    andi t0, opcode, 1<<6                # check if word transfer
    beqz t0, main_loop_exit              # if word, fixup is finished
    andi t0, opcode, 7<<3                # check if EA mode is pre-decremented
    bne t0, 4<<3, main_loop_exit         # otherwise, fixup is finished
    andi t0, opcode, 7
    sll t0, 2
    lw_ctx t1, M64K_OFF_PENDINGEXC+4
    addiu t1, 2
    sw_ctx t1, M64K_OFF_PENDINGEXC+4
    j main_loop_exit

addrerr_fixup_movem_mr:
    andi t0, opcode, 1<<6                # check if word transfer
    beqz t0, main_loop_exit              # if word, no fixup
    andi t0, opcode, 7<<3                # check if EA mode is post-increment
    bne t0, 3<<3, main_loop_exit         # otherwise, no fixup
    
    andi t0, opcode, 7
    sll t0, 2
    lw_ctx t1, M64K_OFF_AREGS,t0        # Revert the post-increment
    addiu t1, -2
    sw_ctx t1, M64K_OFF_AREGS,t0
    j main_loop_exit

address_fixup_cmpm:
    addiu m_pc, -2
    j addrerr_finish

    .set noreorder
#endif

###############################################################
# Privilege violation error handling
###############################################################
#if M64K_CONFIG_PRIVERR
check_supervisor:
    .set reorder
    .set noat
    lw_ctx $1, M64K_OFF_SR
    andi $1, SR_SUPERV
    bnez $1, 1f

    li $1, M64K_PENDINGEXC_PRIVERR
    sw_ctx t0, M64K_OFF_PENDINGEXC
    j main_loop_exit
1:  jr ra
    .set at
    .set noreorder
#endif

###############################################################
# Division by zero exception
###############################################################
#if M64K_CONFIG_DIVBYZERO
    .section .sdata
exc_dbz_pcoff: .byte -2,-2,-2,-2,-2,-4,-4,-2
               .byte -4,-6,-2,-2,-2,-2,-2,-2
    .text

exception_divbyzero:
    .set reorder
    li t0, M64K_PENDINGEXC_DIVBYZERO   # set pending exception code to DIVBYZERO
    sw_ctx t0, M64K_OFF_PENDINGEXC

    srl t0, opcode, 3                  # extract EA mode
    andi t0, 7
    bne t0, 7, dbz_fetch_pcoff
    andi t0, opcode, 7                 # if EA mode is 7, use register as submode
    add t0, 8
dbz_fetch_pcoff:
    lb t0, exc_dbz_pcoff(t0)           # get PC offset correction
    addu m_pc, t0                      # apply it
    li flag_nv, 0                      # undefined in the manual, set N=V=0
    li flag_zc, 1                      # undefined in the manual, set Z=C=0
    j main_loop_exit
    .set noreorder
#endif

###############################################################
# Immediate computation
###############################################################

    .text
ALIGN(decode_imm):
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

    .sdata
ea_table:
    .long   ea_000, ea_001, ea_010, ea_011, ea_100, ea_101, ea_110, ea_111
ea7_table:
    .long   ea7_000, ea7_001, ea7_010, ea7_011, ea7_100, ea7_101, ea7_110, ea7_111

.macro decode_opsize
    .set noat
    bnez opsize, 1f        # if opsize was not specified
    srl opsize, opcode, 6  # extract it from opcode bit 6-7
    andi opsize, 0x3
    li $1, 1
    sllv opsize, $1, opsize
    .set at
1:
.endm

    .text
    .set reorder

     # Decode EA. opsize must be set to 1, 2 or 4. 
     # When 0, extracts from opcode
ALIGN(decode_ea):
    andi t1, opcode, 0x7
decode_ea_customreg:    # Like decode_ea, but t1 is already set to the register to use in decoding
    srl t0, opcode, 1
    andi t0, 0x7 << 2
    lw t0, ea_table(t0)
    sll t1, 2
    jr t0
ea_111: # Register-less EAs
    lw t1, ea7_table(t1)
    jr t1

ea_000: # Dn
    decode_opsize
    la_ctx eaptr, M64K_OFF_DREGS+4,t1
    sub eaptr, opsize
    li eaptr32, 0
    jr ra
ea_001: # An
    decode_opsize
    la_ctx eaptr, M64K_OFF_AREGS+4,t1
    sub eaptr, opsize
    li eaptr32, 0
    jr ra
ea_010: # (An)
    lw_ctx eaptr32, M64K_OFF_AREGS,t1
    map_m68k eaptr, eaptr32
    jr ra
ea_011: # (An)+
    lw_ctx eaptr32, M64K_OFF_AREGS,t1
    decode_opsize
    add t0, eaptr32, opsize
    bne t1, 7*4, 1f   # if A7 (stack pointer), align to word
    add t0, 1
    and t0, ~1
1:  sw_ctx t0, M64K_OFF_AREGS,t1
    map_m68k eaptr, eaptr32
    jr ra
ea_100: # -(An)
    lw_ctx eaptr32, M64K_OFF_AREGS,t1   # read address register
    decode_opsize                        # decode opsize (if wasn't already)
    sub eaptr32, opsize                  # decrement address register
    bne t1, 7*4, 1f                      # if A7 (stack pointer), align to word
    and eaptr32, ~1
1:  sw_ctx eaptr32, M64K_OFF_AREGS,t1   # write back address register
    map_m68k eaptr, eaptr32              # map address to N64 space
    jr ra
ea_101: # (d16,An)
    lw_ctx eaptr32, M64K_OFF_AREGS,t1
    lh t0, 0(m_pc)
    addi m_pc, 2
    addu eaptr32, t0
    map_m68k eaptr, eaptr32
    jr ra
ea7_011: # (d8,PC,Xn)
    lw t0, pc_diff
    addu eaptr32, m_pc, t0    # Compute exact eaptr32 (in case it's read via LEA/PEA)
    j ea_110_inner    
ea_110: # (d8,An,Xn)
    lw_ctx eaptr32, M64K_OFF_AREGS,t1   # read An
ea_110_inner:
    lbu t1, 0(m_pc)  # read extension word
    lb t0, 1(m_pc)   # read 8-bit displacement
    addi m_pc, 2
    addu eaptr32, t0
    srl t0, t1, 2       # calculate pointer to Xn
    andi t0, 0xF << 2
    andi t2, t1, 0x8
    add t0, ctx
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
    lw t0, pc_diff
    addu eaptr32, m_pc, t0    # Compute exact eaptr32 (in case it's read via LEA/PEA)
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

    .sdata
rmw_table:
    .long   rmw8_easrc, rmw16_easrc, rmw32_easrc, invalid_opmode
    .long   rmw8_eadst, rmw16_eadst, rmw32_eadst, invalid_opmode

    .text
    .set noreorder

ALIGN(decode_dptr):
    srl dptr, opcode, 7       # get data register pointer
    andi dptr, 0x7 << 2       # isolate data register pointer
    la_ctx dptr, M64K_OFF_DREGS,dptr
    jr ra

ALIGN(rmw_easrc):
    andi opcode, 0xFFFF ^ (1<<8)
    j rmw
rmw_eadst:
    ori opcode, 1<<8          # force EA mode to EA-dst
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
    li rmw_bitsize , 24
    lbu_m68k t0, eaptr
    lbu t1, 3(dptr)
    jalr rmw_op
    sb result, 3(dptr)
    j main_loop
rmw16_easrc:
    check_addr_error eaptr32, read_address_error
    li rmw_bitsize , 16
    lhu_m68k t0, eaptr
    lhu t1, 2(dptr)
    jalr rmw_op
    sh result, 2(dptr)
    j main_loop
rmw32_easrc:
    check_addr_error eaptr32, read_address_error
    li rmw_bitsize , 0
    lw_m68k t0, eaptr
    lwu t1, 0(dptr)
    zx64 t0
    jalr rmw_op
    sw result, 0(dptr)
    j main_loop
rmw8_eadst:
    lbu_m68k t0, eaptr
    li rmw_bitsize , 24
    move t1, t0
    lbu t0, 3(dptr)
    jalr rmw_op
    sb_m68k result, eaptr
    j main_loop
rmw16_eadst:
    check_addr_error eaptr32, read_address_error
    lhu_m68k t0, eaptr
    li rmw_bitsize , 16
    move t1, t0
    lhu t0, 2(dptr)
    jalr rmw_op
    sh_m68k result, eaptr
    j main_loop
rmw32_eadst:
    check_addr_error eaptr32, read_address_error
    lw_m68k t0, eaptr
    li rmw_bitsize , 0
    move t1, t0
    lwl t0, 0(dptr)   # NOTE: this is to handle immediate case (where dptr is PC-relative)
    lwr t0, 3(dptr)
    zx64 t0
    zx64 t1
    jalr rmw_op
    sw_m68k result, eaptr
    j main_loop

invalid_opmode:
    move a0, opcode
    addiu a1, m_pc, -2
    jal __m64k_assert_invalid_opmode
    .set noreorder

###############################################################
# Opcodes
###############################################################

    .set reorder

OP(addq): OP(subq):
    andi t0, opcode, 3<<6
    beq t0, 3<<6, op_scc
    lax rmw_op, op_subq_rmwimpl
    andi t0, opcode, 1<<8
    bnez t0, 1f
    lax rmw_op, op_addq_rmwimpl
1:  srl t1, opcode, 9   # extract immediate value
    addi t1, -1         # convert 0 to 8
    andi t1, 0x7
    addi t1, 1
    sw t1, M64K_OFF_PENDINGEXC+4(ctx)      # store imediate into dummy space
    la dptr, M64K_OFF_PENDINGEXC+4(ctx)    # set dptr to dummy space
    li opsize, 0            # opsize not calculated yet
    andi result, opcode, 0x7<<3  # isolate EA mode
    xori result, 1<<3            # set 0 if EA mode is An
    j rmw_eadst
op_addq_rmwimpl:
    .set noreorder
    bnez result, op_add_rmwimpl_flags   # if EA mode is not An, calculate flags
    daddu result, t0, t1
    jr ra
    nop
op_subq_rmwimpl:
    bnez result, op_sub_rmwimpl_flags   # if not, calculate flags like add
    dsubu result, t1, t0
    jr ra
    nop
    .set reorder

OP(addi):
    lax rmw_op, op_add_rmwimpl
    jal_and_j decode_imm, rmw_eadst
op_subi:  # (same cacheline)
    lax rmw_op, op_sub_rmwimpl
    jal_and_j decode_imm, rmw_eadst
op_cmpi:
    lax rmw_op, op_cmp_rmwimpl
    jal_and_j decode_imm, rmw_eadst

OP(add):
    lax rmw_op, op_add_rmwimpl
    jal decode_dptr
    andi t0, opcode, 0xC0
    beq t0, 0xC0, op_adda     # if adda jump to implementation
    andi t0, opcode, 0x130    # check if the opcode is addx instead
    xori t0, 0x100
    li opsize, 0              # opsize not calculated yet
    beqz t0, op_addx          # if addx, jump to implementation
    j rmw
op_add_rmwimpl:
    daddu result, t0, t1
op_add_rmwimpl_flags:
    dsll flag_zc, result, rmw_bitsize 
    dsrl flag_x, flag_zc, 32
    sllv t0, t0, rmw_bitsize 
    sllv t1, t1, rmw_bitsize 
    daddu flag_nv, t0, t1
    jr ra

OP(sub):
    lax rmw_op, op_sub_rmwimpl
    jal decode_dptr
    andi t0, opcode, 0xC0
    beq t0, 0xC0, op_suba     # if suba jump to implementation
    andi t0, opcode, 0x130
    xori t0, 0x100
    li opsize, 0              # opsize not calculated yet
    beqz t0, op_subx          # if subx, jump to implementation
    j rmw
op_sub_rmwimpl:
    dsubu result, t1, t0
op_sub_rmwimpl_flags:
    dsll flag_zc, result, rmw_bitsize 
    dsrl flag_x, flag_zc, 32
    sllv t0, t0, rmw_bitsize 
    sllv t1, t1, rmw_bitsize 
    dsubu flag_nv, t1, t0
    jr ra

OP(nbcd):
    li opsize, 1
    lax rmw_op, op_nbcd_rmwimpl
    j rmw_eadst
OP(abcd):
    li opsize, 1
    lax rmw_op, op_abcd_rmwimpl
    j op_addx_start
op_sbcd:
    li opsize, 1
    lax rmw_op, op_sbcd_rmwimpl
    j op_addx_start
op_subx: # opsize already set to 0 by caller
    lax rmw_op, op_subx_rmwimpl
    j op_addx_start
op_addx: # opsize already set to 0 by caller
    lax rmw_op, op_addx_rmwimpl
op_addx_start:
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
    andi flag_x, 1                       # isolate flag_x to 0/1
    zx64 t2, flag_zc                     # put current Z flag (without C) into t2
    daddu result, t0, t1                 # compute full size result
    daddu result, flag_x                 # add flag_x
    dsll flag_zc, result, rmw_bitsize    # compute new flag_zc
    or flag_zc, t2                       # combine with old Z flag
    sllv t0, t0, rmw_bitsize             # put operands into MSB
    sllv t1, t1, rmw_bitsize 
    sllv flag_x, flag_x, rmw_bitsize 
    daddu flag_nv, t0, t1                # compute flag_nv
    daddu flag_nv, flag_x
    dsrl flag_x, flag_zc, 32             # copy new carry bit into flag_x
    jr ra
OP(subx_rmwimpl):
    andi flag_x, 1                       # isolate flag_x to 0/1
    zx64 t2, flag_zc                     # put current Z flag (without C) into t2
    dsubu result, t1, t0                 # compute full size result
    dsubu result, flag_x                 # subtract flag_x
    dsll flag_zc, result, rmw_bitsize    # compute new flag_zc
    or flag_zc, t2                       # combine with old Z flag
    sllv t0, t0, rmw_bitsize             # put operands into MSB
    sllv t1, t1, rmw_bitsize 
    sllv flag_x, flag_x, rmw_bitsize 
    dsubu flag_nv, t1, t0                # compute flag_nv
    dsubu flag_nv, flag_x
    dsrl flag_x, flag_zc, 32             # copy new carry bit into flag_x
    jr ra
OP(abcd_rmwimpl):
    andi flag_x, 1                       # isolate X flag
    addu result, t0, t1
    addu result, flag_x                  # calculate t0+t1+X (uncorrected result)
    xor t0, t1
    xor t0, result                       # get carries from addition (both 8-bit and 4-bit)
    addiu t1, result, 0x66
    xor t1, result                       # get carries from adding 0x66
    or t0, t1                            # combine carries
    andi t0, 0x110                       # isolate carries
    srl flag_x, t0, 8                    # set X flag to the combined 8-bit carry
    srl t1, t0, 2
    subu t0, t1                          # calculate corf
    sll t0, 23                           # align corf to top 32-bit
    sll flag_nv, result, 24              # align result to top 32-bit
    daddu flag_nv, t0                    # add corf and affact N/V
    srl result, flag_nv, 24              # shift result back to 24-bit
    zx64 flag_zc                         # clear C
    or flag_zc, result                   # accumulate Z and set C
    dsll t0, flag_x, 32
    or flag_zc, t0                       # C combines both the intermediate and the final carry
    jr ra
OP(nbcd_rmwimpl):
    move t0, t1
    li t1, 0
op_sbcd_rmwimpl:
    # Calculate uncorrected result
    andi flag_x, 1
    subu result, t1, t0
    subu result, flag_x
    # Get borrows from the subtraction
    xor t0, t1
    xor t0, result
    # Calculate corf, then align to top of 32-bit
    andi t0, 0x110
    srl flag_x, t0, 8  # Also set X flag to the borrow
    srl t1, t0, 2
    subu t0, t1
    sll t0, 23
    # Subtract corf and set sign/overflow
    sll flag_nv, result, 24
    sltu t1, flag_nv, t0 # Also update X flag on subtraction borrow
    or flag_x, t1
    dsubu flag_nv, t0
    srl result, flag_nv, 24
    # Accumulate zero and set carry
    zx64 flag_zc
    or flag_zc, result
    dsll t0, flag_x, 32
    or flag_zc, t0
    jr ra    

OP(suba):
    li result, 1
    j op_adda_start
op_adda:
    li result, 0
op_adda_start:
    addi dptr, 32                   # Point to AREG instead of DREG
    andi t0, opcode, 0x100          # Check W/L bit
    bnez t0, 1f                     # Jump to correct body
    lax rmw_op, op_adda16_rmwimpl   # ADDA 16-bit
    li opsize, 2                      # Force word size in EA decoding
    jal decode_ea                     # Decode EA
    addi eaptr, -2                    # Convert eaptr to long (for rmw32)
    j rmw32_easrc                     # Do RMW
1:  lax rmw_op, op_adda32_rmwimpl   # ADDA 32-bit
    li opsize, 4                      # Force long size in EA decoding
    jal decode_ea                     # Decode EA
    j rmw32_easrc                     # Do RMW
op_adda16_rmwimpl:
    sll t0, 16
    sra t0, 16
op_adda32_rmwimpl:
    bnez result, 1f
    daddu result, t0, t1
    jr ra
1:  dsubu result, t1, t0
    jr ra

OP(cmp):
    andi t0, opcode, 0b111 << 6
    beq t0, 0b011 << 6, op_cmpa16
    beq t0, 0b111 << 6, op_cmpa32
    bge t0, 0b100 << 6, op_eor
    lax rmw_op, op_cmp_rmwimpl
    jal decode_dptr
    li opsize, 0              # opsize not calculated yet
    j rmw    
op_cmp_rmwimpl:
    dsubu result, t1, t0
    dsll flag_zc, result, rmw_bitsize 
    sllv t0, t0, rmw_bitsize 
    sllv t1, t1, rmw_bitsize 
    dsubu flag_nv, t1, t0
    j main_loop               # skip rmw writeback

OP(cmpa16):
    li opsize, 2
    jal decode_ea
    lax rmw_op, op_cmpa16_rmwimpl
    j op_cmpa_start
op_cmpa32:
    li opsize, 4
    jal decode_ea
    lax rmw_op, op_cmp_rmwimpl
op_cmpa_start:
    jal decode_dptr
    addi dptr, 8*4
    j rmw32_easrc
op_cmpa16_rmwimpl:
    lh_m68k t0, eaptr
    zx64 t0
    j op_cmp_rmwimpl

OP(cmpm):
    li opsize, 0                      # opsize not calculated yet
    andi t1, opcode, 7                # extract Ay*4 (needed for EA calculation)
    sll t1, 2
    jal ea_011                        # calculate EA as post-increment
    sub dptr, eaptr, 4                # set dptr to decoded address, adjusting for size
    add dptr, opsize                  #  (rmw treats dptr accesses differently from eaptr)
    #if M64K_CONFIG_ADDRERR
    andi t0, opcode, 3<<6             # if the size is not byte, check for address error
    beqz t0, 1f                       #  (we do this because rmw will not check address errors in dptr)
    check_addr_error eaptr32, read_address_error
    #endif
1:  srl t1, opcode, 7                 # extract Ax*4
    andi t1, 7<<2
    jal ea_011                        # calculate EA as post-increment
    lax rmw_op, op_cmp_rmwimpl        
    j rmw_no_ea                       # do rmw


OP(mul):
    li opsize, 2
    jal decode_ea
    check_addr_error eaptr32, read_address_error
    jal decode_dptr
    andi t0, opcode, 1<<8
    bnez t0, op_muls_exec
op_mulu_exec:
    lhu t1, 2(dptr)
    lhu_m68k t0, eaptr
    multu t0, t1
    j op_mul_finish
op_muls_exec:
    lh t1, 2(dptr)
    lh_m68k t0, eaptr
    mult t0, t1
op_mul_finish:
    mflo flag_nv
    zx64 flag_zc, flag_nv
    sw flag_nv, 0(dptr)
    j main_loop

OP(div):
    li opsize, 2
    jal decode_ea
    check_addr_error eaptr32, read_address_error
    jal decode_dptr
    andi t0, opcode, 1<<8
    bnez t0, op_divs_exec
op_divu_exec:
    lw t1, 0(dptr)
    lhu_m68k t0, eaptr
    #if M64K_CONFIG_DIVBYZERO
    beqz t0, exception_divbyzero
    #endif
    divu zero, t1, t0
    mflo t0
    and t2, t0, 0xFFFF
    j op_div_finish
op_divs_exec:
    lw t1, 0(dptr)
    lh_m68k t0, eaptr
    #if M64K_CONFIG_DIVBYZERO
    beqz t0, exception_divbyzero
    #endif
    div zero, t1, t0
    mflo t0
    sll t2, t0, 16
    sra t2, 16
op_div_finish:
    bne t0, t2, op_div_overflow 
    mfhi t1
    zx64 flag_zc, t0
    sll flag_nv, t0, 16
    sh t1, 0(dptr)
    sh t0, 2(dptr)
    j main_loop
op_div_overflow:
    not32sx flag_nv
    not32 flag_nv
    zx64 flag_zc
    j main_loop

OP(group_logical):
    li opsize, 0
    jal_and_j decode_dptr, rmw

OP(andi):
    andi t0, opcode, 0b111111
    beq t0, 0b111100, op_andi_sr
    lax rmw_op, op_and_rmwimpl
    jal_and_j decode_imm, rmw_eadst
OP(ori):
    andi t0, opcode, 0b111111
    beq t0, 0b111100, op_ori_sr
    lax rmw_op, op_or_rmwimpl
    jal_and_j decode_imm, rmw_eadst

OP(and):
    li t1, 3<<6
    and t0, opcode, t1                      # Whan opsize is 3
    beq t0, t1, op_mul                      # ... it is the MUL opcode instead
    andi t0, opcode, (1<<8) | (0b110 << 3)  # When EA-Dst (bit 8) and EA mode An/Dn (bit 3-5: 000/001)
    beq t0, 1<<8, op_exg                    # ... it is the EXG opcode instead
    lax rmw_op, op_and_rmwimpl
    j op_group_logical
op_and_rmwimpl:
    and result, t0, t1
    sllv flag_nv, result, rmw_bitsize 
    zx64 flag_zc, flag_nv
    jr ra

OP(or):
    .set noat
    .set noreorder
    and t0, opcode, 3<<6
    li $1, 3<<6
    beq t0, $1, op_div
     andi t0, opcode, 0b11111<<4
    li $1, 0b10000<<4
    beq t0, $1, op_sbcd
     li $1, 0b10100<<4
    beq t0, $1, op_pack
     li $1, 0b11000<<4
    beq t0, $1, op_unpk
     nop
    .set at
    .set reorder
    lax rmw_op, op_or_rmwimpl
    j op_group_logical
op_or_rmwimpl:
    or result, t0, t1
    sllv flag_nv, result, rmw_bitsize 
    zx64 flag_zc, flag_nv
    jr ra

OP(eori):
    andi t0, opcode, 0b111111
    beq t0, 0b111100, op_eori_sr
    lax rmw_op, op_eor_rmwimpl
    jal_and_j decode_imm, rmw_eadst
OP(eor):
    andi t0, opcode, 0b111000
    beq t0, 0b001000, op_cmpm
    lax rmw_op, op_eor_rmwimpl
    j op_group_logical
op_eor_rmwimpl:
    xor result, t0, t1
    sllv flag_nv, result, rmw_bitsize 
    zx64 flag_zc, flag_nv
    jr ra

OP(not):
    srl t0, opcode, 6
    beq t0, 0b0100011011, op_mvtsr
    lax rmw_op, op_not_rmwimpl
    j rmw_eadst
op_not_rmwimpl:
    not result, t1
    sll flag_nv, result, rmw_bitsize 
    zx64 flag_zc, flag_nv
    jr ra

OP(neg):
    srl t0, opcode, 6
    beq t0, 0b0100010011, op_mvccr
    lax rmw_op, op_neg_rmwimpl
    j rmw_eadst
op_neg_rmwimpl:
    move t0, t1
    li t1, 0
    j op_sub_rmwimpl

OP(negx):
    srl t0, opcode, 6
    beq t0, 0b0100000011, op_mvfsr
    lax rmw_op, op_negx_rmwimpl
    j rmw_eadst
op_negx_rmwimpl:
    move t0, t1
    li t1, 0
    j op_subx_rmwimpl


OP(roxli): OP(roxri):
    andi opcode, 0xFFFF^(7<<3)   # turn off ir so that EA is Dn
    lax t1, op_roxl_rmwimpl
    lax t2, op_roxr_rmwimpl
    j op_shift_imm
OP(roli): OP(rori):
    andi opcode, 0xFFFF^(7<<3)   # turn off ir so that EA is Dn
    lax t1, op_rol_rmwimpl
    lax t2, op_ror_rmwimpl
    j op_shift_imm
OP(lsli): OP(lsri):
    xori opcode, 1<<3
    lax t1, op_lsl_rmwimpl
    lax t2, op_lsr_rmwimpl
    j op_shift_imm
OP(asli): OP(asri):
    lax t1, op_asl_rmwimpl
    lax t2, op_asr_rmwimpl
op_shift_imm:
    andi t0, opcode, 1<<8
    move rmw_op, t1
    bnez t0, asli_start
    move rmw_op, t2
asli_start:
    srl t2, opcode, 9    # extract immediate value in t2
    addi t2, -1          # if value is 0, it means 8
    andi t2, 7
    addi t2, 1
    j rmw_eadst          # call RMW (note opsize=0 in OP(shifts)).

OP(lsl): OP(lsr):
    andi t0, opcode, 1<<8
    andi opcode, 0xFFFF^(1<<5)^(1<<3)   # turn off ir so that EA is Dn
    lax rmw_op, op_lsl_rmwimpl
    bnez t0, group_shifts
    lax rmw_op, op_lsr_rmwimpl
    j group_shifts
OP(asl): OP(asr):
    andi t0, opcode, 1<<8
    andi opcode, 0xFFFF^(1<<5)   # turn off ir so that EA is Dn
    lax rmw_op, op_asl_rmwimpl
    bnez t0, group_shifts
    lax rmw_op, op_asr_rmwimpl
    j group_shifts
OP(rol): OP(ror):
    andi t0, opcode, 1<<8
    andi opcode, 0xFFFF^(7<<3)   # turn off ir so that EA is Dn
    lax rmw_op, op_rol_rmwimpl
    bnez t0, group_shifts
    lax rmw_op, op_ror_rmwimpl
    j group_shifts
OP(roxl): OP(roxr):
    andi t0, opcode, 1<<8
    andi opcode, 0xFFFF^(7<<3)   # turn off ir so that EA is Dn
    lax rmw_op, op_roxl_rmwimpl
    bnez t0, group_shifts
    lax rmw_op, op_roxr_rmwimpl
    j group_shifts

group_shifts:
    srl t0, opcode, 7            # read source Dn register into t2; similar to decode_dptr,
    andi t0, 0x7<<2              #  but we use t2 instead so that we can reuse the impl
    lw_ctx t2, M64K_OFF_DREGS,t0 #  functions used by immediate shifts.
    andi t2, 63                  # mask the shift amount
    j rmw_eadst                  # call RMW (note opsize=0 in OP(shifts))

OP(lsl_rmwimpl): # t1=register, t2=shift amount
    dsllv result, t1, t2                # calculate extended result
    dsllv flag_zc, result, rmw_bitsize  # affect Z and C flags
    sllv flag_nv, result, rmw_bitsize   # affect N flag and reset V flag
    beqz t2, 1f                         # affect X flag only if shift amount is 0
    dsrl flag_x, flag_zc, 32
1:  jr ra
OP(lsr_rmwimpl): # t1=register, t2=shift amount
    dsrlv result, t1, t2                # do the right shift into result
    addi t0, t2, -1                     # reduce shift amount
    dsrlv flag_zc, t1, t0               # shift one less (last bit = carry)
    dsll flag_zc, 32                    # extract carry bit
    or flag_zc, result                  # combine with zero bit
    sll flag_nv, result, rmw_bitsize    # set N to result sign and reset V
    beqz t2, 1f                         # affect X flag only if shift amount is 0
    dsrl flag_x, flag_zc, 32
1:  jr ra
OP(asl_rmwimpl): # t1=register, t2=shift amount
    dsllv result, t1, t2                # calculate extended result
    dsllv flag_zc, result, rmw_bitsize  # affect Z and C flags
    sllv flag_nv, result, rmw_bitsize   # affect N flag and reset V flag
    dsrav t0, flag_nv, t2               # shift back expected sign bits and
    sllv t1, t1, rmw_bitsize            # ...
    sne t0, t1                          # compare to sign-extended input
    dsll t0, 63                         # set V flag on mismatch
    xor flag_nv, t0
    beqz t2, 1f                         # affect X flag only if shift amount is 0
    dsrl flag_x, flag_zc, 32
1:  jr ra
OP(asr_rmwimpl): # t1=register, t2=shift amount
    sllv flag_nv, t1, rmw_bitsize       # set N flag to sign of input, reset V flag
    dsll result, flag_nv, 32            # align input to the upper bits
    dsrav result, result, t2            # arithmetic shift right
    dsrlv result, result, rmw_bitsize   # align result to bit 32, carry to bit 31
    dsrl flag_zc, result, 31            # extract carry bit
    dsll flag_zc, 32
    dsrl result, 32                     # align result to bit 0, zero-extended
    or flag_zc, result                  # combine Z and C
    beqz t2, 1f                         # affect X flag only if shift amount is 0
    dsrl flag_x, flag_zc, 32
1:  jr ra
OP(rol_rmwimpl): # t1=register, t2=shift amount
    sll t3, opsize, 3                   # calculate bitsize in bits (8, 16, 32)
    addi t0, t3, -1                     # calculate bytessize mask (7, 15, 31)
    neg result, t2
    and result, t0                      # res = shift right amount (range 0..N-1)
    sub t0, t3, result                  # t0 = shift left amount (range 1..N)
    dsllv t0, t1, t0                    # shift left
    dsrlv t1, t1, result                # shift right
    or result, t0, t1                   # combine
    dsllv flag_zc, result, rmw_bitsize  # affect Z and C flags
    sllv flag_nv, result, rmw_bitsize   # affect N flag and reset V flag
    bnez t2, 1f                         # clear C flag if shift amount is 0
    zx64 flag_zc
1:  jr ra
OP(ror_rmwimpl): # t1=register, t2=shift amount
    sll t3, opsize, 3                   # calculate bitsize in bits (8, 16, 32)
    addi result, t3, -1                 # calculate bytessize mask (7, 15, 31)
    and t0, t2, result
    sub result, t3, t0                  # t0 = shift right amount (range 1..N)
    dsrlv t0, t1, t0                    # shift right
    dsllv t1, t1, result                # shift left
    or result, t0, t1                   # combine
    sllv flag_nv, result, rmw_bitsize   # affect N flag and reset V flag
    move flag_zc, flag_nv               # affect Z and C flags
    bnez t2, 1f                         # clear C flag if shift amount is 0
    zx64 flag_zc
1:  jr ra

OP(roxr_rmwimpl): # t1=register, t2=shift amount
    andi opcode, 0xFFFF^(1<<8)          # clear direction bit again (it was turned on in rmw_eadst) 
OP(roxl_rmwimpl): # t1=register, t2=rotate amount
    andi flag_x, 1
    dsll t0, flag_x, 32                 # move input carry (from X) to bit 32
    dsll t1, rmw_bitsize                # left-align input register to bit 31
    or t1, t0                           # combine with input carry
    srl t3, opsize, 1
    add t3, 3                           # t3 = 3,4,5 for b,w,l. Next lines explain the byte case.
    xori opsize, rmw_bitsize, 31        # bitsize = 7,15,31 for b,w,l
    srlv t0, t2, t3                     # t0 = ra>>3 = ra/8
    and t2, opsize                      # t2 = ra&7 = ra%8
    sub t2, t0                          # ra%8 - ra/8 = ra%9 (though with negative remainders)
    andi result, opcode, 1<<8           # check rotate direction
    bnez result, 1f                     # if roxr, negate t2
    neg t2
1:  addi opsize, 2                      # opsize = 9,17,31 for b,w,l
    bgtz t2, 1f                         # adjust remainder if negative or zero
    add t2, opsize                      # t2 = 1..9 = shift-left amount 
1:  sub t3, opsize, t2                  # t3 = 8..0 = shift-right amount
    dsllv t0, t1, t2                    # shift left
    dsrlv t1, t1, t3                    # shift right
    or result, t0, t1                   # combine
    dsrlv result, result, rmw_bitsize   # shift back result into correct place
    dsllv flag_zc, result, rmw_bitsize  # affect Z and C flags
    sllv flag_nv, result, rmw_bitsize   # affect N flag and reset V flag
    dsrl flag_x, flag_zc, 32
    jr ra

OP(aslm): OP(asrm):
    jal decode_ea                       # decode EA (note opsize=2 in OP(bits))
    check_addr_error eaptr32, read_address_error
    andi t1, opcode, 0x100
    lh_m68k t0, eaptr
    bnez t1, op_asl_ea_left
op_asl_ea_right:
    sra result, t0, 1
    andi flag_x, t0, 1
    j op_asl_ea_finish
op_asl_ea_left:
    sll result, t0, 1
    srl flag_x, result, 16
op_asl_ea_finish:
    sh_m68k result, eaptr
    dsll flag_nv, result, 16
    zx64 flag_zc, flag_nv
    dsll t0, flag_x, 32
    or flag_zc, t0
    j main_loop

OP(lslm): OP(lsrm):
    jal decode_ea                       # decode EA (note opsize=2 in OP(bits))
    check_addr_error eaptr32, read_address_error
    andi t1, opcode, 0x100
    lhu_m68k t0, eaptr
    bnez t1, op_lsl_ea_left
op_lsl_ea_right:
    srl result, t0, 1
    andi flag_x, t0, 1
    j op_lsl_ea_finish
op_lsl_ea_left:
    sll result, t0, 1
    srl flag_x, result, 16
op_lsl_ea_finish:
    sh_m68k result, eaptr
    sll flag_nv, result, 16
    zx64 flag_zc, flag_nv
    dsll t0, flag_x, 32
    or flag_zc, t0
    j main_loop

OP(rolm): OP(rorm):
    jal decode_ea                       # decode EA (note opsize=2 in OP(bits))
    check_addr_error eaptr32, read_address_error
    andi t1, opcode, 0x100
    lhu_m68k t0, eaptr
    bnez t1, op_rom_ea_left
op_rom_ea_right:
    srl result, t0, 1
    sll t0, 15
    or result, t0
    sll flag_nv, result, 16
    move flag_zc, flag_nv
    sh_m68k result, eaptr
    j main_loop
op_rom_ea_left:
    sll result, t0, 1
    srl t0, 15
    or result, t0
    sll flag_nv, result, 16
    dsll flag_zc, result, 16
    sh_m68k result, eaptr
    j main_loop

OP(roxlm): OP(roxrm):
    jal decode_ea                       # decode EA (note opsize=2 in OP(bits))
    check_addr_error eaptr32, read_address_error
    andi t1, opcode, 0x100        # check rotate direction
    lhu_m68k t0, eaptr            # fetch memory operand
    andi flag_x, 1                # isolate input carry bit
    bnez t1, op_roxm_ea_left
op_roxm_ea_right:
    srl result, t0, 1
    sll flag_x, 15
    sll t0, 16
    or result, t0
    j op_roxm_finish
op_roxm_ea_left:
    sll result, t0, 1             # shift left, new carry in bit 16
op_roxm_finish:
    or result, flag_x             # merge input carry
    sll flag_nv, result, 16       # set N flag, and clear V flag
    dsll flag_zc, result, 16      # set Z and C flags
    dsrl flag_x, flag_zc, 32      # set X flag as a copy of C
    sh_m68k result, eaptr         # store result
    j main_loop

OP(clr):
    lax rmw_op, op_clr_rmwimpl
    j rmw_eadst                   # run rmw cycle (note: clr does a dummy read on 68000)
op_clr_rmwimpl:
    li result, 0
    li flag_nv, 0
    li flag_zc, 0
    jr ra

OP(tst):
    srl rmw_bitsize, opcode, 6
    andi rmw_bitsize, 3
    beq rmw_bitsize, 3, op_tas
    jal decode_ea
    beqz rmw_bitsize, tst_byte
    check_addr_error eaptr32, read_address_error
    addi rmw_bitsize, -1
    beqz rmw_bitsize, tst_word
tst_long:
    lw_m68k t0, eaptr
    move flag_nv, t0
    zx64 flag_zc, t0
    j main_loop
tst_word:
    lhu_m68k t0, eaptr
    move flag_zc, t0
    sll flag_nv, t0, 16
    j main_loop
tst_byte:
    lbu_m68k t0, eaptr
    move flag_zc, t0
    sll flag_nv, flag_zc, 24
    j main_loop

OP(moveb):
    li opsize, 1
    jal decode_ea
    lbu_m68k t0, eaptr
    move result, t0
    srl opcode, 3
    srl t1, opcode, 6
    andi t1, 7
    jal decode_ea_customreg
    sb_m68k result, eaptr
    move flag_zc, result
    sll flag_nv, flag_zc, 24
    j main_loop

OP(movew):
    li opsize, 2
    jal decode_ea
    check_addr_error eaptr32, read_address_error
    and t1, opcode, 0x1C0
    lhu_m68k t0, eaptr              # read word
    beq t1, 0x040, op_movea16
    move result, t0
    move flag_zc, result            # this is already good as flag_zc (as C is always cleared)
    sll flag_nv, result, 16         # flag_nv is the sign extension of the 16 bit value
    srl opcode, 3                   # move the EA dst mode into the EA src mode field
    srl t1, opcode, 6               # decode the EA dst register
    andi t1, 7
    jal decode_ea_customreg         # decode dst EA
    check_addr_error eaptr32, write_address_error
    sh_m68k result, eaptr           # store the word into the destination
    j main_loop

OP(movel):
    li opsize, 4
    jal decode_ea
    check_addr_error eaptr32, read_address_error
    and t1, opcode, 0x1C0
    lw_m68k t0, eaptr               # read unaligned 32-bit word (sign-extended)
    move result, t0
    beq t1, 0x040, op_movea32
    move flag_nv, result            # It is already good as flag_nv, so store it there
    zx64 flag_zc, flag_nv           # flag_zc is the zero-extended copy (as C is always cleared)
    srl opcode, 3
    srl t1, opcode, 6
    andi t1, 7
    jal decode_ea_customreg
    check_addr_error eaptr32, write_address_error
    andi t0, opcode, 7<<3           # isolate EA-destination mode
    xori t0, 4<<3                   # check if it's pre-decrement mode
    beqz t0, 1f                     # if so, write memory in reversed order
    .set noreorder
    swl result, 0(eaptr)            # standard order: SWL+SWR
    j main_loop
1:  swr result, 3(eaptr)            # reversed order: SWR+SWL
    j main_loop
    swl result, 0(eaptr)
    .set reorder

OP(moveq):
    sll flag_nv, opcode, 24
    sra flag_nv, 24
    andi flag_zc, opcode, 0xFF
    srl t1, opcode, 7
    andi t1, 0x7 << 2
    sw_ctx flag_nv, M64K_OFF_DREGS,t1
    j main_loop

OP(movea16):
    lh_m68k t0, eaptr
    move result, t0
op_movea32:
    srl t0, opcode, 7
    andi t0, 0x7<<2
    sw_ctx result, M64K_OFF_AREGS,t0
    j main_loop

    #define movem_mask        rmw_bitsize
    #define movem_op          rmw_op
    #define ea_reg            t1            // set by decode_ea
    #define reg_ptr           dptr
    #define reg_incr          t3
    #define eaptr32_diff      t2
    #define ctz_lut           opcode
    #define reg_incr_3x       a3

    #define MOVEM_4BIT        1            // Faster 4-bit-at-a-time movem

OP(movem_rm):  # reg -> mem
    andi t0, opcode, 7<<3
    beqz t0, op_ext
    andi t0, opcode, 1<<6                   # check if word/long
    beqz t0, movem_rm16                     # jump to 16 bit variant setup
    li opsize, 4                            # 32-bit variant: opsize is 4
    lax movem_op, movem_rm_impl_32          # 32-bit variant: implementation function
    j movem_start
movem_rm16:
    li opsize, 2                            # 16-bit variant: opsize is 2
    lax movem_op, movem_rm_impl_16          # 16-bit variant: implementation function
    j movem_start

OP(movem_mr): # mem -> reg
    andi t0, opcode, 1<<6                   # check if word/long
    beqz t0, movem_mr16                     # jump to 16 bit variant setup
    li opsize, 4
    lax movem_op, movem_mr_impl_32
    j movem_start
movem_mr16:
    li opsize, 2
    lax movem_op, movem_mr_impl_16

movem_start:  # Generic implementation (16/32-bit, mem->reg/reg->mem)
    lhu movem_mask, 0(m_pc)                 # fetch register mask
    addiu m_pc, 2
    jal decode_ea                           # decode EA
    la_ctx reg_ptr, M64K_OFF_DREGS-4       # Initialize pointer to register array (one slot before)
    li reg_incr, 4                          # Initialize register pointer increment (4 bytes)
    li reg_incr_3x, 12                      # Initialize 3x register pointer increment (12 bytes)
    andi t0, opcode, 7<<3                   # check if EA is predecrement or postincrement
    #if MOVEM_4BIT
    li ctz_lut, 0x67646764                  # Load CTZ table (WARN: reusing opcode register)
    #endif
    beq t0, 3<<3, movem_postincr            # jump to setup the postincrement variant
    beq t0, 4<<3, movem_predecr             # jump to setup the predecrement variant
    li ea_reg, -1                           # Otherwise (standard EA), no register writeback is needed
movem_pre_loop:
    check_addr_error eaptr32, read_address_error

#if MOVEM_4BIT
    # Main loop: 4-bit version. Uses a 4-bit based CTZ primitive
    # to process four bits at a time. This is slightly faster
    # than the 1-bit version (and much faster in worst cases).
movem_loop:
    .set noreorder
    andi t0, movem_mask, 1
    bnez t0, 1f
     addu reg_ptr, reg_incr
    srlv t0, ctz_lut, movem_mask
    andi t0, 3
    bnez t0, 2f
     srlv movem_mask, movem_mask, t0
    beqz movem_mask, movem_mr_end
     srl movem_mask, 4
    j movem_loop
     addu reg_ptr, reg_incr_3x
2:  bgez reg_incr, 3f
     sll t0, 2
    neg t0, t0
3:  addu reg_ptr, t0
1:  srl movem_mask, 1
    #if M64K_CONFIG_ADDR_WRAP
    jalr movem_op
     addu eaptr32, opsize
    .set reorder
    map_m68k eaptr, eaptr32
    j movem_loop
    #else
    .set reorder
    jalr movem_op
    addu eaptr, opsize
    j movem_loop
    #endif
#else
    # Main loop: 1-bit version. This is slightly slower than the
    # 4-bit version, but it is more compact and easier to understand.
movem_loop:                              # Main movem loop
    .set noreorder
    beqz movem_mask, movem_mr_end        # If the mask is empty, we are done
1:   andi t0, movem_mask, 1              # Check if bit 0 of the mask is set
    srl movem_mask, 1                    # Shift the mask
    beqz t0, 1b                          # If not, loop again
     addu reg_ptr, reg_incr              # Increment register pointer for each bit
    #if M64K_CONFIG_ADDR_WRAP
    jalr movem_op                        # Do one reg/mem transfer
     addu eaptr32, opsize                # Increment memory pointer (full 32-bit value
    .set reorder
    map_m68k eaptr, eaptr32              # Map the 32-bit memory pointer to the N64 address space
    j movem_loop                         # Loop again
    #else
    .set reorder
    jalr movem_op                        # Do one reg/mem transfer
    addu eaptr, opsize                   # Increment memory pointer
    j movem_loop                         # Loop again
    #endif
#endif

movem_mr_end:
    bltz ea_reg, main_loop                   # If no register writeback is needed, we are done
    addu eaptr32, eaptr, eaptr32_diff        # Calculate the new 32-bit address given the current pointer
    sw_ctx eaptr32, M64K_OFF_AREGS,ea_reg   # Write back the new address to the address register
    j main_loop

movem_predecr:                               # Predecrement variant
    addu t0, eaptr32, opsize                 # Revert the predecrement in the address register
    sw_ctx t0, M64K_OFF_AREGS,t1            #  (beheavior specific of M68000)
    subu eaptr32_diff, t0, eaptr             # Calculate the difference between the 32-bit address and the pointer
    neg opsize                               # Negate opsize, so that we can use it to decrement the memory location
    la_ctx reg_ptr, M64K_OFF_AREGS+8*4      # Initialize pointer to register array (one slot after)
    neg reg_incr                             # Negate register pointer increment (-4 bytes)
    neg reg_incr_3x                          # Negate 3x register pointer increment (-12 bytes)
    j movem_pre_loop
movem_postincr:                              # Postincrement variant
    check_addr_error eaptr32, read_address_error  # Check if the post-incremented address is valid
    subu eaptr32_diff, eaptr32, eaptr        # Calculate the difference between the 32-bit address and the pointer
    subu t0, eaptr32, opsize                 # Rever the post-increment in the address register
    sw_ctx t0, M64K_OFF_AREGS,t1            #  (beheavior specific of M68000)
    j movem_loop


movem_mr_impl_16:
    lh_m68k t0, eaptr
    sw t0, 0(reg_ptr)
    jr ra
movem_mr_impl_32:
    lw_m68k t0, eaptr
    sw t0, 0(reg_ptr)
    jr ra
movem_rm_impl_16:
    lh result, 2(reg_ptr)
    sh_m68k result, eaptr
    jr ra
movem_rm_impl_32:
    lw result, 0(reg_ptr)
    sw_m68k result, eaptr
    jr ra

    #undef movem_mask
    #undef movem_op
    #undef ea_reg        
    #undef reg_ptr       
    #undef reg_incr      
    #undef eaptr32_diff  
    #undef ctz_lut
    #undef reg_incr_3x


OP(mvfsr):
    check_supervisor
    li opsize, 2
    jal decode_ea
    check_addr_error eaptr32, read_address_error
    jal flush_sr
    lh_m68k t0, eaptr               # dummy read before write
    move result, t1
    sh_m68k result, eaptr
    j main_loop

OP(mvccr):
    li opsize, 2
    jal decode_ea
    check_addr_error eaptr32, read_address_error
    jal flush_sr
    lw_ctx t1, M64K_OFF_SR
    lhu_m68k t0, eaptr
    and t1, SR_MASK ^ CCR_MASK
    and t0, CCR_MASK
    or t0, t1
    sw_ctx t0, M64K_OFF_SR
    jal_and_j reload_sr, main_loop

OP(mvtsr):
    check_supervisor
    li opsize, 2
    jal decode_ea
    check_addr_error eaptr32, read_address_error
    jal flush_sr
    lhu_m68k t0, eaptr
    and t0, SR_MASK
    sw_ctx t0, M64K_OFF_SR
    jal_and_j reload_sr, main_loop

OP(rmw_sr_imm):
    andi t0, opcode, 1<<6           # check if the access is just to CCR or the full SR
    bnez t0, rmw_sr_full
    li rmw_srmask, CCR_MASK         # use CCR mask to only modify CCR bits
    j rmw_sr_start
rmw_sr_full:
    check_supervisor                # SR access requires supervisor mode
    li rmw_srmask, SR_MASK          # use SR mask to modify all implemented bits in SR
rmw_sr_start:
    add m_pc, 2
    jal flush_sr                    # sync SR with current values of flags / stack pointer
    lhu t0, -2(m_pc)                # read immediate value
    lw_ctx t1, M64K_OFF_SR         # load SR
    jal rmw_op                      # execute operation
    sw_ctx t0, M64K_OFF_SR         # store modified SR
    jal_and_j reload_sr, main_loop  # reload flags / stack pointer

OP(andi_sr):
    lax rmw_op, op_andi_sr_impl
    j op_rmw_sr_imm
op_andi_sr_impl:
    not rmw_srmask
    or t0, rmw_srmask
    and t0, t1
    jr ra
OP(ori_sr):
    lax rmw_op, op_ori_sr_impl
    j op_rmw_sr_imm
op_ori_sr_impl:
    and t0, rmw_srmask
    or t0, t1
    jr ra
OP(eori_sr):
    lax rmw_op, op_eori_sr_impl
    j op_rmw_sr_imm
op_eori_sr_impl:
    and t0, rmw_srmask
    xor t0, t1
    jr ra

OP(ext):
    andi dptr, opcode, 7                 # extract dreg index
    sll dptr, 2
    andi t0, opcode, 1<<6                # opcode bit 6: 1=word, 0=byte
    la_ctx dptr, M64K_OFF_DREGS,dptr    # compute dreg address
    bnez t0, ext_long
ext_word:
    lb flag_nv, 3(dptr)                  # extend byte to word
    sh flag_nv, 2(dptr)
    zx64 flag_zc, flag_nv
    j main_loop
ext_long:
    lh flag_nv, 2(dptr)                  # extend word to long
    sw flag_nv, 0(dptr)
    zx64 flag_zc, flag_nv
    j main_loop

OP(swap):
    andi dptr, opcode, 7                 # extract dreg index
    sll dptr, 2
    la_ctx dptr, M64K_OFF_DREGS,dptr    # compute dreg address
    lh t0, 0(dptr)
    lh flag_nv, 2(dptr)
    sh t0, 2(dptr)
    sh flag_nv, 0(dptr)
    lwu flag_zc, 0(dptr)
    j main_loop

OP(mvusp):
    check_supervisor
    andi t0, opcode, 8         # DR bit
    sll t1, opcode, 2          # Isolate register offset
    andi t1, 0x7<<2
    bnez t0, mvusp_read        # See if it's read or write
    lw_ctx t2, M64K_OFF_AREGS,t1
    sw_ctx t2, M64K_OFF_USP
    j main_loop
mvusp_read:
    lw_ctx t2, M64K_OFF_USP
    sw_ctx t2, M64K_OFF_AREGS,t1
    j main_loop

OP(pea):
    andi t0, opcode, 1<<7
    bnez t0, op_movem_rm
    andi t0, opcode, 1<<6
    beqz t0, op_nbcd
    andi t0, opcode, 0b111000
    beqz t0, op_swap
    li opsize, 2
    jal decode_ea
    lw_ctx t0, M64K_OFF_AREGS+7*4
    addiu t0, -4
    check_addr_error t0, write_address_error
    map_m68k t1, t0
    move result, eaptr32
    sw_m68k result, t1
    sw_ctx t0, M64K_OFF_AREGS+7*4
    j main_loop

OP(lea):
    andi t0, opcode, 1<<6
    beqz t0, op_chk
    li opsize, 2
    jal decode_ea
    srl t1, opcode, 7
    andi t1, 0x7<<2
    sw_ctx eaptr32, M64K_OFF_AREGS,t1
    j main_loop

ALIGN(push32):
    li t0, -4
pushn:
    lw_ctx t1, M64K_OFF_AREGS+7*4
    addu t1, t0
    sw_ctx t1, M64K_OFF_AREGS+7*4
    check_addr_error t1, write_address_error
    map_m68k t0, t1
    jr ra

# Pop from the stack:
#
# * pop16: pop 16 bits
# * pop32: pop 32 bits
# * popn:  pop the number of bytes in t0
#
# The function returns the address of the stack slot in t0. The caller
# must perform the actual read.
ALIGN(pop16):
    li t0, 2
    j popn
pop32:
    li t0, 4
popn:
    lw_ctx t1, M64K_OFF_AREGS+7*4       # read SP
    check_addr_error t1, read_address_error
    .set noat
    addu $1, t1, t0                      # SP += 2/4
    map_m68k t0, t1
    sw_ctx $1, M64K_OFF_AREGS+7*4       # write SP
    .set at
    jr ra

OP(link):
    andi t2, opcode, 7                 # extract An from opcode
    sll t2, 2
    la_ctx t2, M64K_OFF_AREGS,t2      # compute An pointer

    andi t1, opcode, 1<<3
    bnez t1, op_unlk

    jal push32
    lw result, 0(t2)    # read An
    sw_m68k result, t0  # An -> (SP)
    sw t1, 0(t2)        # SP -> An

    lh t2, 0(m_pc)  # read displacement
    addiu m_pc, 2
    addu t1, t2     # SP += disp
    sw_ctx t1, M64K_OFF_AREGS+7*4
    j main_loop

OP(unlk):
    lw t1, 0(t2)                         # read An
    sw_ctx t1, M64K_OFF_AREGS+7*4       # An -> SP
    jal pop32
    lwl t3, 0(t0)                        # read (SP)
    lwr t3, 3(t0)                        # read (SP)
    sw t3, (t2)                          # (SP) -> An
    j main_loop

OP(jmp):
    jal decode_ea
jmp_exec:
    map_m68k m_pc, eaptr32             # map 32-bit target to host pointer
    subu t0, eaptr32, m_pc             # save new pc_diff
    sw t0, pc_diff
    check_addr_error eaptr32, pc_address_error
    j main_loop

OP(jsr):
    jal decode_ea
jsr_exec:
    lw result, pc_diff
    addu result, m_pc
    jal push32
    sw_m68k result, t0
    j jmp_exec

OP(rts):
    jal pop32
    lwl eaptr32, 0(t0)
    lwr eaptr32, 3(t0)
    j jmp_exec

OP(bsr):
    lb t3, -1(m_pc)                    # fetch signed 8-bit displacement (from opcode)
    bnez t3, bsr_exec                  # if displacement is 0, there's a 16-bit displacement
    lh t3, 0(m_pc)                     # fetch 16-bit displacement
    addiu m_pc, 2
    addiu t3, -2
bsr_exec:
    lw eaptr32, pc_diff                # calculate full 32bit branch target (using pc_diff)
    addu eaptr32, t3                   
    addu eaptr32, m_pc
    j jsr_exec

OP(bcc):
    andi t0, opcode, 0xFF00
    beq t0, 0x6100, op_bsr
    lb t3, -1(m_pc)                    # fetch signed 8-bit displacement (from opcode)
    bnez t3, bcc_exec                  # if displacement is 0, there's a 16-bit displacement
bcc_disp_16:
    lh t3, 0(m_pc)                     # fetch 16-bit displacement
    addiu m_pc, 2                      # increment PC
    addiu t3, -2                       # displacement refers to PC before increment
bcc_exec:
    jal check_cc
    beqz t0, main_loop                 # cc is false => branch is not taken
branch_exec:
    lw eaptr32, pc_diff                # calculate full 32bit branch target (using pc_diff)
    addu eaptr32, t3                   
    addu eaptr32, m_pc
    j jmp_exec

OP(dbcc):
    addiu m_pc, 2
    jal check_cc
    bnez t0, main_loop
    andi t0, opcode, 0x7
    sll t0, 2
    la_ctx dptr, M64K_OFF_DREGS,t0
    lhu t0, 2(dptr)
    addiu t1, t0, -1
    sh t1, 2(dptr)
    beqz t0, main_loop
    lh t3, -2(m_pc)
    addiu t3, -2
    j branch_exec

OP(scc):
    andi t0, opcode, 0b111000
    beq t0, 0b001000, op_dbcc
    li opsize, 1
    jal decode_ea
    lbu_m68k t0, eaptr                 # dummy fetch (performed by 68000)
    jal check_cc
    neg result, t0                     # convert 0x01 into 0xFF
    sb_m68k result, eaptr              # store result
    j main_loop

    .section .sdata
opcctable:
    .long   cc_t, cc_hi, cc_cc, cc_ne, cc_vc, cc_pl, cc_ge, cc_gt

    .text
ALIGN(check_cc):
    srl t0, opcode, 7
    andi t0, 0x7<<2
    lw t0, opcctable(t0)               # fetch condition code function pointer
    srl t2, opcode, 8
    andi t2, 1                         # t2=1 => invert branch condition
    jr t0                              # call condition code function
    # Condition codes checking functions
    # Input: t2 => 0: standard chaeck, 1: inverted chack
    # Output: t0 => 0: branch not taken, 1: branch taken
cc_t:  # true
    xori t0, t2, 1
    jr ra
cc_cc: # carry clear (C == 0)
    dsrl t0, flag_zc, 32
    andi t0, 1
    seq t0, t2
    jr ra
cc_ne: # not equal (Z == 0)
    zx64 t0, flag_zc
    seq t0, zero
    seq t0, t2
    jr ra
cc_pl: # plus (N == 0)
    srl t0, flag_nv, 31
    seq t0, t2
    jr ra
cc_hi: # hi (C==0 && Z==0)
    zx64 t0, flag_zc
    seq t0, zero
    dsrl t1, flag_zc, 32
    andi t1, 1
    or t0, t1
    seq t0, t2
    jr ra
cc_vc: # overflow clear (V == 0)
    dsrl t0, flag_nv, 63
    srl t1, flag_nv, 31
    xor t0, t1
    seq t0, t2
    jr ra
cc_ge: # greater equal (N == V) 
    sge t0, flag_nv, zero
    xor t0, t2
    jr ra
cc_gt: # greater than (N == V && Z == 0)
    sge t0, flag_nv, zero
    zx64 t1, flag_zc
    sne t1, zero
    and t0, t1
    xor t0, t2
    jr ra

OP(reset):
    check_supervisor
    addiu m_cycles, -128
    li t0, M64K_PENDINGEXC_RSTO
    sw_ctx t0, M64K_OFF_PENDINGEXC     # exit with code M64K_PENDINGEXC_RSTO
    j main_loop_exit

OP(rtr):
    li rmw_srmask, CCR_MASK
    j rte_exec
op_rte:
    li rmw_srmask, SR_MASK
    check_supervisor
rte_exec:
    li t0, 6                            # pop 6 bytes from the stack
    jal popn
    move eaptr, t0                      # store pointer to frame in eaptr
    jal flush_stack                     # store current stack pointer to USP/SSP, return t1=SR
    lhu t0, 0(eaptr)                    # read SR from stack
    and t0, rmw_srmask
    not rmw_srmask
    and t1, rmw_srmask
    or t0, t1
    sw_ctx t0, M64K_OFF_SR
    lwl eaptr32, 2(eaptr)               # read PC from stack
    lwr eaptr32, 5(eaptr)
    jal_and_j reload_sr, jmp_exec       # reload flags/A7 using SR, and then jump to new PC

OP(trap):
    andi eaptr32, opcode, 0xF
    addi eaptr32, 32
trap:
    jal flush_sr
    ori t1, SR_SUPERV
    sw_ctx t1, M64K_OFF_SR
    jal reload_stack

    li t0, -6
    jal pushn
    lw result, pc_diff
    addu result, m_pc
    swl result, 2(t0)
    swr result, 5(t0)

    lw_ctx result, M64K_OFF_SR
    sh result, 0(t0)

    lw_ctx t1, M64K_OFF_VBR
    sll eaptr32, 2
    addu eaptr32, t1
    map_m68k eaptr, eaptr32
    check_addr_error eaptr32, read_address_error
    lw eaptr32, 0(eaptr)
    j jmp_exec

OP(trapv):
    li t2, 0
    jal cc_vc
    bnez t0, main_loop
    li eaptr32, 7
    j trap

    #define bitidx_mask     result
OP(bitsr):  # bit operation using a register as index
    jal_and_j decode_dptr, op_bits_start
op_bitsi:   # bit operation using immediate value as index
    addi dptr, m_pc, -2
    addiu m_pc, 2
op_bits_start:
    lax rmw_op, bits_impl
    andi t0, opcode, 0b111000
    beq t0, 0<<3, op_bits_dreg   # check if EA mode is Dn
    beq t0, 1<<3, op_movep       # check if EA mode is An => movep
op_bits_ea:
    li bitidx_mask, 7            # standard EA: 8-bit operation
    li opsize, 1
    jal_and_j decode_ea, rmw8_eadst
op_bits_dreg:
    li bitidx_mask, 31           # EA is Dn: 32-bit operation
    li opsize, 4
    jal_and_j decode_ea, rmw32_eadst
bits_impl:  # First part is common to all bit operations
    lax rmw_op, bittable         # load bittable pointer
    srl t2, opcode, 3            # extract bitop from opcode, multiplied by 8
    andi t2, 3<<3                # (0=btst, 8=bchg, 16=bclr, 24=bset)
    add rmw_op, t2               # calculate bitop pointer
    dsll t2, zx64_mask, 32       # t2 = ffffffff00000000
    and flag_zc, t2              # isolate C flag
    and t0, bitidx_mask          # mask bit index (either 0..7 o 0..31)
    li t2, 1                        
    sllv t2, t2, t0              # t2 = 1 << bitindex
    and t3, t2, t1               # t3 = value & (1 << bitindex)
    or flag_zc, t3               # set Z flag with current bit value
    jr rmw_op                    # jump to bitop function
bittable:
    nop                          # btst: no writeback necessary
    j main_loop            
    xor result, t1, t2           # bchg: result = value ^ (1 << bitindex)
    jr ra
    xor result, t1, t3           # bclr: result = value ^ (value & (1 << bitindex))
    jr ra
    or result, t1, t2            # bset: result = value | (1 << bitindex)
    jr ra
    #undef bitmask

OP(movep): # NTOE: dptr was already decoded in op_bits
    xori opcode, 1<<5            # convert EA mode to (d16,An)
    andi t0, opcode, 1<<6        # check size bit
    li opsize, 4                 #  bit 6 = 1 => 32-bit operation
    bnez t0, movep_start
    li opsize, 2                 #  bit 6 = 0 => 16-bit operation
    addi dptr, 2
movep_start:
    jal decode_ea                # now decode EA
    andi t1, opcode, 1<<7        # check direction bit
    bnez t1, movep_wmem
movep_wreg:
    lbu_m68k t0, eaptr           # read memory
    addiu opsize, -2
    sb t0, 0(dptr)               # write register
    lb t0, 2(eaptr)              
    sb t0, 1(dptr)
    beqz opsize, main_loop
    lb t0, 4(eaptr)              
    sb t0, 2(dptr)
    lb t0, 6(eaptr)              
    sb t0, 3(dptr)
    j main_loop
movep_wmem:
    lbu result, 0(dptr)          # read register
    addiu opsize, -2
    sb_m68k result, eaptr        # write memory
    lbu result, 1(dptr)
    sb result, 2(eaptr)
    beqz opsize, main_loop
    lbu result, 2(dptr)
    sb result, 4(eaptr)
    lbu result, 3(dptr)
    sb result, 6(eaptr)
    j main_loop

OP(exg):
    andi t0, opcode, 3<<6
    beqz t0, op_abcd
    jal decode_dptr
    li opsize, 4
    jal decode_ea
    andi t2, opcode, 0b11111000
    bne t2, 0b01001000, op_exg_start
    addi dptr, 8*4
op_exg_start:
    lw result, (dptr)
    lw t3, (eaptr)
    sw result, (eaptr)
    sw t3, (dptr)
    j main_loop

OP(tas):
    li opsize, 1                       # TAS is always 8-bit
    lax rmw_op, op_tas_rmwimpl
    jal_and_j decode_ea, rmw8_eadst    # decode the EA and run RMW
op_tas_rmwimpl:
    dsllv flag_zc, t1, rmw_bitsize     # affect Z and C flags
    sllv flag_nv, t1, rmw_bitsize      # affect N and V flags
    #if M64K_CONFIG_BROKEN_TAS
    andi t0, opcode, 3<<3              # check if EA mode is Dn
    bnez t0, main_loop                 # if not, avoid writeback (broken TAS)
    #endif
    ori result, t1, 0x80               # writeback value: set bit 7
    jr ra

op_unk:
# 68000+ instructions
op_stop: op_chk:
# 68010+ instructions
op_movec: op_bkpt: op_mvfccr: op_moves: op_rtd:
# 68020+ instructions
op_bftst: op_bfextu: op_bfchg: op_bfexts: op_bfclr: op_bfffo: op_bfset: op_bfins:
op_callm: op_cas: op_cas2: op_chk2: op_cmp2: op_cpbcc: op_cpdbcc: op_cpgen:
op_cprestore: op_cpsave: op_cpscc: op_cptrapcc: op_divul: op_divsl: op_extb:
op_pack: op_rtm: op_trapcc: op_unpk:
    move a0, opcode
    jal __m64k_assert_invalid_opcode
    addiu a1, m_pc, -2
