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

typedef uint32_t m64k_mapping_t;


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
 * Calling #m64k_map_memory again with the same address will simply cause
 * the previous mapping to be overwritten. Notice though if the new size
 * is smaller than the previous one, a parte of the previous mapping will still
 * be valid. To completely remove a previous mapping, using #m64k_unmap_memory.
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
 * @return A mapping ID
 * 
 * @see #m64k_map_memory_change
 * @see #m64k_unmap_memory
 */
m64k_mapping_t m64k_map_memory(m64k_t *m64k, uint32_t address, uint32_t size, void *ptr, bool writable);

/**
 * @brief Change a memory region previously mapped with #m64k_map_memory.
 * 
 * This function can be used to change the address and permissions of a memory
 * region previously mapped with #m64k_map_memory. Notice that it is always
 * possible to call #m64k_map_memory again with the same address to obtain
 * the same effect, but this function is much faster.
 * 
 * @param m64k      The m68k context
 * @param mapping   The mapping ID returned by #m64k_map_memory.
 * @param ptr       Pointer to the new buffer in N64 memory space.
 * @param writable  Whether the memory region is writable. If false, the memory
 *                  will be mapped as read-only.
 */
void m64k_map_memory_change(m64k_t *m64k, m64k_mapping_t mapping, void *ptr, bool writable);

/**
 * @brief Unmap a memory region previously mapped with #m64k_map_memory.
 * 
 * After running this function, the previous mapping will be removed from the
 * m68k memory map. This means that accesses to the memory region will fall
 * into the MMIO handler.
 * 
 * @param m64k      The m68k context
 * @param mapping   The mapping ID returned by #m64k_map_memory.
 */
void m64k_unmap_memory(m64k_t *m64k, m64k_mapping_t mapping);

/**
 * @brief Configure the fast MMIO handlers, written in assembly.
 * 
 * This function should be called to map assembly I/O handlers. These are handlers
 * are potentially the fastest as they are quicker to call compared to C ones
 * (fewer context switches are required). It is suggested to implement the
 * most performance-critical I/O handlers in assembly, and the others in C
 * (via #m64k_map_io).
 * 
 * Like the C handlers, there is no attempt at address decoding. The specified
 * functions will be called for all addresses that are not otherwise mapped
 * as memory (via #m64k_map_memory), and even for write accesses to read-only memory.
 * 
 * Assembly handlers are called with the following input registers:
 * 
 * - `k1`: 24-bit 68000 address accessed (upper 8 bits are guaranteed to be 0).
 * - `t6`: for I/O write functions, this register will contain the 16-bit value
 *         being written. In 8-bit I/O write functions, the 8-bit value is replicated
 *         two times (just like it happens on the memory bus of a 68000).
 * 
 * These are the expected output registers:
 * 
 * - `k1`: must be cleared to 0 if the assembly handler did not handle the operation.
 *         In this case, the C handler will be called as a fallback.
 * - `t6`: for I/O read functions, this register should contain either the 16-bit
 *         or 8-bit value being read.
 * 
 * Registers `k0`, `k1`, `t6` and `at` can be freely modified and destroyed.
 * All other registers are reserved and should not be modified. If more registers
 * are needed, it is possible to save them on the stack in slots from 40(sp) to
 * 120(sp) which are free at the point of call.
 * 
 * After the handler is done, it can return to the caller using the standard
 * `jr ra` instruction.
 * 
 * It is possible to register up to 4 handlers: read/write 8-bit and read/write
 * 16-bit. If a handler is not needed, it can be set to NULL. Notice that
 * 32-bit handlers are not supported as the 68000 only does 16-bit memory
 * transactions. 32-bit operations are split into two subsequent 16-bit memory
 * transactions (normally the high word / lower address first, but the actual
 * order is opcode dependent).
 * 
 * @param m64k              The m68k context
 * @param asm_io_read16     Pointer to the assembly I/O 16-bit read handler (can be NULL)
 * @param asm_io_write16    Pointer to the assembly I/O 16-bit write handler (can be NULL)
 * @param asm_io_read8      Pointer to the assembly I/O 8-bit read handler (can be NULL)
 * @param asm_io_write8     Pointer to the assembly I/O 8-bit write handler (can be NULL)
 * 
 * @see #m64k_map_io
 */
void m64k_set_mmio_fast_handlers(m64k_t *m64k,
    void *asm_io_read16, void *asm_io_write16,
    void *asm_io_read8, void *asm_io_write8);

/**
 * @brief Configure the I/O handlers, written in C.
 * 
 * This function allows to configure the MMIO standard handlers that will be
 * used by the core. These handlers are called for all addresses that are not
 * otherwise mapped as memory (via #m64k_map_memory), and even for write accesses
 * to read-only memory. No attempt at address decoding is made: the full address
 * is passed to the handler.
 * 
 * There are two handlers: one for memory reads and one for memory writes. The
 * handlers are called with the following parameters:
 * 
 * - `address`: 24-bit 68000 address accessed (upper 8 bits are guaranteed to be 0).
 * - `value`: for I/O write functions, this parameter will contain the 16-bit value
 *            being written. In 8-bit I/O writes, the 8-bit value is replicated
 *            two times (just like it happens on the memory bus of a 68000).
 * - `sz`: size of the access (either 1 or 2).
 * 
 * The read handler must return the 8-bit or 16-bit value read from the MMIO.
 * 
 * Calling the handlers is unfortunately a bit expensive as it requires a complex
 * context switch from the optimized context in which the interpreter runs (most
 * registers must be saved into the stack). For this reason, it is suggested
 * to implement the most performance-critical I/O handlers in assembly, and
 * register them using #m64k_set_mmio_fast_handlers. Fast handlers use
 * a special ABI that is more efficient to call for the interpreter core.
 * 
 * @param m64k      The m68k context
 * @param read      Pointer to the I/O read handler (can be NULL)
 * @param write     Pointer to the I/O write handler (can be NULL)
 * 
 * @see #m64k_set_mmio_fast_handlers
 */
void m64k_set_mmio_handlers(m64k_t *m64k,
    uint32_t (*read)(uint32_t address, int sz),
    void (*write)(uint32_t address, uint16_t value, int sz));


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
