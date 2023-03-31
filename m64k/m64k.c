#include "m64k.h"
#include <libdragon.h>
#include <string.h>

extern uint32_t _m64k_dregs[8];
extern uint32_t _m64k_aregs[8];
extern int32_t  _m64k_cycles;
extern uint32_t _m64k_pc;
extern uint32_t _m64k_sr;

extern void _m64k_asmrun(void);

void __m64k_assert_invalid_opcode(uint16_t opcode, uint32_t pc) {
    assertf(0, "Invalid opcode: %04x @ %08lx", opcode, pc);
}

void m64k_init(m64k_t *m64k)
{
    memset(m64k, 0, sizeof(*m64k));
//    m64k->sr = 0x2700;
}

void m64k_run(m64k_t *m64k, int64_t until)
{
    memcpy(_m64k_dregs, m64k->dregs, sizeof(m64k->dregs));
    memcpy(_m64k_aregs, m64k->aregs, sizeof(m64k->aregs));
    _m64k_aregs[7] = m64k->usp;
    _m64k_pc = m64k->pc;
    _m64k_cycles = 1;
    _m64k_sr = m64k->sr;
    
    _m64k_asmrun();

    memcpy(m64k->dregs, _m64k_dregs, sizeof(m64k->dregs));
    memcpy(m64k->aregs, _m64k_aregs, sizeof(m64k->aregs));
    m64k->usp = _m64k_aregs[7];
    m64k->pc = _m64k_pc;
    m64k->sr = _m64k_sr;
}
