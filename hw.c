#include <stdio.h>
#include <stdbool.h>
#include <memory.h>
#include "hw.h"
#include "roms.h"
#include "video.h"
#include "emu.h"
#include "m68k.h"
#include "platform.h"

// Typedefs for unaligned memory accesses
typedef uint16_t u_uint16_t __attribute__((aligned(1)));
typedef uint32_t u_uint32_t __attribute__((aligned(1)));

extern void cpu_start_trace(int cnt);

#ifdef N64
#define ALIGN_64K __attribute__((aligned(64*1024)))
#else
#define ALIGN_64K
#endif

uint8_t P_ROM_VECTOR[0x80];
uint8_t BIOS[128*1024];
uint8_t WORK_RAM[64*1024] ALIGN_64K;
uint8_t BACKUP_RAM[64*1024] ALIGN_64K;
uint16_t PALETTE_RAM[8*1024];  // two banks
uint16_t VIDEO_RAM[34*1024];

int PALETTE_RAM_BANK;

typedef uint32_t (*ReadCB)(uint32_t addr, int sz);
typedef void (*WriteCB)(uint32_t addr, uint32_t val, int sz);

typedef struct {
	uint8_t *mem;
	uint32_t mask;
	ReadCB r;
	WriteCB w;
} Bank;

static Bank banks[16];

#include "rtc.c"
#include "lspc.c"
#include "input.c"
#include "watchdog.c"

#define DIPSW_SETTINGS_MODE    (1<<0)
#define DIPSW_FREEPLAY         (1<<6)

static void write_unk(uint32_t addr, uint32_t val, int sz) {
	debugf("[MEM] unknown write%d: %06x <- %0*x\n", sz*8, (unsigned int)addr, sz*2, (unsigned int)val);
}

static int pbrom_bank = 0;

void write_pbrom(uint32_t addr, uint32_t val, int sz) {
	if (addr >= 0x2FFFF0 && addr <= 0x2FFFFF) {
		val &= 7;
		pbrom_bank = val << 20;

		// If the PBROM area linearly mapped, update the mapping.
		if (banks[0x2].mem)
			banks[0x2].mem = pbrom_linear() + val*0x100000;
		// debugf("[CART] bankswitch %x <= %x\n", (unsigned int)addr, (unsigned int)pbrom_bank);
		return;
	}

	debugf("[CART] unknown write%d: %06x <- %0*x (PC:%08x)\n", sz*8, (unsigned int)addr, sz*2, (unsigned int)val, m68k_get_reg(NULL, M68K_REG_PC));
}

uint32_t read_pbrom(uint32_t addr, int sz) {
	// Most (all?) games seem to keep the bank number at 0x2fffef,
	// and they check for it after switching banks (in a loop, maybe
	// to wait until the bank is effectively mapped).
	// Since games often uselessly bankswitch and they only access
	// this location, we shortcircuit it to avoid loading that banks
	// uselessly everytime.
	// FIXME: check if we really need this
	// FIXME: see if we can detect when this is false, to avoid bugs
	if (addr == 0x2fffef && sz == 1) return pbrom_bank >> 20;

	uint8_t *rom = pbrom_cache_lookup(pbrom_bank | (addr & 0x0FFFFF));
	if (sz == 4) return BE32(*(u_uint32_t*)rom);
	if (sz == 2) return BE16(*(uint16_t*)rom);
	return *rom;
}

uint32_t read_hwio(uint32_t addr, int sz)  {
	if (sz == 4) {
		// NOTE: order is important
		uint32_t val = read_hwio(addr+0, 2) << 16;
		return val | read_hwio(addr+2, 2);
	}

	// Idle skip for RTC Wait Pulse in BIOS boot
	if (addr == 0x320001 && m68k_get_reg(NULL, M68K_REG_PC) == 0xC11DA2) {
		m68k_consume_timeslice();
	}

	// debugf("[HWIO] read%d: %06x (68K PC:%x)\n", sz*8, (unsigned int)addr, m68k_get_reg(NULL, M68K_REG_PC));
	// debugf("[HWIO] read%d: %06x (68K PC:%x EPC:%lx)\n", sz*8, (unsigned int)addr, m68k_get_reg(NULL, M68K_REG_PC), C0_READ_EPC());

	if ((addr>>16) == 0x30) switch (addr&0xFFFF) {
		case 0x00: assert(sz==1); return input_p1cnt_r();
		case 0x01: assert(sz==1); return 0xFF ^ DIPSW_FREEPLAY; // dipswitches

	} else if ((addr>>16) == 0x32) switch (addr&0xFFFF) {
		case 0x00: assert(sz==1); debugf("[HWIO] Read Z80 command\n"); return 1;
		case 0x01: assert(sz==1); return input_status_a_r();

	} else if ((addr>>16) == 0x38) switch (addr&0xFFFF) {
		case 0x00: assert(sz==1); return input_status_b_r();

	} else if ((addr>>16) == 0x3A) switch (addr&0xFFFF) {

	} else if ((addr>>16) == 0x3C) switch (addr&0xFFFF) {
		case 0x00: case 0x08: case 0x0A:
		case 0x02: assert(sz==2); return lspc_vram_data_r();
		case 0x0C:
		case 0x04: assert(sz==2); return lspc_vram_modulo_r();
		case 0x0E:
		case 0x06: assert(sz==2); return lspc_mode_r();
	}

	debugf("[HWIO] unknown read%d: %06x\n", sz*8, (unsigned int)addr);
	return 0xFFFFFFFF;
}

