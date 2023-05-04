
#include "regs.S"

#define C0_BADVADDR	       $8
#define C0_CAUSE_TLBSTORE   3

#define SAFE_MODE           1


.macro PROFILE_START
	mfc0 k0, C0_COUNT
	lw k1, profile_hw_io
	subu k1, k0
	la k0, profile_hw_io
	sw k1, 0(k0)
.endm

.macro PROFILE_END
	mfc0 k0, C0_COUNT
	lw k1, profile_hw_io
	addu k1, k0
	la k0, profile_hw_io
	sw k1, 0(k0)
.endm

	.section .sdata

	.set noreorder
	.set noat
	.text
mvs_tlbhandler:
	PROFILE_START
	# Check whether the faulting address is at address 0xFFxxxxxx, which is
	# where the m68k memory map is TLB-mapped. Otherwise, go to the standard
	# exception handler.
	mfc0 k1, C0_BADVADDR
	srl k0, k1, 24
	xori k0, 0xFF
	bnez k0, tlbfallback
	 mfc0 k0, C0_CAUSE

	# Clean MSB of address, which is not used by m68k.
	sll k1, 8
	srl k1, 8

	# Check if the exception happened in a delay slot. If so, change EPC
	# so that it points to the delay slot itself, so that the following code
	# can refer to EPC as the faulting memory read/write.
	bgez k0, tlbcallhandler    # Bit 31: BD (exception in delay slot)
	 mfc0 k0, C0_EPC
	addiu k0, 4
	mtc0 k0, C0_EPC

tlbcallhandler:
	# k1 = TLB address
	# k0 = EPC (branch-delay corrected)
	# Fetch opcode at EPC.
	lw k0, 0(k0)

	# Setup stack frame. Make room for all the registers we might need
	addiu sp, -26*8

	# Keep slots 0-3 empty as argument slots for function calls.
	# This is required by MIPS ABI even if we only call functions
	# that receive arguments in registers.
	sd $1,   4*8(sp)   # at
	sd $14, 17*8(sp)   # t6 (all stores by m64k are made with this register)
	sd $31, 21*8(sp)   # ra (this allows backtracing to work corretly across TLB miss exceptions)
	.set at            # we can use assembly macros now to simplify our job

	# Check whether the opcode is SWL or SWR. These are used by m64k to
	# write 32-bit values to memory. A real m68k has a 16-bit data bus, so
	# 32-bit writes are actually made by two 16-bit writes. To simulate this,
	# we emulate "SWL" as the high 16-bit write, and "SWR" as the low 16-bit
	# write. We can do this because m64k always issue these in pairs, on
	# addresses which are always 16-bit aligned.
	srl k0, 26                       # extract opcode
	andi k0, 0xF
	beq k0, 0xE, tlb_asmwrite_swr
	 nop
	bgt k0, 0xA, tlbfallback
	 sll k0, 5
	.set noat
	la $1, tlb_asmread_lb
	add k0, $1
	.set at
	jr k0
	 nop

	.balign 32
tlb_asmread_lb:
	jal mvs64_asm_io_read8
	 nop
	sll t0, t6, 24
	j tlb_restore_asm
 	 sra t0, 24

	.balign 32
tlb_asmread_lh:
	jal mvs64_asm_io_read16
	 nop
	sll t0, t6, 16
	j tlb_restore_asm
 	 sra t0, 16

	.balign 32
tlb_asmread_lwl:
	jal mvs64_asm_io_read16
	 nop
 	sll t6, 16
	andi t0, 0xFFFF
	j tlb_restore_asm
	 or t0, t6

	.balign 32
tlb_asmread_lw:
	j tlbfallback  # not implemented
	nop

	.balign 32
tlb_asmread_lbu:
	jal mvs64_asm_io_read8
	 nop
	j tlb_restore_asm
 	 and t0, t6, 0xFF

	.balign 32
tlb_asmread_lhu:
	jal mvs64_asm_io_read16
	 nop
	j tlb_restore_asm
 	 and t0, t6, 0xFFFF

	.balign 32
tlb_asmread_lwr:
	jal mvs64_asm_io_read16
	 addiu k1, -1
 	and t6, 0xFFFF
	dsrl t0, 16
	dsll t0, 16
	j tlb_restore_asm
	 or t0, t6

	.balign 32
