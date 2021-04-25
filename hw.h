#ifndef HW_H
#define HW_H

#include <stdint.h>
#include <stdbool.h>

extern uint8_t P_ROM_VECTOR[0x80];
extern uint8_t P_ROM[5*1024*1024];

extern uint8_t S_ROM[128*1024];
extern uint8_t SFIX_ROM[128*1024];
extern uint8_t *CUR_S_ROM;

extern uint8_t C_ROM[64*1024*1024];
extern int C_ROM_SIZE;
extern int C_ROM_PLANE_SIZE;

extern uint8_t BIOS[128*1024];

extern uint8_t WORK_RAM[64*1024];
extern uint8_t BACKUP_RAM[64*1024];
extern uint16_t VIDEO_RAM[34*1024];

extern uint32_t PALETTE_RAM[8*1024];  // two banks
extern uint32_t *CUR_PALETTE_RAM;

void hw_init(void);
void hw_vblank(void);

bool lspc_get_auto_animation(uint8_t *value);

#endif