void write_hwio(uint32_t addr, uint32_t val, int sz)  {
	if (sz == 4) {
		write_hwio(addr+0, val>>16, 2);
		write_hwio(addr+2, val&0xFFFF, 2);
		return;
	}

	if ((addr>>16) == 0x30) switch (addr&0xFFFF) {
		case 0x01: watchdog_kick(); return;

	} else if ((addr>>16) == 0x32) switch (addr&0xFFFF) {
		case 0x00: assert(sz==1); debugf("[HWIO] Send Z80 command: %02x\n", (unsigned int)val); return;

	} else if ((addr>>16) == 0x38) switch (addr&0xFFFF) {
		case 0x51: rtc_data_w(val&1); rtc_clock_w(val&2); rtc_stb_w(val&4); return;

	} else if ((addr>>16) == 0x3A) switch (addr&0xFFFF) {
		case 0x03: assert(sz==1); memcpy(P_ROM, BIOS, sizeof(P_ROM_VECTOR)); return;
		case 0x13: assert(sz==1); memcpy(P_ROM, P_ROM_VECTOR, sizeof(P_ROM_VECTOR)); return;
		case 0x0F: assert(sz==1); PALETTE_RAM_BANK = 0x1000; return;
		case 0x1F: assert(sz==1); PALETTE_RAM_BANK = 0x0000; return;
		case 0x0D: assert(sz==1); banks[0xD].w = write_unk; return;
		case 0x1D: assert(sz==1); banks[0xD].w = NULL; return;
		case 0x0B: assert(sz==1); srom_set_bank(0); return;
		case 0x1B: assert(sz==1); srom_set_bank(1); return;

	} else if ((addr>>16) == 0x3C) switch (addr&0xFFFF) {
		case 0x00: assert(sz==2); lspc_vram_addr_w(val); return;
		case 0x02: assert(sz==2); lspc_vram_data_w(val); return;
		case 0x04: assert(sz==2); lspc_vram_modulo_w(val); return;
		case 0x06: assert(sz==2); lspc_mode_w(val); return;
		case 0x0C: if (val&1) m68k_set_virq(3,false); if (val&2) m68k_set_virq(2,false); if (val&4) m68k_set_virq(1,false); return;
	}

	debugf("[HWIO] unknown write%d: %06x <- %0*x (PC=%06x)\n", sz*8, (unsigned int)addr, sz*2, (unsigned int)val, m68k_get_reg(NULL, M68K_REG_PC));
}


#ifndef N64

unsigned int  m68k_read_memory_8(unsigned int address) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->r) return b->r(address, 1);
	if (b->mem) return *(b->mem + (address & b->mask));
	debugf("[MEM] unknown read8: %06x\n", (unsigned int)address);
	return 0xFF;
}

unsigned int  m68k_read_memory_16(unsigned int address) {
	assertf(!(address&1), "unaligned rm16: %x\n", address);
	Bank *b = &banks[(address>>20)&0xF];
	if (b->r) return b->r(address, 2);
	if (b->mem) return BE16(*(u_uint16_t*)(b->mem + (address & b->mask)));
	debugf("[MEM] unknown read16: %06x\n", (unsigned int)address);
	return 0xFFFF;
}

