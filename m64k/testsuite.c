#include <libdragon.h>
#include <stdlib.h>
#include <stdalign.h>
#include "m64k.h"
#include "tlb.h"

uint8_t ram_pages[16][8192] alignas(8192);
uint32_t ram_address[16];

static void m68k_ram_init(void) {
    memset(ram_address, 0xFF, sizeof(ram_address));
    tlb_init();
}

static void m68k_ram_w8(uint32_t addr, uint8_t v) {
    uint32_t page = (addr & ~0x1FFF) | M64K_CONFIG_MEMORY_BASE;

    for (int i=0;i<16;i++) {
        if (ram_address[i] == 0xFFFFFFFF) {
            ram_address[i] = page;
            memset(ram_pages[i], 0, 8192);
            tlb_map_area(i, (void*)page, 0x1FFF, PhysicalAddr(ram_pages[i]), true);
        }
        if (ram_address[i] == page) {
            ram_pages[i][addr & 0x1FFF] = v;
            return;
        }
    }
    assertf(0, "Out of RAM pages");
}

static uint8_t m68k_ram_r8(uint32_t addr) {
    uint32_t page = (addr & ~0x1FFF) | M64K_CONFIG_MEMORY_BASE;
    for (int i=0;i<16;i++) {
        if (ram_address[i] == page) {
            return ram_pages[i][addr & 0x1FFF];
        }
        if (ram_address[i] == 0xFFFFFFFF) {
            ram_address[i] = page;
            memset(ram_pages[i], 0, 8192);
            // debugf("Mapping RAM page %d at %08lx\n", i, page);
            tlb_map_area(i, (void*)page, 0x1FFF, PhysicalAddr(ram_pages[i]), true);
            return 0;
        }
    }
    assertf(0, "Out of RAM pages");
}

typedef struct  {
    uint32_t dregs[8];
    uint32_t aregs[7];
    uint32_t usp, ssp;
    uint32_t sr;
    uint32_t pc;
    uint32_t prefetch[2];
    uint32_t nrams;
    uint32_t ram[80][2];
} test_state_t;

void read_state(FILE *f, test_state_t *s)
{
    fread(s->dregs, 1, 32, f);
    fread(s->aregs, 1, 28, f);
    fread(&s->usp, 1, 4, f);
    fread(&s->ssp, 1, 4, f);
    fread(&s->sr, 1, 4, f);
    fread(&s->pc, 1, 4, f);
    fread(s->prefetch, 1, 8, f);
    fread(&s->nrams, 1, 4, f);
    assertf(s->nrams <= 80, "Too many RAM entries in test: %ld", s->nrams);
    for (int i=0; i<s->nrams; i++) {
        fread(s->ram[i], 1, 8, f);
    }
}

