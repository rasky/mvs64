#include <stdio.h>
#include <stdbool.h>
#include <memory.h>
#include "hw.h"
#include "video.h"
#include "emu.h"
#include "m68k.h"
#include "platform.h"

extern void cpu_start_trace(int cnt);

uint8_t P_ROM_VECTOR[0x80];
uint8_t P_ROM[5*1024*1024];
uint8_t BIOS[128*1024];
uint8_t WORK_RAM[64*1024];
uint8_t BACKUP_RAM[64*1024];
uint32_t PALETTE_RAM[8*1024];  // two banks
uint16_t VIDEO_RAM[34*1024];

uint8_t S_ROM[128*1024];
uint8_t SFIX_ROM[128*1024];

uint8_t C_ROM[64*1024*1024];
int C_ROM_SIZE;
int C_ROM_PLANE_SIZE;

uint8_t *CUR_S_ROM;
uint32_t *CUR_PALETTE_RAM;

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

#define DIPSW_SETTINGS_MODE    0x1


static void write_unk(uint32_t addr, uint32_t val, int sz) {
	debugf("[MEM] unknown write%d: %06x <- %0*x\n", sz*8, addr, sz*2, val);
}

static void write_bankswitch(uint32_t addr, uint32_t val, int sz) {
	if (addr == 0x2FFFFE) {
		debugf("[CART] bankswitch: %x\n", val);
		banks[0x2].mem = P_ROM + (val+1)*0x100000;
		return;
	}

	debugf("[CART] unknown write%d: %06x <- %0*x\n", sz*8, addr, sz*2, val);
}

uint32_t read_hwio(uint32_t addr, int sz)  {
	if ((addr>>16) == 0x30) switch (addr&0xFFFF) {
		case 0x00: assert(sz==1); return input_p1cnt_r();
		case 0x01: assert(sz==1); return 0xFF;

	} else if ((addr>>16) == 0x32) switch (addr&0xFFFF) {
		case 0x00: assert(sz==1); debugf("[HWIO] Read Z80 command\n"); return 1;
		case 0x01: assert(sz==1); return input_status_a_r();

	} else if ((addr>>16) == 0x38) switch (addr&0xFFFF) {
		case 0x00: assert(sz==1); return input_status_b_r();

	} else if ((addr>>16) == 0x3A) switch (addr&0xFFFF) {

	} else if ((addr>>16) == 0x3C) switch (addr&0xFFFF) {
		case 0x02: assert(sz==2); return lspc_vram_data_r();
		case 0x06: assert(sz==2); return lspc_mode_r();
	}

	debugf("[HWIO] unknown read%d: %06x\n", sz*8, addr);
	return 0xFFFFFFFF;
}

void write_hwio(uint32_t addr, uint32_t val, int sz)  {
	if ((addr>>16) == 0x30) switch (addr&0xFFFF) {
		case 0x01: return; // watchdog

	} else if ((addr>>16) == 0x32) switch (addr&0xFFFF) {
		case 0x00: assert(sz==1); debugf("[HWIO] Send Z80 command: %02x\n", val); return;

	} else if ((addr>>16) == 0x38) switch (addr&0xFFFF) {
		case 0x51: rtc_data_w(val&1); rtc_clock_w(val&2); rtc_stb_w(val&4); return;

	} else if ((addr>>16) == 0x3A) switch (addr&0xFFFF) {
		case 0x03: assert(sz==1); memcpy(P_ROM, BIOS, sizeof(P_ROM_VECTOR)); return;
		case 0x13: assert(sz==1); memcpy(P_ROM, P_ROM_VECTOR, sizeof(P_ROM_VECTOR)); return;
		case 0x0F: assert(sz==1); CUR_PALETTE_RAM = PALETTE_RAM + 0x1000; return;
		case 0x1F: assert(sz==1); CUR_PALETTE_RAM = PALETTE_RAM + 0x0000; return;
		case 0x0D: assert(sz==1); banks[0xD].w = write_unk; return;
		case 0x1D: assert(sz==1); banks[0xD].w = NULL; return;
		case 0x0B: assert(sz==1); CUR_S_ROM = SFIX_ROM; return;
		case 0x1B: assert(sz==1); CUR_S_ROM = S_ROM; return;

	} else if ((addr>>16) == 0x3C) switch (addr&0xFFFF) {
		case 0x00: lspc_vram_addr_w(val, sz); return;
		case 0x02: assert(sz==2); lspc_vram_data_w(val); return;
		case 0x04: assert(sz==2); lspc_vram_modulo_w(val); return;
		case 0x06: assert(sz==2); lspc_mode_w(val); return;
		case 0x0C: assert(sz==2); if (val&1) m68k_set_virq(3,false); if (val&2) m68k_set_virq(2,false); if (val&4) m68k_set_virq(1,false); return;
	}

	debugf("[HWIO] unknown write%d: %06x <- %0*x\n", sz*8, addr, sz*2, val);
}



