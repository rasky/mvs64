#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "platform.h"
#include "hw.h"

static void rom(const char *dir, const char* name, int off, int sz, uint8_t *buf, bool bswap) {
	char fullname[1024];
	strlcpy(fullname, dir, sizeof(fullname));
	strlcat(fullname, name, sizeof(fullname));

	FILE *f = fopen(fullname, "rb");
	assertf(f, "file not found: %s", fullname);
	fseek(f, off, SEEK_SET);
	int read = fread(buf, 1, sz, f);
	fclose(f);
	assertf(read == sz, "rom:%s off:%d sz:%d read:%d", fullname, off, sz, read);

	if (bswap) {
		for (int i=0;i<sz;i+=2) {
			uint8_t v = buf[i];
			buf[i] = buf[i+1];
			buf[i+1] = v;
		}
	}
}


void rom_load_bios(const char *dir) {
	// rom(dir, "sp1-selftest.bin", 0, 128*1024, BIOS, true);
	rom(dir, "sp-s2.sp1", 0, 128*1024, BIOS, true);
	rom(dir, "sfix.sfix", 0, 128*1024, SFIX_ROM, false);
}

void rom_load_mslug(const char *dir) {
	rom(dir, "201-p1.bin", 1*1024*1024, 1024*1024, P_ROM+0*1024*1024, true);
	rom(dir, "201-p1.bin", 0*1024*1024, 1024*1024, P_ROM+1*1024*1024, true);
	rom(dir, "201-s1.bin", 0, 128*1024, S_ROM, false);
	rom(dir, "201-c1.bin", 0, 4*1024*1024, C_ROM+0*4*1024*1024, false);
	rom(dir, "201-c3.bin", 0, 4*1024*1024, C_ROM+1*4*1024*1024, false);
	rom(dir, "201-c2.bin", 0, 4*1024*1024, C_ROM+2*4*1024*1024, false);
	rom(dir, "201-c4.bin", 0, 4*1024*1024, C_ROM+3*4*1024*1024, false);
	C_ROM_SIZE = 16*1024*1024;
	C_ROM_PLANE_SIZE = 8*1024*1024;
}

void rom_load_kof98(const char *dir) {
	rom(dir, "kof98_p1.rom", 0, 1024*1024, P_ROM+0*1024*1024, true);
	rom(dir, "kof98_p2.rom", 0, 4*1024*1024, P_ROM+1*1024*1024, true);
	rom(dir, "kof98_s1.rom", 0, 128*1024, S_ROM, false);
	rom(dir, "kof98_c1.rom", 0, 8*1024*1024, C_ROM+0*8*1024*1024, false);
	rom(dir, "kof98_c3.rom", 0, 8*1024*1024, C_ROM+1*8*1024*1024, false);
	rom(dir, "kof98_c5.rom", 0, 8*1024*1024, C_ROM+2*8*1024*1024, false);
	rom(dir, "kof98_c7.rom", 0, 8*1024*1024, C_ROM+3*8*1024*1024, false);
	rom(dir, "kof98_c2.rom", 0, 8*1024*1024, C_ROM+4*8*1024*1024, false);
	rom(dir, "kof98_c4.rom", 0, 8*1024*1024, C_ROM+5*8*1024*1024, false);
	rom(dir, "kof98_c6.rom", 0, 8*1024*1024, C_ROM+6*8*1024*1024, false);
	rom(dir, "kof98_c8.rom", 0, 8*1024*1024, C_ROM+7*8*1024*1024, false);
	C_ROM_SIZE = 64*1024*1024;
	C_ROM_PLANE_SIZE = 32*1024*1024;
}

void rom_load_aof(const char *dir) {
	rom(dir, "044-p1.bin", 0, 512*1024, P_ROM, true);
	rom(dir, "044-s1.bin", 0, 128*1024, S_ROM, false);
	rom(dir, "044-c1.bin", 0, 2*1024*1024, C_ROM+0*2*1024*1024, false);
	rom(dir, "044-c3.bin", 0, 2*1024*1024, C_ROM+1*2*1024*1024, false);
	rom(dir, "044-c2.bin", 0, 2*1024*1024, C_ROM+2*2*1024*1024, false);
	rom(dir, "044-c4.bin", 0, 2*1024*1024, C_ROM+3*2*1024*1024, false);
	C_ROM_SIZE = 8*1024*1024;
	C_ROM_PLANE_SIZE = 4*1024*1024;
}

void rom_load_nyanmvs(const char *dir) {
	rom(dir, "052-p1.bin", 0, 128*1024, P_ROM, false);
	rom(dir, "052-s1.bin", 0, 128*1024, S_ROM, false);
	rom(dir, "052-c1.bin", 0, 4*1024*1024, C_ROM+0*4*1024*1024, false);
	rom(dir, "052-c2.bin", 0, 4*1024*1024, C_ROM+1*4*1024*1024, false);
	C_ROM_SIZE = 8*1024*1024;
	C_ROM_PLANE_SIZE = 4*1024*1024;
}

void rom_load_spriteex(const char *dir) {
	rom(dir, "052-p1.bin", 0, 512*1024, P_ROM, true);
	rom(dir, "052-s1.bin", 0, 128*1024, S_ROM, false);
	rom(dir, "052-c1.bin", 0, 4*1024*1024, C_ROM+0*4*1024*1024, false);
	rom(dir, "052-c2.bin", 0, 4*1024*1024, C_ROM+1*4*1024*1024, false);
	C_ROM_SIZE = 8*1024*1024;
	C_ROM_PLANE_SIZE = 4*1024*1024;
}

void rom_load_krom(const char *dir) {
	rom(dir, "LSPC/3D-LineSpriteChain/202-p1.p1", 0, 512*1024, P_ROM, true);
	rom(dir, "LSPC/3D-LineSpriteChain/202-s1.s1", 0, 128*1024, S_ROM, false);
	rom(dir, "LSPC/3D-LineSpriteChain/202-c1.c1", 0, 1024*1024, C_ROM, false);
	rom(dir, "LSPC/3D-LineSpriteChain/202-c2.c2", 0, 1024*1024, C_ROM+1024*1024, false);
	C_ROM_SIZE = 2*1024*1024;
	C_ROM_PLANE_SIZE = 1*1024*1024;
}
