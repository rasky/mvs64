
#include "regs.S"

#define C0_BADVADDR	       $8
#define C0_CAUSE_TLBSTORE   3

.macro PROFILE_START
	mfc0 k0, C0_COUNT
	lw k1, hw_io_profile
	sub k1, k0
	la k0, hw_io_profile
	sw k1, 0(k0)
.endm

.macro PROFILE_END
	mfc0 k0, C0_COUNT
	lw k1, hw_io_profile
	add k1, k0
	la k0, hw_io_profile
	sw k1, 0(k0)
.endm

	.set noreorder
	.set noat

mvs_tlbhandler:
	PROFILE_START
	# Check whether the faulting address is in one of these ranges:
	#  * 0xFF2nnnnn: PBROM area (banked)
	#  * 0xFF3nnnnn: hardware register of MVS
	#  * 0xFF4nnnnn: palette RAM (go through handlers for conversion to RGB32)
	mfc0 k1, C0_BADVADDR
	sll k1, 8
	srl k1, 8
	srl k0, k1, 20
	addi k0, -0x2
	beqz k0, tlb_start
	addi k0, -1
	beqz k0, tlb_start
	addi k0, -1
	beqz k0, tlb_start
	nop

	# Otherwise, go back to the standard handler (that will crash and show exception on screen)
	j tlbfallback
	nop

tlb_start:
	# Check if the exception happened in a delay slot. If so, change EPC
	# so that it points to the delay slot itself, so that the following code
	# can refer to EPC as the faulting memory read/write.
	mfc0 k0, C0_CAUSE
	bgez k0, tlbcallhandler    # Bit 31: BD (exception in delay slot)
	mfc0 k0, C0_EPC
	addi k0, 4
	mtc0 k0, C0_EPC

tlbcallhandler:
	# Save all caller-saved registers
	addi sp, -26*8
	# Keep slots 0-3 empty as argument slots for function calls.
	# This is required by MIPS ABI even if we only call functions
	# that receive arguments in registers.
	sd $1, 4*8(sp)
	sd $2, 5*8(sp)
	sd $3, 6*8(sp)
	sd $4, 7*8(sp)
	sd $5, 8*8(sp)
	sd $6, 9*8(sp)
	sd $7, 10*8(sp)
	sd $8, 11*8(sp)
	sd $9, 12*8(sp)
	sd $10, 13*8(sp)
	sd $11, 14*8(sp)
	sd $12, 15*8(sp)
	sd $13, 16*8(sp)
	sd $14, 17*8(sp)
	sd $15, 18*8(sp)
	sd $24, 19*8(sp)
	sd $25, 20*8(sp)
	sd $31, 21*8(sp)
	.set at          # we can use assembly macros now to simplify our job

	mfc0 t0, C0_EPC
	nop
	sd t0, 22*8(sp)

	mfc0 t1, C0_SR
	nop
	sd t1, 23*8(sp)

	mflo t0
	mfhi t1
	sd t0, 24*8(sp)
	sd t1, 25*8(sp)

	# Disable FPU & EXL. To speed up TLB handler, we don't save
	# FPU registers context, so we expect memory handlers not to
	# touch FPU. If they do for a bug, a Coprocessor Unusable exception
	# will trigger. Also disable EXL to allow exception-in-exception
	# processing, which should be safe at this point.
	li t0, ~((0<<29)|(0<<1))   # FIXME: disabled for now
	and t1, t0
	mtc0 t1, C0_SR

	# Decode faulting instruction at EPC. We get the memory address in C0_BADVADDR,
	# but we need to decode the RS register because it contains the actual value
	# being read/written. Also, the opcode itself encodes the access width (eg:
	# LH = 2 bytes).
	mfc0 t1, C0_EPC
	nop
	nop
	lw t1, 0(t1)

	srl t0, t1, 26
	beq t0, 0x23, tlb_readhwio   # LW
	li a1, 4
	beq t0, 0x25, tlb_readhwio   # LHU
	li a1, 2
	beq t0, 0x24, tlb_readhwio   # LBU
	li a1, 1
	beq t0, 0x22, tlb_readhwio   # LWL (treat as LW)
	li a1, 4
	beq t0, 0x26, tlb_restore    # LWR (ignore, we emulate on LWL)
	li a1, 4

	beq t0, 0x2B, tlb_writehwio  # SW
	li a2, 4
	beq t0, 0x29, tlb_writehwio  # SH
	li a2, 2
	beq t0, 0x28, tlb_writehwio  # SB
	li a2, 1

	beq t0, 0x2A, tlb_writehwio  # SWL (treat as SW)
	li a2, 4
	beq t0, 0x2E, tlb_restore    # SWR (ignore, we emulate on SWL)
	li a2, 4

	# Unsupported read/write instruction. Go to the fallback handler that will
	# crash on the screen.
	# FIXME: we already poisoned registers and stack!
	j tlbfallback
	nop


	##########################################
	# Memory-mapped registers: READS
	#
	##########################################