unsigned int  m68k_read_memory_8(unsigned int address) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->r) return b->r(address, 1);
	if (b->mem) return *(b->mem + (address & b->mask));
	debugf("[MEM] unknown read8: %06x\n", address);
	return 0xFF;
}

unsigned int  m68k_read_memory_16(unsigned int address) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->r) return b->r(address, 2);
	if (b->mem) return BE16(*(uint16_t*)(b->mem + (address & b->mask)));
	debugf("[MEM] unknown read16: %06x\n", address);
	return 0xFFFF;
}

unsigned int  m68k_read_memory_32(unsigned int address) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->r) return b->r(address, 4);
	if (b->mem) return BE32(*(uint32_t*)(b->mem + (address & b->mask)));
	debugf("[MEM] unknown read32: %06x\n", address);
	return 0;
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->w) { b->w(address, value, 1); return; }
	if (b->mem) { *(b->mem + (address & b->mask)) = value; return; }
	debugf("[MEM] unknown write8: %06x = %02x\n", address, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->w) { b->w(address, value, 2); return; }
	if (b->mem) { *(uint16_t*)(b->mem + (address & b->mask)) = BE16(value); return; }
	debugf("[MEM] unknown write16: %06x = %04x\n", address, value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->w) { b->w(address, value, 4); return; }
	if (b->mem) { *(uint32_t*)(b->mem + (address & b->mask)) = BE32(value); return; }
	debugf("[MEM] unknown write32: %06x = %08x\n", address, value);
}

unsigned int m68k_read_disassembler_8(unsigned int address) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->mem)
		return *(b->mem + (address & b->mask));
	return 0xFFFFFFFF;
}

unsigned int m68k_read_disassembler_16(unsigned int address) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->mem)
		return BE16(*(uint16_t*)(b->mem + (address & b->mask)));
	return 0xFFFFFFFF;
}

unsigned int m68k_read_disassembler_32(unsigned int address) {
	Bank *b = &banks[(address>>20)&0xF];
	if (b->mem)
		return BE32(*(uint32_t*)(b->mem + (address & b->mask)));
	return 0xFFFFFFFF;
}


void hw_init(void) {
	memset(banks, 0, sizeof(banks));
	memcpy(P_ROM_VECTOR, P_ROM, sizeof(P_ROM_VECTOR));
	CUR_S_ROM = SFIX_ROM;
	CUR_PALETTE_RAM = PALETTE_RAM;

	banks[0x0] = (Bank){ P_ROM+0x000000,   0xFFFFF,   NULL,      write_unk };
	banks[0x1] = (Bank){ WORK_RAM,         0x0FFFF,   NULL,      NULL };
	banks[0x2] = (Bank){ P_ROM+0x100000,   0xFFFFF,   NULL,      write_bankswitch };
	banks[0x3] = (Bank){ NULL,             0x00000,   read_hwio, write_hwio };
	banks[0x4] = (Bank){ NULL,             0x00000,   video_palette_r,      video_palette_w };
	banks[0xC] = (Bank){ BIOS,             0x1FFFF,   NULL,      write_unk };
	banks[0xD] = (Bank){ BACKUP_RAM,       0x0FFFF,   NULL,      write_unk };

	rtc_init();
}

void hw_vblank(void) {
	lspc_vblank();
}