unsigned int  m68k_read_memory_32(unsigned int address) {
	assertf(!(address&1), "unaligned rm32: %x\n", address);
	Bank *b = &banks[(address>>20)&0xF];
	if (b->r) return b->r(address, 4);
	if (b->mem) return BE32(*(u_uint32_t*)(b->mem + (address & b->mask)));
	debugf("[MEM] unknown read32: %06x\n", (unsigned int)address);
	return 0;
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->w) { b->w(address, value, 1); return; }
	if (b->mem) { *(b->mem + (address & b->mask)) = value; return; }
	debugf("[MEM] unknown write8: %06x = %02x\n", (unsigned int)address, (unsigned int)value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
	assertf(!(address&1), "unaligned wm16: %x\n", address);
	Bank *b = &banks[(address>>20)&0xF];
	if (b->w) { b->w(address, value, 2); return; }
	if (b->mem) { *(u_uint16_t*)(b->mem + (address & b->mask)) = BE16(value); return; }
	debugf("[MEM] unknown write16: %06x = %04x\n", (unsigned int)address, (unsigned int)value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
	assertf(!(address&1), "unaligned wm32: %x\n", address);
	Bank *b = &banks[(address>>20)&0xF];
	if (b->w) { b->w(address, value, 4); return; }
	if (b->mem) { *(u_uint32_t*)(b->mem + (address & b->mask)) = BE32(value); return; }
	debugf("[MEM] unknown write32: %06x = %08x\n", (unsigned int)address, (unsigned int)value);
}
#endif


unsigned int m68k_read_disassembler_8(unsigned int address) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->mem)
		return *(b->mem + (address & b->mask));
	return 0xFFFFFFFF;
}

unsigned int m68k_read_disassembler_16(unsigned int address) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->mem)
		return BE16(*(u_uint16_t*)(b->mem + (address & b->mask)));
	return 0xFFFFFFFF;
}

unsigned int m68k_read_disassembler_32(unsigned int address) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->mem)
		return BE32(*(u_uint32_t*)(b->mem + (address & b->mask)));
	return 0xFFFFFFFF;
}


#ifdef N64

#define C0_INDEX() ({ \
    uint32_t x; \
    asm volatile("mfc0 %0,$0":"=r"(x)); \
    x; \
})

#define C0_ENTRYLO0() ({ \
    uint32_t x; \
    asm volatile("mfc0 %0,$2":"=r"(x)); \
    x; \
})

#define C0_ENTRYLO1() ({ \
    uint32_t x; \
    asm volatile("mfc0 %0,$3":"=r"(x)); \
    x; \
})

#define C0_WRITE_INDEX(x)    asm volatile("mtc0 %0,$0; nop; nop"::"r"(x))
#define C0_WRITE_ENTRYHI(x)  asm volatile("mtc0 %0,$10; nop; nop"::"r"(x))
#define C0_WRITE_ENTRYLO0(x) asm volatile("mtc0 %0,$2; nop; nop"::"r"(x))
#define C0_WRITE_ENTRYLO1(x) asm volatile("mtc0 %0,$3; nop; nop"::"r"(x))
#define C0_WRITE_PAGEMASK(x) asm volatile("mtc0 %0,$5; nop; nop"::"r"(x))

#define C0_TLBWI()           asm volatile("tlbwi; nop; nop; nop; nop")
#define C0_TLBR()            asm volatile("tlbr; nop; nop; nop; nop")
#define C0_TLBP()            asm volatile("tlbp; nop; nop; nop; nop")

void tlb_init(void) {
	C0_WRITE_ENTRYHI(0xFFFFFFFF);
	C0_WRITE_ENTRYLO0(0);
	C0_WRITE_ENTRYLO1(0);
	C0_WRITE_PAGEMASK(0);
	for (int i=0;i<32;i++) {
		C0_WRITE_INDEX(i);
		C0_TLBWI();
	}
}

void tlb_map_area(unsigned int idx, uint32_t virt, uint32_t vmask, void* phys, bool readwrite) {

	bool dbl;
	switch (vmask) {
	case 0x00FFFF: C0_WRITE_PAGEMASK(0x0F << 13); dbl=false; break;
	case 0x01FFFF: C0_WRITE_PAGEMASK(0x0F << 13); dbl=true;  break;
	case 0x03FFFF: C0_WRITE_PAGEMASK(0x3F << 13); dbl=false; break;
	case 0x07FFFF: C0_WRITE_PAGEMASK(0x3F << 13); dbl=true;  break;
	case 0x0FFFFF: C0_WRITE_PAGEMASK(0xFF << 13); dbl=false; break;
	case 0x1FFFFF: C0_WRITE_PAGEMASK(0xFF << 13); dbl=true;  break;
	default: assert(0);
	}

	if (dbl) vmask >>= 1;
	assert(((uint32_t)phys & vmask) == 0);
	assert((virt & vmask) == 0);

	uint32_t vpn2 = virt;
	if (!dbl) vpn2 &= ~(vmask+1);
	C0_WRITE_ENTRYHI(vpn2);

	C0_TLBP();
	uint32_t existing = C0_INDEX();
	uint32_t exentry0 = C0_ENTRYLO0();
	uint32_t exentry1 = C0_ENTRYLO1();
	assertf((existing & 0x80000000) || (((exentry0|exentry1) & 2) == 0), "Duplicated TLB entry with vaddr %08lx (%lx/%lx)", vpn2, exentry0, exentry1);

	if (dbl || !(virt & (vmask+1))) {	
		uint32_t entry = ((uint32_t)phys & 0x3FFFFFFF) >> 6;
		if (readwrite)
			entry |= (1<<2); // dirty bit
		entry |= 1<<1; // valid bit
		entry |= 1<<0; // global bit
		C0_WRITE_ENTRYLO0(entry);
	} else {
		C0_WRITE_ENTRYLO0(1<<0);
	}

	if (dbl || (virt & (vmask+1))) {	
		uint32_t entry = (((uint32_t)phys + (dbl ? vmask + 1 : 0)) & 0x3FFFFFFF) >> 6;
		if (readwrite)
			entry |= (1<<2); // dirty bit
		entry |= 1<<1; // valid bit
		entry |= 1<<0; // global bit
		C0_WRITE_ENTRYLO1(entry);
	} else {
		C0_WRITE_ENTRYLO1(1<<0);		
	}

	assert(idx < 32);
	C0_WRITE_INDEX(idx);
	C0_TLBWI();
}