tlb_asmread_lwu:
	j tlbfallback  # not implemented
	nop

	.balign 32
tlb_asmwrite_sb:
	andi t6, 0xFF
	sll k0, t6, 8                    # duplicate 8-bit value two times in register
	or t6, k0
	la ra, tlb_restore_asm
	j mvs64_asm_io_write
	 li k0, 0

	.balign 32
tlb_asmwrite_sh:
	la ra, tlb_restore_asm
	j mvs64_asm_io_write
	 li k0, 1

	.balign 32
tlb_asmwrite_swl:
	srl t6, 16
	la ra, tlb_restore_asm
	j mvs64_asm_io_write
	 li k0, 1

	.balign 32
tlb_asmwrite_swr:
	addiu k1, -1
	la ra, tlb_restore_asm
	j mvs64_asm_io_write
	 li k0, 1

tlbcallhandler_standard:
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
	sd $15, 18*8(sp)
	sd $24, 19*8(sp)
	sd $25, 20*8(sp)

	ld t6, 17*8(sp)   # restore t6, which was modified by the fasthandler code

	mfc0 t0, C0_EPC
	nop
	sd t0, 22*8(sp)

	mflo t0
	mfhi t1
	sd t0, 24*8(sp)
	sd t1, 25*8(sp)

	mfc0 t1, C0_SR
	nop
	sd t1, 23*8(sp)

	# Disable FPU & EXL. To speed up TLB handler, we don't save
	# FPU registers context, so we expect memory handlers not to
	# touch FPU. If they do for a bug, a Coprocessor Unusable exception
	# will trigger. Also disable EXL to allow exception-in-exception
	# processing, which should be safe at this point.
	# FIXME: disabled for now
	and t1, ~(SR_IE | SR_EXL | SR_CU1)
	mtc0 t1, C0_SR

	# WARNING: after clearing the EXL bit, it is now possible to trigger
	# reentrant exceptions (and not only crashing ones).
	# Avoid using k0/k1 from now on,
	# as they would get corrupted by a reentrant exception.
#undef k0
#undef k1

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
	#if SAFE_MODE
	# Verify that the memory read went through the t0 register
	srl t0, t1, 16
	andi t0, 0x1F
	bne t0, 8, tlbfallback
	 nop
	#endif

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
	beq t0, 2, tlb_readhwio_pbrom
	nop
	j tlbfallback
	nop

tlb_readhwio_pbrom:
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
	# The memory handler returned in v0 the value. The m64k core always does
	# stores via "t0" ($8) so we can store in its stack slot the result
	sd v0, 11*8(sp)

tlb_readhwio_end:
	# OK we're done. Exit
	j tlb_restore
	nop

	##########################################
	# Memory-mapped registers: WRITES
	#
	##########################################

tlb_writehwio:
	#if SAFE_MODE
	# Verify that the memory read went through the t6 register
	srl t0, t1, 16
	andi t0, 0x1F
	bne t0, 14, tlbfallback
	 nop
	#endif

	# Read value of t6 register as saved onto the stack
	ld a1, 17*8(sp)

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

# Now that we're back to standard EXL exception context, k0/k1 can be reused
#define k0 $26
#define k1 $27

	ld t0, 24*8(sp)
	ld t1, 25*8(sp)
	mtlo t0
	mthi t1

	.set noat    # no more assembly macros from now on
	ld $25, 20*8(sp)
	ld $24, 19*8(sp)
	ld $15, 18*8(sp)
	ld $13, 16*8(sp)
	ld $12, 15*8(sp)
	ld $11, 14*8(sp)
	ld $10, 13*8(sp)
	ld $9, 12*8(sp)
	ld $8, 11*8(sp)
	ld $7, 10*8(sp)
	ld $6, 9*8(sp)
	ld $5, 8*8(sp)
	ld $4, 7*8(sp)
	ld $3, 6*8(sp)
	ld $2, 5*8(sp)

tlb_restore_asm:
	ld $31, 21*8(sp)
	ld $14, 17*8(sp)
	ld $1, 4*8(sp)
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
	addu k0, k1
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
	li k1, (SR_IE | SR_EXL | SR_CU1)
	or k0, k1
	mtc0 k0, C0_SR

	j inthandler
	nop


