#ifndef M68K_RECOMPILER_H
#define M68K_RECOMPILER_H

/**
 * M68K AOT recompiler support.
 *
 * This file provides helper functions to recompiled code to do opcode decoding
 * and emulation.
 *
 * Recompiled code is made by roughly stitching interpreter functions together,
 * using their implementation coming from m68kops.c. Those functions rely on
 * helper functions and macros defined in m68kcpu.h. Unfortunately, Musashi
 * was designed to keep the 68K context in a global variable (m68ki_cpu), so
 * all those helpers directly access the global state.
 *
 * This creates a problem in the AOT recompiler, because the generated code
 * that accesses the global variable is not very efficient; loads and stores
 * cannot be reordered and coalesced because GCC assumes that they can be
 * modified at any time by external code (eg: all memory handlers), though we
 * know this does not happen.
 *
 * To obtain optimal code generation, the recompiled functions receive the
 * M68K context as a restricted pointer, so that GCC is able to perform its
 * magic like all M68K context was completely functional-local. To make this
 * work, though, we need to redefine all helper functions in m68kcpu.h to
 * go through this restricted pointer rather than going to the global context.
 * This is what this file mostly does.
 *
 * First, we include a stripped down version of m68kcpu.h (using the M68K_RECOMPILER
 * compile-time macro), that just brings in the main CPU structure definition.
 * Then, we reimplement most helper functions.
 *
 * Notice that we also redefine all the opcode reading functions (OPER_I_* and
 * m68k_read_imm_NN()). We don't want those helpers to go through emulated memory
 * accesses, because in the recompiler the whole opcode is a compile-time constant,
 * which is encoded in the OPARG[] array.
 */

#define M68K_RECOMPILER
#include "m68kcpu.h"

#undef REG_IR

#undef OPER_I_8
#undef OPER_I_16
#undef OPER_I_32
#undef EA_AY_DI_8
#undef EA_AX_DI_8
#undef EA_AW_8
#undef EA_AL_8
#undef EA_PCDI_8
#undef EA_PCIX_8
#undef EA_AY_IX_8
#undef EA_AX_IX_8
#undef EA_AY_DI_16
#undef EA_AY_DI_32
#undef EA_AY_IX_16
#undef EA_AY_IX_32
#undef EA_AX_DI_16
#undef EA_AX_DI_32
#undef EA_AX_IX_16
#undef EA_AX_IX_32
#undef EA_PCDI_16
#undef EA_PCDI_32
#undef EA_PCIX_16
#undef EA_PCIX_32

#define OPER_I_16()    ({ REG_PC += 2; OPARG[OPARGIDX++]; })
#define OPER_I_8()     ({ OPER_I_16() & 0xFF; })
#define OPER_I_32()    ({ unsigned int x = OPER_I_16() << 16; x |= OPER_I_16(); x; })
#define EA_AY_DI_8()   (AY+MAKE_INT_16(OPER_I_16()))
#define EA_AX_DI_8()   (AX+MAKE_INT_16(OPER_I_16()))
#define EA_AW_8()      MAKE_INT_16(OPER_I_16())
#define EA_AL_8()      OPER_I_32()

#define m68ki_get_ea_ix(_ARG)   _Static_assert(0)  // should not happen -- all calls should go through hle_m68ki_get_ea_ix

#define hle_m68ki_get_ea_ix(_AN, _EXT) ({ \
	uint An=_AN; uint extension=_EXT; uint Xn = 0; \
	Xn = REG_DA[extension>>12]; \
	if(!BIT_B(extension)) \
		Xn = MAKE_INT_16(Xn); \
	An + Xn + MAKE_INT_8(extension); \
})

#define m68ki_get_ea_pcdi()  ({ uint pc = REG_PC; pc + MAKE_INT_16(OPER_I_16()); })
#define m68ki_get_ea_pcix()  ({ uint pc = REG_PC; hle_m68ki_get_ea_ix(pc, OPER_I_16()); })

