#ifndef M68KINLINE_H
#define M68KINLINE_H

#include <stdint.h>
#include <stdbool.h>
#include "roms.h"

#ifdef N64

// M68K memory handlers on N64 host.
//
// In general, m68k can only do 16-bit aligned memory accesses. 32-bit memory
// accesses are done through two subsequent 16-bit accesses, which means that
// in general 32-bit accesses are not 32-bit aligned (they're only 16-bit aligned).
//
// This means that we on N64 we can use a direct memory access for both 8-bit
// and 16-bit handlers, but for 32-bits we need to use unaligned accesses
// through the u_uint32_t datatype which generates LWL/LWR/SWL/SWR sequences.
//
// This also works for HWIO registers, where memory accesses will cause a TLB
// fault that will be handled by hw_n64.S to dispatch to the appropriate functions
// in hw.c. hw_n64.S will not have any trouble with 8-bit and 16-bit, and can
// also handle LWL/LWR by simply emulating the real access on LWL, and then
// treating LWR as a nop.
//
// We mark all accesses as volatile because we need the CPU core to actually
// make them the way they're defined without outsmarting us. This is important
// for HWIO accesses where the actual bus access size and order might matter.

// Typedefs for unaligned memory accesses
typedef uint32_t u_uint32_t __attribute__((aligned(1)));

static inline unsigned int  m68k_read_memory_8(unsigned int address) {
	return *(volatile uint8_t* restrict)(address);
}
static inline unsigned int  m68k_read_memory_16(unsigned int address) {
	return *(volatile uint16_t* restrict)(address);
}
static inline unsigned int  m68k_read_memory_32(unsigned int address) {
	return *(volatile u_uint32_t* restrict)(address);
}

static inline void m68k_write_memory_8(unsigned int address, unsigned int val) {
	*(volatile uint8_t* restrict)(address) = val;
}
static inline void m68k_write_memory_16(unsigned int address, unsigned int val) { 
	*(volatile uint16_t* restrict)(address) = val;	
}
static inline void m68k_write_memory_32(unsigned int address, unsigned int val) { 
	*(volatile u_uint32_t* restrict)(address) = val;
}

#endif

static inline bool m68k_check_idle_skip(unsigned int address) {
	return address == rom_pc_idle_skip;
}

#endif