mvs_inthandler:
	mfc0 k0, C0_CAUSE
	andi k0, 0xFF
	xori k0, 1<<2        # TLB modification
	beqz k0, mvs_tlbhandler
	 nop
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

#define value     t6

	.set at
	.set reorder

# m64k assembly I/O handlers
# Input:
#   k0 = 0 if 8-bit access, 1 if 16-bit access
#   k1 = 24-bit m68k address accessed
# Output:
#   k0 = 1 if handled, 0 if not handled (fallback to C handler)
# For writes:
#   t6 = value being written
# For reads:
#   returned value must go into t6 (8 or 16 bit)

mvs64_asm_io_read8:
	li k0, 0
	beq k1, 0x2FFFEF, asm_pbrom_bankno_r8
	blt k1, 0x300000, asm_pbrom_r
	j tlbcallhandler_standard

mvs64_asm_io_read16:
	li k0, 1
	blt k1, 0x300000, asm_pbrom_r
	j tlbcallhandler_standard

mvs64_asm_io_write:
	beq k1, 0x300001, asm_watchdog_w
	beq k1, 0x3C0000, asm_lspc_vram_addr_w
	beq k1, 0x3C0002, asm_lspc_vram_data_w
	beq k1, 0x3C0004, asm_lspc_vram_modulo_w
	bge k1, 0x400000, asm_video_palette_w
	j tlbcallhandler_standard

asm_watchdog_w:
	li k0, 1
	sb k0, %gprel(watchdog_kicked)(gp)
	jr ra

asm_lspc_vram_addr_w:
	andi k0, value, 0x8000
	bnez k0, 1f
	la k0, VIDEO_RAM
	sw k0, %gprel(reg_vram_bank)(gp)
	li k0, 0x7FFF
	sh k0, %gprel(reg_vram_mask)(gp)
	j 2f
1:
	la k0, VIDEO_RAM + 0x8000*2
	sw k0, %gprel(reg_vram_bank)(gp)
	li k0, 0x7FF
	sh k0, %gprel(reg_vram_mask)(gp)
2:
	and k0, value
	sh k0, %gprel(reg_vram_addr)(gp)
	jr ra

asm_lspc_vram_data_w:
	lhu k0, %gprel(reg_vram_addr)(gp)
	lw k1, %gprel(reg_vram_bank)(gp)
	addu k1, k0
	addu k1, k0
	sh value, 0(k1)
	lhu k1, %gprel(reg_vram_mod)(gp)
	addu k0, k1
	lhu k1, %gprel(reg_vram_mask)(gp)
	and k0, k1
	sh k0, %gprel(reg_vram_addr)(gp)
	jr ra

asm_lspc_vram_modulo_w:
	sh value, %gprel(reg_vram_mod)(gp)
	jr ra

asm_video_palette_w:
	# Palette RAM is a 16-bit RAM that ignores the UDS/IDS signals,
	# so 8-bit writes are treated as 16-bit writes. NOTE: the 8-bit
	# in "value" has been already duplicated, so we can store it as-is.
	andi k1, 0x1FFE
	lw k0, PALETTE_RAM_BANK
	addu k1, k0
	addu k1, k0
	la k0, PALETTE_RAM
	addu k1, k0
	sh value, 0(k1)
	jr ra

asm_pbrom_bankno_r8:
	lw t6, %gprel(pbrom_bank)(gp)
	srl t6, 20
	jr ra

asm_pbrom_r:
	.set noat
	sll t6, k1, 12
	srl t6, 12
	lw $1, %gprel(pbrom_bank)(gp)
	or t6, $1
	srl t6, 6
	lw $1, %gprel(pbrom_last_bank)(gp)
	bne t6, $1, tlbcallhandler_standard
	lw $1, %gprel(pbrom_last_mem)(gp)
	andi k1, (1<<6)-1
	addu k1, $1
	.set at
	beqz k0, asm_pbrom_r8
	lhu t6, 0(k1)
	jr ra
asm_pbrom_r8:
	lbu t6, 0(k1)
	jr ra


