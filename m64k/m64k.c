#include "m64k.h"
#include "m64k_internal.h"
#include "cycles.h"
#include <libdragon.h>
#include <string.h>
#include "tlb.h"

#if M64K_CONFIG_LOG_EXCEPTIONS
#define logexcf(...)    debugf(__VA_ARGS__)
#else
#define logexcf(...)    ({ })
#endif

#define SR_T0   0x8000  // Trace 0
#define SR_T1   0x4000  // Trace 1
#define SR_S    0x2000  // Supervisor mode
#define SR_M    0x1000  // Master/interrupt state
#define SR_INT  0x0700  // Interrupt mask

// Convert a m68k address (24 bit) to a pointer in the N64 memory space
#define M64K_PTR(a)   ((void*)(((a) & 0x00FFFFFF) + M64K_CONFIG_MEMORY_BASE))

#define RM16(a)     (*(  uint16_t*)M64K_PTR(a))
#define RM32(a)     (*(u_uint32_t*)M64K_PTR(a))
#define WM32(a, v)  (*(u_uint32_t*)M64K_PTR(a) = (v))
#define WM16(a, v)  (*(  uint16_t*)M64K_PTR(a) = (v))

extern int _m64k_asmrun(m64k_t *m64k, int ncycles);

void __m64k_assert_invalid_opcode(uint16_t opcode, uint32_t pc) {
    assertf(0, "Invalid opcode: %04x @ %08lx", opcode, pc);
}

void __m64k_assert_invalid_opmode(uint16_t opcode, uint32_t pc) {
    assertf(0, "Invalid opmode: %04x @ %08lx", opcode, pc);
}

void __m64k_assert_invalid_ea(uint16_t opcode, uint32_t pc) {
    assertf(0, "Invalid EA: %04x @ %08lx", opcode, pc);
}

void __m64k_assert_privilege_violation(uint16_t opcode, uint32_t pc) {
    assertf(0, "unimplemented: privilege violation error: %04x @ %08lx", opcode, pc);
}

static inline void exc_push32(m64k_t *m64k, uint32_t v)
{
    m64k->ssp -= 4;
    WM32(m64k->ssp, v);
}

static inline void exc_push16(m64k_t *m64k, uint16_t v)
{
    m64k->ssp -= 2;
    WM16(m64k->ssp, v);
}

void m64k_init(m64k_t *m64k)
{
    memset(m64k, 0, sizeof(*m64k));
    m64k->sr = 0x2700;
    __m64k_tlb_reset(); // FIXME: this clears all TLB entries
}

void m64k_pulse_reset(m64k_t *m64k)
{
    m64k->sr  = 0x2700;
    m64k->ssp = RM32(0);
    m64k->pc  = RM32(4);
}

void m64k_exception_address(m64k_t *m64k, uint32_t address, uint16_t fc)
{
    uint32_t oldsr = m64k->sr;

    m64k->sr &= ~(SR_T0 | SR_T1);
    m64k->sr |= SR_S;

    exc_push32(m64k, m64k->pc);  // cdef
    exc_push16(m64k, oldsr);     // ab
    exc_push16(m64k, m64k->ir);  // 89
    exc_push32(m64k, address);   // 4567
    exc_push16(m64k, fc);        // 23

    m64k->pc = RM32(m64k->vbr + 0x3*4);
    m64k->cycles += __m64k_exception_cycle_table[0x3];
}

void m64k_exception_divbyzero(m64k_t *m64k)
{
    uint32_t oldsr = m64k->sr;

    m64k->sr &= ~(SR_T0 | SR_T1);
    m64k->sr |= SR_S;

    exc_push32(m64k, m64k->pc);
    exc_push16(m64k, oldsr);

    m64k->pc = RM32(m64k->vbr + 0x5*4);
    m64k->cycles += __m64k_exception_cycle_table[0x5];
}