#define EA_PCDI_8()    m68ki_get_ea_pcdi()                   /* pc indirect + displacement */
#define EA_PCIX_8()    m68ki_get_ea_pcix()                   /* pc indirect + index */

#define EA_AY_IX_8()   hle_m68ki_get_ea_ix(AY, OPER_I_16())                   /* indirect + index */
#define EA_AX_IX_8()   hle_m68ki_get_ea_ix(AX, OPER_I_16())


#define EA_AY_DI_16()  EA_AY_DI_8()
#define EA_AY_DI_32()  EA_AY_DI_8()
#define EA_AY_IX_16()  EA_AY_IX_8()
#define EA_AY_IX_32()  EA_AY_IX_8()

#define EA_AX_DI_16()  EA_AX_DI_8()
#define EA_AX_DI_32()  EA_AX_DI_8()
#define EA_AX_IX_16()  EA_AX_IX_8()
#define EA_AX_IX_32()  EA_AX_IX_8()

#define EA_PCDI_16()   EA_PCDI_8()
#define EA_PCDI_32()   EA_PCDI_8()
#define EA_PCIX_16()   EA_PCIX_8()
#define EA_PCIX_32()   EA_PCIX_8()

#define OPER_AY_AI_8()  ({uint ea = EA_AY_AI_8();   m68ki_read_8(ea); })
#define OPER_AY_AI_16() ({uint ea = EA_AY_AI_16();  m68ki_read_16(ea);})
#define OPER_AY_AI_32() ({uint ea = EA_AY_AI_32();  m68ki_read_32(ea);})
#define OPER_AY_PI_8()  ({uint ea = EA_AY_PI_8();   m68ki_read_8(ea); })
#define OPER_AY_PI_16() ({uint ea = EA_AY_PI_16();  m68ki_read_16(ea);})
#define OPER_AY_PI_32() ({uint ea = EA_AY_PI_32();  m68ki_read_32(ea);})
#define OPER_AY_PD_8()  ({uint ea = EA_AY_PD_8();   m68ki_read_8(ea); })
#define OPER_AY_PD_16() ({uint ea = EA_AY_PD_16();  m68ki_read_16(ea);})
#define OPER_AY_PD_32() ({uint ea = EA_AY_PD_32();  m68ki_read_32(ea);})
#define OPER_AY_DI_8()  ({uint ea = EA_AY_DI_8();   m68ki_read_8(ea); })
#define OPER_AY_DI_16() ({uint ea = EA_AY_DI_16();  m68ki_read_16(ea);})
#define OPER_AY_DI_32() ({uint ea = EA_AY_DI_32();  m68ki_read_32(ea);})
#define OPER_AY_IX_8()  ({uint ea = EA_AY_IX_8();   m68ki_read_8(ea); })
#define OPER_AY_IX_16() ({uint ea = EA_AY_IX_16();  m68ki_read_16(ea);})
#define OPER_AY_IX_32() ({uint ea = EA_AY_IX_32();  m68ki_read_32(ea);})

