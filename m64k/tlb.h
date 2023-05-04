#ifndef M64K_TLB_H
#define M64K_TLB_H

#include <stdint.h>
#include <stdbool.h>

#define TLBF_READONLY      (1<<0)    // Set if the TLB area is read-only (writes will cause an exception)
#define TLBF_OVERWRITE     (1<<1)    // Set if you want to overwrite existing TLB entries

/** @brief Add a TLB mapping of a memory area */
uint32_t __m64k_tlb_add(void *virt, uint32_t vmask, uint32_t phys, int flags);

/** @brief Remove a TLB mapping of a memory area */
void __m64k_tlb_change(uint32_t mid, uint32_t phys, int flags);

/** @brief Remove a TLB mapping of a memory area */
void __m64k_tlb_rem(uint32_t mid);

/** @brief Reset all TLBs */
void __m64k_tlb_reset(void);

#endif