void m64k_exception_interrupt(m64k_t *m64k, int level)
{
    if (m64k->hook_irqack) {
        m64k->hook_irqack(m64k->hook_irqack_ctx, level);
    } else {
        // Auto-ack the interrupt for simpler cases
        if (m64k->virq & (1<<(level-1)))
            m64k_set_virq(m64k, level, false);
        else
            m64k_set_irq(m64k, 0);
    }

    uint32_t oldsr = m64k->sr;

    m64k->sr &= ~(SR_T0 | SR_T1 | SR_INT);
    m64k->sr |= SR_S | (level << 8);

    uint32_t pc = RM32(m64k->vbr + (24 + level)*4);
    if (pc == 0)
        pc = RM32(m64k->vbr + 15*4);

    exc_push32(m64k, m64k->pc);
    exc_push16(m64k, oldsr);

    m64k->pc = pc;
    m64k->cycles += __m64k_exception_cycle_table[24 + level];
}

int64_t m64k_run(m64k_t *m64k, int64_t until)
{
    while (until > m64k->cycles) {
        int timeslice = until - m64k->cycles;
        int remaining = _m64k_asmrun(m64k, timeslice);
        m64k->cycles += timeslice - remaining;

        if (__builtin_expect(m64k->pending_exc[0] != 0, 0)) {
            switch (m64k->pending_exc[0]) {
            #if M64K_CONFIG_ADDRERR
            case M64K_PENDINGEXC_ADDRERR:
                logexcf("[m64k] address error\n");
                m64k_exception_address(m64k, m64k->pending_exc[1], m64k->pending_exc[2]);
                break;
            #endif
            case M64K_PENDINGEXC_RSTO:
                logexcf("[m64k] RSTO asserted\n");
                break;
            case M64K_PENDINGEXC_DIVBYZERO:
                logexcf("[m64k] division by zero\n");
                m64k_exception_divbyzero(m64k);
                break;
            case M64K_PENDINGEXC_IRQ:
                m64k_exception_interrupt(m64k, m64k->pending_exc[1]);
                break;
            default:
                assertf(0, "Unhandled pending exception: %ld", m64k->pending_exc[0]);
            }
            m64k->pending_exc[0] = 0;
        }
    }

    return m64k->cycles;
}

void m64k_set_irq(m64k_t *m64k, int level)
{
    if (level == 7 && m64k->ipl < 7) {
        m64k->nmi_pending = 1;
    }
    m64k->ipl = level;
    m64k->check_interrupts = 1;
}

void m64k_set_virq(m64k_t *m64k, int irq, bool on)
{
    assertf(irq > 0 && irq <= 7, "Invalid IRQ: %d", irq);
    if (on)
        m64k->virq |= 1 << (irq-1);
    else
        m64k->virq &= ~(1 << (irq-1));

    int i;
    for (i=7; i>0; i--) {
        if (m64k->virq & (1 << (i-1)))
            break;
    }
    m64k_set_irq(m64k, i);
}

uint32_t m64k_get_pc(m64k_t *m64k)
{
    // FIXME: fix while m68k is running
    return m64k->pc;
}

int64_t m64k_get_clock(m64k_t *m64k)
{
    if (!m64k->ts_start)
        return m64k->cycles;
    return m64k->cycles + (m64k->ts_start - m64k->ts_cur);
}

void m64k_run_stop(m64k_t *m64k)
{
    m64k->check_interrupts = 2;
}

void m64k_set_hook_irqack(m64k_t *m64k, int (*hook)(void *ctx, int level), void *ctx)
{
    m64k->hook_irqack = hook;
    m64k->hook_irqack_ctx = ctx;
}

m64k_mapping_t m64k_map_memory(m64k_t *m64k, uint32_t address, uint32_t size, void *ptr, bool writable)
{
    assertf(address < 0x1000000, "address must be in the 24-bit range");
    assertf((size & (size-1)) == 0, "size must be a power of 2");
    assertf(size >= 0x1000, "size must be at least 4 KiB");

    int flags = TLBF_OVERWRITE;
    if (!writable)
        flags |= TLBF_READONLY;

    void *virt = (void*)(M64K_CONFIG_MEMORY_BASE | address);
    uint32_t phys = PhysicalAddr(ptr);
    return __m64k_tlb_add(virt, size-1, phys, flags);
}

void m64k_map_memory_change(m64k_t *m64k, m64k_mapping_t mapping, void *ptr, bool writable)
{
    __m64k_tlb_change(mapping, PhysicalAddr(ptr), writable ? 0 : TLBF_READONLY);
}

void m64k_unmap_memory(m64k_t *m64k, m64k_mapping_t mapping)
{
    __m64k_tlb_rem(mapping);
}
