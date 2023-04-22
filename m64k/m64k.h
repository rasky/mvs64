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
    uint32_t ts_start;
    uint32_t ts_cur;
    uint8_t ipl;
    uint8_t nmi_pending;
    uint8_t check_interrupts;

    uint8_t virq;
    int (*hook_irqack)(void *ctx, int level);
    void *hook_irqack_ctx;
} m64k_t;


void m64k_init(m64k_t *m64k);
void m64k_pulse_reset(m64k_t *m64k);
int64_t m64k_run(m64k_t *m64k, int64_t until);

/** 
 * @brief Map a linear buffer of memory into the m68k memory map.
 * 
 * This function should be called to configure a linear memory buffer within
 * the m64k address space. Configuration is made via a VR4300 TLB for
 * performance, so it has pretty strict constraints. The main constraint
 * is that of an alignment: for instance, to map a 128 KiB buffer, the buffer
 * must be 128 KiB aligned in both the N64 and m68k address spaces. Normally,
 * you will want to allocate it with #malloc_aligned to ensure this.
 * 
 * A buffer can be either only readable, or readable/writable. If the buffer
 * is not writable, writes to the memory region will be ignored. Notice that
 * this will be done configuring a TLB entry with the "no write" flag set,
 * and then catching and ignoring the resulting TLB exception, so it will
 * be quite slower than a writable buffer.
 * 
 * @note As an exception to alignment rules, 4 KiB buffers (the smallest supported)
 *       must be 8 KiB aligned. This is required to enforce cache coherency
 *       between regular and TLB accesses.
 * 
 * @param m64k      The m68k context
 * @param address   The address to map the memory to in the m68k address space.
 *                  This must be a 24-bit address, and must be aligned to the
 *                  size of the memory region.
 * @param size      The size of the memory region to map. This must be a power
 *                  of two, and minimum 4 KiB.
 * @param ptr       Pointer to the buffer in N64 memory space.
 * @param writable  Whether the memory region is writable. If false, the memory
 *                  will be mapped as read-only.
 * 
 */
void m64k_map_memory(m64k_t *m64k, uint32_t address, uint32_t size, void *ptr, bool writable);


typedef uint16_t (*m64k_ioread_t)(uint32_t address, int sz);
typedef void (*m64k_iowrite_t)(uint32_t address, uint16_t value, int sz);

void m64k_map_io(m64k_t *m64k, uint32_t address, uint32_t size, m64k_ioread_t read, m64k_iowrite_t write);

/**
 * @brief Stop the m68k execution.
 * 
 * This function can be called from within a MMIO handler to abort the
 * current timeslice and have #m64k_run return as soon as possible, even
 * before reaching the requested target.
 */
void m64k_run_stop(m64k_t *m64k);

/**
 * @brief Get the current PC.
 * 
 * This function can be called to obtain the current PC. Notice that
 * the value returned is correct even while the m68k is running.
 * 
 * @param m64k        The m68k context
 * @return uint32_t   The current PC
 */
uint32_t m64k_get_pc(m64k_t *m64k);

/**
 * @brief Get the current clock
 * 
 * This function can be called to obtain the current clock counter.
 * Notice that the value returned is correct even while the m68k
 * is running.
 * 
 * @param m64k        The m68k context
 * @return uint32_t   The current PC
 */
int64_t m64k_get_clock(m64k_t *m64k);

/**
 * @brief Set the IRQ line state.
 * 
 * @param m64k  The m68k context
 * @param irq   The IRQ line to set. This must be a value between 0 and 7.
 * 
 * @note The m64k core only supports autovectored interrupts.
 */
void m64k_set_irq(m64k_t *m64k, int irq);

void m64k_set_virq(m64k_t *m64k, int irq, bool state);

/**
 * @brief Configure the IRQ acknowledge hook.
 * 
 * This hook is called any time the m68k triggers an interrupt. It allows
 * the external peripherals (eg: the interrupt controller) to be aware
 * that an interrupt has been triggered, and thus probably to change
 * the IPL lines (via #m64k_set_irq).
 * 
 * If an hook is not configured, m64k will automatically set the IPL
 * lines to 0 when the interrupt is triggered. This basically implements
 * the most simple auto-ack scenario.
 * 
 * @note The hook return value is ignored (reserved for future use).
 *       Hooks are currently expected to return 0 for forward compatibility.
 * 
 * @param m64k 
 * @param hook 
 * @param ctx 
 */
void m64k_set_hook_irqack(m64k_t *m64k, int (*hook)(void *ctx, int level), void *ctx);

void m64k_exception_address(m64k_t *m64k, uint32_t address, uint16_t fc);
void m64k_exception_divbyzero(m64k_t *m64k);

#endif