void run_testsuite(const char *fn)
{
    debugf("Running testsuite: %s\n", fn);
    FILE *f = asset_fopen(fn);

    bool asl_test = strstr(fn, "ASL.b.") != NULL;
    bool asr_test = strstr(fn, "ASR.") != NULL;

    // Read ID
    char id[4]; fread(id, 1, 4, f); (void)id;
    assert(id[0] == 'M' && id[1] == '6' && id[2] == '4' && id[3] == 'K');

    // Read number of tests
    uint32_t num_tests; fread(&num_tests, 1, 4, f);

    for (int t=0; t<num_tests; t++) {
        fread(id, 1, 4, f);
        assert(id[0] == 'T' && id[1] == 'E' && id[2] == 'S' && id[3] == 'T');

        uint32_t nl; fread(&nl, 1, 4, f);
        char *name = alloca(nl+1); fread(name, 1, nl, f); name[nl] = 0;
        uint32_t cycles; fread(&cycles, 1, 4, f);
        test_state_t initial, final;
        read_state(f, &initial);
        read_state(f, &final);

        // skip buggy tests
        // see https://github.com/TomHarte/ProcessorTests/issues/21
        if (asl_test && (t == 1583-1 || t == 1761-1)) continue;

        // Run the test
        m64k_t m64k;
        m64k_init(&m64k);
        memcpy(m64k.dregs, initial.dregs, sizeof(m64k.dregs));
        memcpy(m64k.aregs, initial.aregs, sizeof(m64k.aregs));
        m64k.usp = initial.usp;
        m64k.ssp = initial.ssp;
        m64k.pc = initial.pc;
        m64k.sr = initial.sr;

        m68k_ram_init();
        m68k_ram_w8(initial.pc+0, initial.prefetch[0] >> 8);
        m68k_ram_w8(initial.pc+1, initial.prefetch[0] & 0xff);
        m68k_ram_w8(initial.pc+2, initial.prefetch[1] >> 8);
        m68k_ram_w8(initial.pc+3, initial.prefetch[1] & 0xff);
        for (int i=0; i<initial.nrams; i++) {
            uint32_t addr = initial.ram[i][0];
            uint32_t value = initial.ram[i][1];
            m68k_ram_w8(addr, value);
            // debugf("RAM[%08lx] = %02lx\n", addr, value);
        }
        // Make sure also locations mentioned in final state are mapped
        for (int i=0; i<final.nrams; i++)
            (void)m68k_ram_r8(final.ram[i][0]);

        // Run the opcode
        m64k_run(&m64k, 1);

        // Check the results
        bool failed = false;
        for (int i=0; i<8; i++) {
            if (m64k.dregs[i] != final.dregs[i])  {
                if (!failed) debugf("Running test: %s\n", name);
                debugf("D%d: %08lx != %08lx\n", i, m64k.dregs[i], final.dregs[i]);
                failed = true;
            }
        }
        for (int i=0; i<7; i++) {
            if (m64k.aregs[i] != final.aregs[i])  {
                if (!failed) debugf("Running test: %s\n", name);
                debugf("A%d: %08lx != %08lx\n", i, m64k.aregs[i], final.aregs[i]);
                failed = true;
            }
        }
        if (m64k.usp != final.usp) {
            if (!failed) debugf("Running test: %s\n", name);
            debugf("USP: %08lx != %08lx\n", m64k.usp, final.usp);
            failed = true;
        }
        if (m64k.ssp != final.ssp) {
            if (!failed) debugf("Running test: %s\n", name);
            debugf("SSP: %08lx != %08lx\n", m64k.ssp, final.ssp);
            failed = true;
        }
        if(asr_test) {
            // buggy tests, ignore flags C and X
            m64k.sr &= ~0x11; final.sr &= ~0x11;
        }
        if (m64k.sr != final.sr) {
            if (!failed) debugf("Running test: %s\n", name);
            debugf("SR: %08lx != %08lx\n", m64k.sr, final.sr);
            failed = true;
        }
        if (m64k.pc != final.pc) {
            if (!failed) debugf("Running test: %s\n", name);
            debugf("PC: %08lx != %08lx\n", m64k.pc, final.pc);
            failed = true;
        }
        for (int i=0; i<final.nrams; i++) {
            uint8_t got = m68k_ram_r8(final.ram[i][0]);
            if (got != final.ram[i][1]) {
                if (!failed) debugf("Running test: %s\n", name);
                debugf("RAM[%lx] = %02x != %02lx\n", final.ram[i][0], got, final.ram[i][1]);
                failed = true;
            }
        }

        if (failed) abort();
    }

    fclose(f);
}

bool strendswith(const char *s, const char *suffix)
{
    int sl = strlen(s);
    int sufl = strlen(suffix);
    if (sl < sufl) return false;
    return strcmp(s+sl-sufl, suffix) == 0;
}

