#include "m64k.h"
#include "m64k_internal.h"
#include "cycles.h"
#include <libdragon.h>
#include <string.h>

#define SR_T0   0x8000  // Trace 0
#define SR_T1   0x4000  // Trace 1
#define SR_S    0x2000  // Supervisor mode
#define SR_M    0x1000  // Master/interrupt state
#define SR_INT  0x0700  // Interrupt mask

// Convert a m68k address (24 bit) to a pointer in the N64 memory space
#define M64K_PTR(a)   ((void*)(((a) & 0x00FFFFFF) + M68K_CONFIG_MEMORY_BASE))

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

void m64k_exception_address(m64k_t *m64k, uint32_t address, uint16_t fc)
{
    uint32_t oldsr = m64k->sr;

    m64k->sr &= ~(SR_T0 | SR_T1);
    m64k->sr |= SR_S;

    exc_push32(m64k, m64k->pc);
    exc_push16(m64k, oldsr);
    exc_push16(m64k, m64k->ir);
    exc_push32(m64k, address);
    exc_push16(m64k, fc);

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

void m64k_init(m64k_t *m64k)
{
    memset(m64k, 0, sizeof(*m64k));
    m64k->sr = 0x2700;
}

void m64k_run(m64k_t *m64k, int64_t until)
{
    while (until > m64k->cycles) {
        int c = _m64k_asmrun(m64k, until - m64k->cycles);
        m64k->cycles += until - c;

        if (__builtin_expect(m64k->pending_exc[0] != 0, 0)) {
            switch (m64k->pending_exc[0]) {
            #if M64K_CONFIG_ADDRERR
            case M64K_PENDINGEXC_ADDRERR:
                m64k_exception_address(m64k, m64k->pending_exc[1], m64k->pending_exc[2]);
                break;
            #endif
            case M64K_PENDINGEXC_RSTO:
                debugf("[m64k] RSTO asserted\n");
                break;
            case M64K_PENDINGEXC_DIVBYZERO:
                debugf("[m64k] division by zero\n");
                m64k_exception_divbyzero(m64k);
                break;
            default:
                assertf(0, "Unhandled pending exception: %ld", m64k->pending_exc[0]);
            }
            m64k->pending_exc[0] = 0;
        }
    }
}
