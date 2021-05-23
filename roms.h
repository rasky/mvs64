#ifndef ROMS_H
#define ROMS_H

extern uint8_t P_ROM[];
extern unsigned int rom_pc_idle_skip;

void rom_load(const char *dir);

uint8_t* crom_get_sprite(int spritenum);
uint8_t* srom_get_sprite(int spritenum);

void srom_set_bank(int bank);  // 0 = fixed (BIOS), 1 = game

void rom_next_frame(void);

#endif
