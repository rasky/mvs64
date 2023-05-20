#ifndef M64K_INTERNAL_H
#define M64K_INTERNAL_H

#define M64K_PENDINGEXC_ADDRERR   1
#define M64K_PENDINGEXC_RSTO      2
#define M64K_PENDINGEXC_PRIVERR   3
#define M64K_PENDINGEXC_DIVBYZERO 4
#define M64K_PENDINGEXC_IRQ       5

#define M64K_OFF_DREGS        0
#define M64K_OFF_AREGS        (M64K_OFF_DREGS      + 8 * 4)
#define M64K_OFF_USP          (M64K_OFF_AREGS      + 8 * 4)
#define M64K_OFF_SSP          (M64K_OFF_USP        + 1 * 4)
#define M64K_OFF_PC           (M64K_OFF_SSP        + 1 * 4)
#define M64K_OFF_SR           (M64K_OFF_PC         + 1 * 4)
#define M64K_OFF_IR           (M64K_OFF_SR         + 1 * 4)
#define M64K_OFF_VBR          (M64K_OFF_IR         + 1 * 4)
#define M64K_OFF_PENDINGEXC   (M64K_OFF_VBR        + 1 * 4)
#define M64K_OFF_CYCLES       (M64K_OFF_PENDINGEXC + 4 * 4)
#define M64K_OFF_TS_START     (M64K_OFF_CYCLES     + 1 * 8)
#define M64K_OFF_TS_CUR       (M64K_OFF_TS_START   + 1 * 4)
#define M64K_OFF_IPL          (M64K_OFF_TS_CUR     + 1 * 4)
#define M64K_OFF_NMI_PENDING  (M64K_OFF_IPL        + 1 * 1)
#define M64K_OFF_CHECK_INTERRUPTS  (M64K_OFF_NMI_PENDING + 1 * 1)


#ifndef __ASSEMBLER__
#include "m64k.h"
#include <stddef.h>
_Static_assert(offsetof(m64k_t, dregs)  == M64K_OFF_DREGS, "dregs offset is wrong");
_Static_assert(offsetof(m64k_t, aregs)  == M64K_OFF_AREGS, "dregs offset is wrong");
_Static_assert(offsetof(m64k_t, sr)     == M64K_OFF_SR, "dregs offset is wrong");
_Static_assert(offsetof(m64k_t, cycles) == M64K_OFF_CYCLES, "cycles offset is wrong");
_Static_assert(offsetof(m64k_t, ts_start) == M64K_OFF_TS_START, "ts_start offset is wrong");
_Static_assert(offsetof(m64k_t, check_interrupts) == M64K_OFF_CHECK_INTERRUPTS, "check_interrupts offset is wrong");
#endif

#endif