tlb_readhwio:
	# We're ready to call the memory handler. Use read_hwio for HWIO reads,
	# and video_palette_r for palette reads.
	mfc0 a0, C0_BADVADDR
	sll a0, 8
	srl a0, 8
	srl t0, a0, 20
	beq t0, 4, tlb_readhwio_palette
	nop
	beq t0, 3, tlb_readhwio_hwio
	nop

	la ra, tlb_readhwio_epilog
	j read_pbrom
	nop

tlb_readhwio_hwio:
	la ra, tlb_readhwio_epilog
	j read_hwio
	nop

tlb_readhwio_palette:
	la ra, tlb_readhwio_epilog
	j video_palette_r
	nop

tlb_readhwio_epilog:
	# The memory handler returned in v0 the value that needs to be stored in
	# the target MIPS register of the faulting instruction. Decode the RS field
	# of the opcode and use a jump table to copy V0 into the correct register.
	# For caller-saved registers, we insert the value directly into the stack
	# frame.
	mfc0 t1, C0_EPC
	lw t1, 0(t1)		
	srl t0, t1, 16-3
	andi t0, 0x1F<<3
	la t2, tlb_readhwio_writedest
	add t0, t2
	jr t0
	nop
tlb_readhwio_writedest:
	j tlb_readhwio_end
	nop  # $0
	j tlb_readhwio_end
	sd v0, 4*8(sp)
	j tlb_readhwio_end
	sd v0, 5*8(sp)
	j tlb_readhwio_end
	sd v0, 6*8(sp)
	j tlb_readhwio_end
	sd v0, 7*8(sp)
	j tlb_readhwio_end
	sd v0, 8*8(sp)
	j tlb_readhwio_end
	sd v0, 9*8(sp)
	j tlb_readhwio_end
	sd v0, 10*8(sp)
	j tlb_readhwio_end
	sd v0, 11*8(sp)
	j tlb_readhwio_end
	sd v0, 12*8(sp)
	j tlb_readhwio_end
	sd v0, 13*8(sp)
	j tlb_readhwio_end
	sd v0, 14*8(sp)
	j tlb_readhwio_end
	sd v0, 15*8(sp)
	j tlb_readhwio_end
	sd v0, 16*8(sp)
	j tlb_readhwio_end
	sd v0, 17*8(sp)
	j tlb_readhwio_end
	sd v0, 18*8(sp)
	j tlb_readhwio_end
	move $16, v0
	j tlb_readhwio_end
	move $17, v0
	j tlb_readhwio_end
	move $18, v0
	j tlb_readhwio_end
	move $19, v0
	j tlb_readhwio_end
	move $20, v0
	j tlb_readhwio_end
	move $21, v0
	j tlb_readhwio_end
	move $22, v0
	j tlb_readhwio_end
	move $23, v0
	j tlb_readhwio_end
	sd v0, 19*8(sp)
	j tlb_readhwio_end
	sd v0, 20*8(sp)
	j tlb_readhwio_end
	move $26, v0
	j tlb_readhwio_end
	move $27, v0
	j tlb_readhwio_end
	move $28, v0
	j tlb_readhwio_end
	move $29, v0
	j tlb_readhwio_end
	move $30, v0
	j tlb_readhwio_end
	sd v0, 21*8(sp)

