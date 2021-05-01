#ifndef HW_H
#define HW_H

#include <stdint.h>
#include <stdbool.h>

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
