#ifndef M64K_H
#define M64K_H

#include <stdint.h>
#include <stdbool.h>
#include "m64k_config.h"

typedef struct {
    uint32_t dregs[8];
    uint32_t aregs[8];
    uint32_t usp;
    uint32_t ssp;
    uint32_t pc;
    uint32_t sr;
    uint32_t ir;
    uint32_t vbr;
    uint32_t pending_exc[4];
    int64_t cycles;
} m64k_t;

void m64k_init(m64k_t *m64k);
void m64k_run(m64k_t *m64k, int64_t until);
void m64k_exception_address(m64k_t *m64k, uint32_t address, uint16_t fc);
void m64k_exception_divbyzero(m64k_t *m64k);

#endif