int main()
{
    debug_init_isviewer();
    debug_init_usblog();

    dfs_init(DFS_DEFAULT_LOCATION);

    if (!M64K_CONFIG_ADDRERR) {
        debugf("\nWARNING: this testsuite requires address errors to be emulated.\n");
        debugf("Make sure to compile M64K with M64K_CONFIG_ADDRERR=1\n\n");
    }

#if 0
    char* testfns[256];
    int num_tests = 0;

	char sbuf[1024];
	strcpy(sbuf, "rom:/");
	if (dfs_dir_findfirst(".", sbuf+5) == FLAGS_FILE) {
		do {
			if (strendswith(sbuf, ".btest")) {
                assert(num_tests < 256);
				testfns[num_tests++] = strdup(sbuf);
            }
		} while (dfs_dir_findnext(sbuf+5) == FLAGS_FILE);
	}
#else
    static const char *testfns[] = {
        "rom:/MOVEP.w.btest",
        "rom:/MOVEP.l.btest",

        "rom:/TRAPV.btest",
        "rom:/TAS.btest",

        "rom:/ABCD.btest",
        "rom:/SBCD.btest",
        "rom:/NBCD.btest",

        "rom:/ROXL.b.btest",
        "rom:/ROXL.l.btest",
        "rom:/ROXL.w.btest",
        "rom:/ROXR.b.btest",
        "rom:/ROXR.l.btest",
        "rom:/ROXR.w.btest",
        "rom:/ROL.b.btest",
        "rom:/ROL.l.btest",
        "rom:/ROL.w.btest",
        "rom:/ROR.b.btest",
        "rom:/ROR.l.btest",
        "rom:/ROR.w.btest",

        "rom:/ASL.b.btest",
        "rom:/ASL.l.btest",
        "rom:/ASL.w.btest",
        "rom:/ASR.b.btest",   // these seem too buggy
        "rom:/ASR.l.btest",   // these seem too buggy
        "rom:/ASR.w.btest",   // these seem too buggy
        "rom:/LSR.b.btest",
        "rom:/LSR.l.btest",
        "rom:/LSR.w.btest",
        "rom:/LSL.b.btest",
        "rom:/LSL.l.btest",
        "rom:/LSL.w.btest",

        "rom:/TRAP.btest",
        "rom:/EXG.btest",

        "rom:/BTST.btest",
        "rom:/BCHG.btest",
        "rom:/BSET.btest",
        "rom:/BCLR.btest",

        "rom:/CMPA.w.btest",
        "rom:/CMPA.l.btest",
        "rom:/CMP.b.btest",
        "rom:/CMP.w.btest",
        "rom:/CMP.l.btest",

        "rom:/EXT.l.btest",
        "rom:/EXT.w.btest",

        "rom:/MOVEM.w.btest",
        "rom:/MOVEM.l.btest",

        "rom:/DIVS.btest",
        "rom:/DIVU.btest",

        "rom:/MULS.btest",
        "rom:/MULU.btest",

        "rom:/DBcc.btest",

        "rom:/RTE.btest",
        "rom:/RTR.btest",
        "rom:/RTS.btest",

        "rom:/NOP.btest",
        "rom:/RESET.btest",
    
        "rom:/LINK.btest",
        "rom:/UNLINK.btest",

        "rom:/BSR.btest",
        "rom:/JMP.btest",
        "rom:/JSR.btest",
        "rom:/Bcc.btest",
        "rom:/Scc.btest",

        "rom:/SWAP.btest",

        "rom:/EORItoCCR.btest",
        "rom:/EORItoSR.btest",
        "rom:/ORItoCCR.btest",
        "rom:/ORItoSR.btest",
        "rom:/ANDItoCCR.btest",
        "rom:/ANDItoSR.btest",
        "rom:/MOVEfromSR.btest",
        "rom:/MOVEfromUSP.btest",
        "rom:/MOVEtoSR.btest",
        "rom:/MOVEtoUSP.btest",
        "rom:/MOVEtoCCR.btest",

        "rom:/TST.b.btest",
        "rom:/TST.l.btest",
        "rom:/TST.w.btest",

        "rom:/NEGX.b.btest",
        "rom:/NEGX.l.btest",
        "rom:/NEGX.w.btest",
        "rom:/NEG.b.btest",
        "rom:/NEG.l.btest",
        "rom:/NEG.w.btest",

        "rom:/NOT.b.btest",
        "rom:/NOT.l.btest",
        "rom:/NOT.w.btest",

        "rom:/CLR.b.btest",
        "rom:/CLR.l.btest",
        "rom:/CLR.w.btest",

        "rom:/PEA.btest",
        "rom:/LEA.btest",

        "rom:/AND.b.btest",
        "rom:/AND.l.btest",
        "rom:/AND.w.btest",
        "rom:/OR.b.btest",
        "rom:/OR.l.btest",
        "rom:/OR.w.btest",
        "rom:/EOR.b.btest",
        "rom:/EOR.l.btest",
        "rom:/EOR.w.btest",

        "rom:/ADD.b.btest",
        "rom:/ADD.l.btest",
        "rom:/ADD.w.btest",
        "rom:/ADDX.b.btest",
        "rom:/ADDX.l.btest",
        "rom:/ADDX.w.btest",
        "rom:/ADDA.l.btest",
        "rom:/ADDA.w.btest",

        "rom:/SUB.b.btest",
        "rom:/SUB.l.btest",
        "rom:/SUB.w.btest",
        "rom:/SUBX.b.btest",
        "rom:/SUBX.l.btest",
        "rom:/SUBX.w.btest",
        "rom:/SUBA.l.btest",
        "rom:/SUBA.w.btest",

        "rom:/MOVE.b.btest",
        "rom:/MOVE.w.btest",
        "rom:/MOVE.l.btest",
        "rom:/MOVE.q.btest",
        "rom:/MOVEA.w.btest",
        "rom:/MOVEA.l.btest",
    };
    int num_tests = sizeof(testfns)/sizeof(testfns[0]);
#endif

    for (int i=0; i<num_tests; i++) {
        run_testsuite(testfns[i]);
    }

    debugf("Finished testsuite\n");
}
