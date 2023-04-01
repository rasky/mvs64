#include "m64k.h"
#include "cycles.h"
#include <libdragon.h>
#include <string.h>

extern int _m64k_asmrun(m64k_t *m64k, int ncycles);

void __m64k_assert_invalid_opcode(uint16_t opcode, uint32_t pc) {
    assertf(0, "Invalid opcode: %04x @ %08lx", opcode, pc);
}

void __m64k_assert_invalid_ea(uint16_t opcode, uint32_t pc) {
    assertf(0, "Invalid EA: %04x @ %08lx", opcode, pc);
}

void m64k_init(m64k_t *m64k)
{
    memset(m64k, 0, sizeof(*m64k));
//    m64k->sr = 0x2700;
}

void m64k_run(m64k_t *m64k, int64_t until)
{
    int c = _m64k_asmrun(m64k, until - m64k->cycles);
    m64k->cycles += until - c;
}