tlb_readhwio_end:
	# OK we're done. Exit
	j tlb_restore
	nop

	##########################################
	# Memory-mapped registers: WRITES
	#
	##########################################

tlb_writehwio:
	# We need to decode the RS field of the faulting instruction to read the
	# register that contains the value that needs to be written into the
	# emulating memory-mapped register. We use a jump table to load the correct
	# value into a1. For caller-saved registers, we use the value saved in the
	# stack frame (in case we've already modified it).
	srl t0, t1, 16-3
	andi t0, 0x1F<<3
	la t2, tlb_writehwio_fetchsrc
	add t0, t2
	jr t0
	nop
tlb_writehwio_fetchsrc:
	j tlb_writehwio_do
	move a1, $0
	j tlb_writehwio_do
	ld a1, 4*8(sp)
	j tlb_writehwio_do
	ld a1, 5*8(sp)
	j tlb_writehwio_do
	ld a1, 6*8(sp)
	j tlb_writehwio_do
	ld a1, 7*8(sp)
	j tlb_writehwio_do
	ld a1, 8*8(sp)
	j tlb_writehwio_do
	ld a1, 9*8(sp)
	j tlb_writehwio_do
	ld a1, 10*8(sp)
	j tlb_writehwio_do
	ld a1, 11*8(sp)
	j tlb_writehwio_do
	ld a1, 12*8(sp)
	j tlb_writehwio_do
	ld a1, 13*8(sp)
	j tlb_writehwio_do
	ld a1, 14*8(sp)
	j tlb_writehwio_do
	ld a1, 15*8(sp)
	j tlb_writehwio_do
	ld a1, 16*8(sp)
	j tlb_writehwio_do
	ld a1, 17*8(sp)
	j tlb_writehwio_do
	ld a1, 18*8(sp)
	j tlb_writehwio_do
	move a1, $16
	j tlb_writehwio_do
	move a1, $17
	j tlb_writehwio_do
	move a1, $18
	j tlb_writehwio_do
	move a1, $19
	j tlb_writehwio_do
	move a1, $20
	j tlb_writehwio_do
	move a1, $21
	j tlb_writehwio_do
	move a1, $22
	j tlb_writehwio_do
	move a1, $23
	j tlb_writehwio_do
	ld a1, 19*8(sp)
	j tlb_writehwio_do
	ld a1, 20*8(sp)
	j tlb_writehwio_do
	move a1, $26
	j tlb_writehwio_do
	move a1, $27
	j tlb_writehwio_do
	move a1, $28
	j tlb_writehwio_do
	move a1, $29
	j tlb_writehwio_do
	move a1, $30
	j tlb_writehwio_do
	ld a1, 21*8(sp)

tlb_writehwio_do:
	# Call the memory handler. write_hwio for HWIO, and video_palette_w for
	# palette RAM.
	mfc0 a0, C0_BADVADDR
	sll a0, 8
	srl a0, 8
	srl t0, a0, 20
	beq t0, 4, tlb_writehwio_palette
	nop
	beq t0, 3, tlb_writehwio_hwio
	nop

	la ra, tlb_restore
	j write_pbrom
	nop

tlb_writehwio_hwio:
	la ra, tlb_restore
	j write_hwio
	nop

tlb_writehwio_palette:
	la ra, tlb_restore
	j video_palette_w
	nop


	##########################################
	# Memory-mapped registers: EXIT
	#
	##########################################

