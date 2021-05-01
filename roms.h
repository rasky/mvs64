#ifndef ROMS_H
#define ROMS_H

extern uint8_t P_ROM[];
extern uint8_t S_ROM[];
extern uint8_t SFIX_ROM[];

void rom_load_bios(const char *dir);
void rom_load_mslug(const char *dir);
void rom_load_kof98(const char *dir);
void rom_load_aof(const char *dir);
void rom_load_samsho(const char *dir);

void rom_load_spriteex(const char *dir);
void rom_load_nyanmvs(const char *dir);
void rom_load_krom(const char *dir);

uint8_t* crom_get_sprite(int spritenum);
uint8_t* srom_get_sprite(int spritenum);

#endif