#endif


void hw_init(void) {
	uint8_t *PB_ROM = pbrom_linear();

	memset(banks, 0, sizeof(banks));
	memcpy(P_ROM_VECTOR, P_ROM, sizeof(P_ROM_VECTOR));
	PALETTE_RAM_BANK = 0x0000;

	banks[0x0] = (Bank){ P_ROM+0x000000,   0xFFFFF,   NULL,            write_unk };
	banks[0x1] = (Bank){ WORK_RAM,         0x0FFFF,   NULL,            NULL };
	if (PB_ROM)
		banks[0x2] = (Bank){ PB_ROM,       0xFFFFF,   NULL,            write_pbrom };
	else
		banks[0x2] = (Bank){ NULL,         0xFFFFF,   read_pbrom,      write_pbrom };
	banks[0x3] = (Bank){ NULL,             0x00000,   read_hwio,       write_hwio };
	banks[0x4] = (Bank){ NULL,             0x00000,   video_palette_r, video_palette_w };
	banks[0xC] = (Bank){ BIOS,             0x1FFFF,   NULL,            write_unk };
	banks[0xD] = (Bank){ BACKUP_RAM,       0x0FFFF,   NULL,            write_unk };

	#ifdef N64
	disable_interrupts();
	tlb_init();
	tlb_map_area(0, 0x000000, 0x7FFFF, P_ROM+0x000000, false);
	tlb_map_area(1, 0x080000, 0x7FFFF, P_ROM+0x080000, false);
	if (PB_ROM) {
		tlb_map_area(2, 0x200000, 0x7FFFF, PB_ROM+0x000000, false);
		tlb_map_area(3, 0x280000, 0x7FFFF, PB_ROM+0x080000, false);		
	}
	tlb_map_area(4, 0xC00000, 0x1FFFF, BIOS, false);

	tlb_map_area(8, 0x100000, 0x0FFFF, WORK_RAM, true);
	tlb_map_area(9, 0xD00000, 0x0FFFF, BACKUP_RAM, true);

	// Install special exception handler, so that we can handle our own
	// exceptions
	extern uint32_t mvs_intvector[];
	extern uint32_t mvs_tlbvector[];
	volatile uint32_t *mips_exc_vector = (volatile uint32_t*)0x80000180;
	volatile uint32_t *mips_tlb_vector = (volatile uint32_t*)0x80000080;

	for (int i=0;i<4;i++) {
		mips_exc_vector[i] = mvs_intvector[i];
		mips_tlb_vector[i] = mvs_tlbvector[i];
	}
	data_cache_hit_writeback_invalidate(mips_exc_vector, 16);
	data_cache_hit_writeback_invalidate(mips_tlb_vector, 16);
	inst_cache_hit_invalidate(mips_exc_vector, 16);
	inst_cache_hit_invalidate(mips_tlb_vector, 16);

	enable_interrupts();

	#if 0
	// Self-tests just to make sure we're not getting things wrong
	assert(memcmp((void*)0x200000, P_ROM+0x100000, 1024*1024) == 0);
	assert(memcmp((void*)0xC00000, BIOS, 128*1024) == 0);
	assert(memcmp((void*)0x000000+1, P_ROM+1, 1024*1024-1) == 0);
	#endif

	#endif

	rtc_init_();
	watchdog_init();
}

void hw_vblank(void) {
	lspc_vblank();
}