tlb_restore:
	# Restore the stack frame
	ld t0, 23*8(sp)
	ld t1, 22*8(sp)
	mtc0 t0, C0_SR
	mtc0 t1, C0_EPC

	ld t0, 24*8(sp)
	ld t1, 25*8(sp)
	mtlo t0
	mthi t1

	.set noat    # no more assembly macros from now on
	ld $1, 4*8(sp)
	ld $2, 5*8(sp)
	ld $3, 6*8(sp)
	ld $4, 7*8(sp)
	ld $5, 8*8(sp)
	ld $6, 9*8(sp)
	ld $7, 10*8(sp)
	ld $8, 11*8(sp)
	ld $9, 12*8(sp)
	ld $10, 13*8(sp)
	ld $11, 14*8(sp)
	ld $12, 15*8(sp)
	ld $13, 16*8(sp)
	ld $14, 17*8(sp)
	ld $15, 18*8(sp)
	ld $24, 19*8(sp)
	ld $25, 20*8(sp)
	ld $31, 21*8(sp)
	addiu sp, 26*8

tlbdone:
	# If the exception did not happen in the delay slot, we can simply
	# add 4 to EPC to skip the faulting instruction, and exit the exception.
	mfc0 k1, C0_CAUSE
	bltz k1, tlb_done_delayslot
	mfc0 k0, C0_EPC
	nop
	addi k0, 4
	mtc0 k0, C0_EPC
	nop  # 2 hazards before eret
	nop
	PROFILE_END
	eret

tlb_done_delayslot:
	# The faulting instruction was in a delay slot. Since we didn't map anything
	# but just simulate the read/write, we need to jump to the branch destination
	# now. There are three kind of branch instructions:
	#   * "branches" with 16-bit signed offset (scaled by 4)
	#   * "jumps" with 26-bit absolute offset (scaled by 4)
	#   * "indirect jumps" with a register pointing to the jump target
	lw k0, -4(k0)
	srl k1, k0, 26
	beqz k1, tlb_done_delayslot_indirect   # opcode 0: SPECIAL
	addi k1, -2
	beqz k1, tlb_done_delayslot_jump26     # opcode 2: J
	addi k1, -1
	beqz k1, tlb_done_delayslot_jump26     # opcode 3: JAL
	nop

	# Fallback: everything else is a 16-bit branch. There are so many
	# of them so it doesn't make sense to match them.
tlb_done_delayslot_branch16:
	# Decode target (signed 16-bit, to be multiplied by 4)
	sll k1, k0, 16
	sra k1, 14
	mfc0 k0, C0_EPC
	nop
	add k0, k1
	mtc0 k0, C0_EPC
	nop  # 2 hazards before eret
	nop
	PROFILE_END
	eret

tlb_done_delayslot_jump26:
	# The 26 bit offset is left-shifted by 2, and placed into
	# EPC, keeping the highest 6 bits (the region).
	sll k1, k0, 6
	srl k1, 6-2
	mfc0 k0, C0_EPC
	nop
	srl k0, 26
	sll k0, 26
	or k0, k1
	mtc0 k0, C0_EPC
	nop  # 2 hazards before eret
	nop
	PROFILE_END
	eret

tlb_done_delayslot_indirect:
	# TODO: not implemented. We just checked that this is a SPECIAL opcode
	# (so it should be either JR or JALR). We only implment JR RA for now.
	li k1, 0x03e00008
	beq k0, k1, tlb_done_delayslot_jr_ra
	nop
	li k1, 0x01e0f809
	beq k0, k1, tlb_done_delayslot_jalr_t7
	nop
	j tlbfallback
	nop

tlb_done_delayslot_jalr_t7:
	mtc0 t7, C0_EPC
	nop  # 2 hazards before eret
	nop
	PROFILE_END
	eret

tlb_done_delayslot_jr_ra:
	mtc0 ra, C0_EPC
	nop  # 2 hazards before eret
	nop
	PROFILE_END
	eret


tlbfallback:
	mfc0 k0, C0_SR
	li k1, (1<<29)|(1<<1)
	or k0, k1
	mtc0 k0, C0_SR

	j inthandler
	nop


mvs_inthandler:
	j inthandler
	nop


	.global mvs_intvector
mvs_intvector:
	la k1, mvs_inthandler
	jr k1
	nop

	.global mvs_tlbvector
mvs_tlbvector:
	la k1, mvs_tlbhandler
	jr k1
	nop
