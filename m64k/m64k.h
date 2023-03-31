#ifndef M64K_H
#define M64K_H

#include <stdint.h>

typedef struct {
    uint32_t dregs[8];
    uint32_t aregs[7];
    uint32_t usp;
    uint32_t ssp;
    uint32_t pc;
    uint32_t sr;

    int64_t cycles;
} m64k_t;

void m64k_init(m64k_t *m64k);
void m64k_run(m64k_t *m64k, int64_t until);

#endif
