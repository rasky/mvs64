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

static inline unsigned int  m68k_read_immediate_16(unsigned int address) { return *(u_uint16_t*)address; }
static inline unsigned int  m68k_read_immediate_32(unsigned int address) { return *(u_uint32_t*)address; }

static inline unsigned int  m68k_read_pcrelative_8(unsigned int address) { return *(uint8_t*)address; }
static inline unsigned int  m68k_read_pcrelative_16(unsigned int address) { return *(u_uint16_t*)address; }
static inline unsigned int  m68k_read_pcrelative_32(unsigned int address) { return *(u_uint32_t*)address; }

#endif

#endif