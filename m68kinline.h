#ifndef M68KINLINE_H
#define M68KINLINE_H

#include <stdint.h>

#ifndef N64

extern uint8_t *pc_fastptr_bank;

#ifdef N64
	#define BE16(x)  (x)
	#define BE32(x)  (x)
#else
	#define BE16(x)  __builtin_bswap16(x)
	#define BE32(x)  __builtin_bswap32(x)
#endif

static inline unsigned int  m68k_read_immediate_16(unsigned int address) {
	typedef uint16_t u_uint16_t __attribute__((aligned(1)));
	return BE16(*(u_uint16_t*)(pc_fastptr_bank + (address&0xFFFFF)));
}

static inline unsigned int  m68k_read_immediate_32(unsigned int address) {
	typedef uint32_t u_uint32_t __attribute__((aligned(1)));
	return BE32(*(u_uint32_t*)(pc_fastptr_bank + (address&0xFFFFF)));
}

static inline unsigned int  m68k_read_pcrelative_8(unsigned int address) { 
	return *(pc_fastptr_bank + (address&0xFFFFF));
}

static inline unsigned int  m68k_read_pcrelative_16(unsigned int address) { 
	typedef uint16_t u_uint16_t __attribute__((aligned(1)));
	return BE16(*(u_uint16_t*)(pc_fastptr_bank + (address&0xFFFFF)));
}

static inline unsigned int  m68k_read_pcrelative_32(unsigned int address) { 
	typedef uint32_t u_uint32_t __attribute__((aligned(1)));
	return BE32(*(u_uint32_t*)(pc_fastptr_bank + (address&0xFFFFF)));
}

#else

// M68K memory handlers on N64 host.
//
// In general, m68k can do unaligned memory accesses, so we need to force GCC
// to generate code for accessing unaligned memory. This is done through the
// u_uint16_t / u_uint32_t typedefs, which for instance force GCC to generate
// LWL/LWR to do a 32-bit memory load.
//
// For HWIO registers, memory accesses will cause a TLB fault that will be
// handled by hw_n64.S to dispatch to the appropriate functions in hw.c.
// To simplify the job of the TLB exception handlers, we need HWIO memory accesses
// to happen with a single memory opcode (eg: LW), and this is indeed possible
// as memory mapped registers are hopefully always aligned. So, in general,
// all memory handlers will check whether the address is aligned or not, and
// generate the correct code for the two cases.

// Typedefs for unaligned memory accesses
typedef uint16_t u_uint16_t __attribute__((aligned(1)));
typedef uint32_t u_uint32_t __attribute__((aligned(1)));

static inline unsigned int  m68k_read_memory_8(unsigned int address) {
	return *(uint8_t*)address;
}
static inline unsigned int  m68k_read_memory_16(unsigned int address) {
	if (address & 1)
		return *(u_uint16_t*)address;
	else
		return *(volatile uint16_t*)address;
}
static inline unsigned int  m68k_read_memory_32(unsigned int address) {
	if (address & 3)
		return *(u_uint32_t*)address;
	else
		return *(volatile uint32_t*)address;
}

static inline void m68k_write_memory_8(unsigned int address, unsigned int val) {
	*(uint8_t*)address = val;
}
static inline void m68k_write_memory_16(unsigned int address, unsigned int val) { 
	if (address & 1)
		*(u_uint16_t*)address = val; 
	else
		*(uint16_t*)address = val;	
}
static inline void m68k_write_memory_32(unsigned int address, unsigned int val) { 
	if (address & 3)
		*(u_uint32_t*)address = val;
	else
		*(uint32_t*)address = val;	
}

// The following handlers are used to fetch opcodes from memory (activated by
// M68K_SEPARATE_READS in m68kconf.h). Since m68k always runs code from ROM/RAM
// (not from HWIO registers), we can use direct (unaligned) memory accesses without
// having to have a separate code-path for aligned addresses like we do for the
// other handlers.

static inline unsigned int  m68k_read_immediate_16(unsigned int address) { return *(u_uint16_t*)address; }
static inline unsigned int  m68k_read_immediate_32(unsigned int address) { return *(u_uint32_t*)address; }

static inline unsigned int  m68k_read_pcrelative_8(unsigned int address) { return *(uint8_t*)address; }
static inline unsigned int  m68k_read_pcrelative_16(unsigned int address) { return *(u_uint16_t*)address; }
static inline unsigned int  m68k_read_pcrelative_32(unsigned int address) { return *(u_uint32_t*)address; }

#endif

#endif