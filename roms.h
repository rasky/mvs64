#ifndef ROMS_H
#define ROMS_H

extern uint8_t P_ROM[];
extern unsigned int rom_pc_idle_skip;

void rom_load(const char *dir);

uint8_t* crom_get_sprite(int spritenum);
uint8_t* srom_get_sprite(int spritenum);

void srom_set_bank(int bank);  // 0 = fixed (BIOS), 1 = game

uint8_t* pbrom_linear(void);
uint8_t* pbrom_cache_lookup(uint32_t addr);

void rom_next_frame(void);

#endif