#define OPER_AX_AI_8()  ({uint ea = EA_AX_AI_8();   m68ki_read_8(ea); })
#define OPER_AX_AI_16() ({uint ea = EA_AX_AI_16();  m68ki_read_16(ea);})
#define OPER_AX_AI_32() ({uint ea = EA_AX_AI_32();  m68ki_read_32(ea);})
#define OPER_AX_PI_8()  ({uint ea = EA_AX_PI_8();   m68ki_read_8(ea); })
#define OPER_AX_PI_16() ({uint ea = EA_AX_PI_16();  m68ki_read_16(ea);})
#define OPER_AX_PI_32() ({uint ea = EA_AX_PI_32();  m68ki_read_32(ea);})
#define OPER_AX_PD_8()  ({uint ea = EA_AX_PD_8();   m68ki_read_8(ea); })
#define OPER_AX_PD_16() ({uint ea = EA_AX_PD_16();  m68ki_read_16(ea);})
#define OPER_AX_PD_32() ({uint ea = EA_AX_PD_32();  m68ki_read_32(ea);})
#define OPER_AX_DI_8()  ({uint ea = EA_AX_DI_8();   m68ki_read_8(ea); })
#define OPER_AX_DI_16() ({uint ea = EA_AX_DI_16();  m68ki_read_16(ea);})
#define OPER_AX_DI_32() ({uint ea = EA_AX_DI_32();  m68ki_read_32(ea);})
#define OPER_AX_IX_8()  ({uint ea = EA_AX_IX_8();   m68ki_read_8(ea); })
#define OPER_AX_IX_16() ({uint ea = EA_AX_IX_16();  m68ki_read_16(ea);})
#define OPER_AX_IX_32() ({uint ea = EA_AX_IX_32();  m68ki_read_32(ea);})

#define OPER_A7_PI_8()  ({uint ea = EA_A7_PI_8();   m68ki_read_8(ea); })
#define OPER_A7_PD_8()  ({uint ea = EA_A7_PD_8();   m68ki_read_8(ea); })

#define OPER_AW_8()     ({uint ea = EA_AW_8();      m68ki_read_8(ea); })
#define OPER_AW_16()    ({uint ea = EA_AW_16();     m68ki_read_16(ea);})
#define OPER_AW_32()    ({uint ea = EA_AW_32();     m68ki_read_32(ea);})
#define OPER_AL_8()     ({uint ea = EA_AL_8();      m68ki_read_8(ea); })
#define OPER_AL_16()    ({uint ea = EA_AL_16();     m68ki_read_16(ea);})
#define OPER_AL_32()    ({uint ea = EA_AL_32();     m68ki_read_32(ea);})
#define OPER_PCDI_8()   ({uint ea = EA_PCDI_8();    m68ki_read_pcrel_8(ea); })
#define OPER_PCDI_16()  ({uint ea = EA_PCDI_16();   m68ki_read_pcrel_16(ea);})
#define OPER_PCDI_32()  ({uint ea = EA_PCDI_32();   m68ki_read_pcrel_32(ea);})
#define OPER_PCIX_8()   ({uint ea = EA_PCIX_8();    m68ki_read_pcrel_8(ea); })
#define OPER_PCIX_16()  ({uint ea = EA_PCIX_16();   m68ki_read_pcrel_16(ea);})
#define OPER_PCIX_32()  ({uint ea = EA_PCIX_32();   m68ki_read_pcrel_32(ea);})

#define m68ki_jump(pc)  ({ REG_PC = pc; })

#define m68ki_cpu (*__m68ki_cpu)
#define m68ki_remaining_cycles (*__m68ki_remaining_cycles)

#define m68ki_push_16(value) ({ \
	REG_SP = MASK_OUT_ABOVE_32(REG_SP - 2); \
	m68ki_write_16(REG_SP, value); \
})

#define m68ki_push_32(value) ({ \
	REG_SP = MASK_OUT_ABOVE_32(REG_SP - 4); \
	m68ki_write_32(REG_SP, value); \
})

#define m68ki_pull_16() ({ \
	REG_SP = MASK_OUT_ABOVE_32(REG_SP + 2); \
	m68ki_read_16(REG_SP-2); \
})

#define m68ki_pull_32() ({ \
	REG_SP = MASK_OUT_ABOVE_32(REG_SP + 4); \
	m68ki_read_32(REG_SP-4); \
})

#define m68ki_set_ccr(_VAL) ({ \
	uint value = _VAL; \
	FLAG_X = BIT_4(value)  << 4; \
	FLAG_N = BIT_3(value)  << 4; \
	FLAG_Z = !BIT_2(value); \
	FLAG_V = BIT_1(value)  << 6; \
	FLAG_C = BIT_0(value)  << 8; \
})

#endif
