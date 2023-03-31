#include <libdragon.h>
#include <stdlib.h>
#include <stdalign.h>
#include "m64k.h"
#include "tlb.h"

uint8_t ram_pages[16][4096] alignas(4096);
uint32_t ram_address[16];

static void m68k_ram_init(void) {
    memset(ram_address, 0xFF, sizeof(ram_address));
    tlb_init();
}

static void m68k_ram_w8(uint32_t addr, uint8_t v) {
    uint32_t page = addr & ~0xFFF;
    for (int i=0;i<16;i++) {
        if (ram_address[i] == 0xFFFFFFFF) {
            ram_address[i] = page;
            memset(ram_pages[i], 0, 4096);
            tlb_map_area(i, page, 0xFFF, ram_pages[i], true);
        }
        if (ram_address[i] == page) {
            ram_pages[i][addr & 0xFFF] = v;
            // FIXME: is this really required? Check on real hardware
            data_cache_hit_writeback_invalidate(&ram_pages[i][addr&0xFFF], 1);
            return;
        }
    }
    assertf(0, "Out of RAM pages");
}

static uint8_t m68k_ram_r8(uint32_t addr) {
    uint32_t page = addr & ~0xFFF;
    for (int i=0;i<16;i++) {
        if (ram_address[i] == page) {
            return ram_pages[i][addr & 0xFFF];
        }
        if (ram_address[i] == 0xFFFFFFFF) {
            ram_address[i] = page;
            memset(ram_pages[i], 0, 4096);
            tlb_map_area(i, page, 0xFFF, ram_pages[i], true);
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
    uint32_t ram[32][2];
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
    assertf(s->nrams <= 32, "Too many RAM entries in test: %ld", s->nrams);
    for (int i=0; i<s->nrams; i++) {
        fread(s->ram[i], 1, 8, f);
    }
}

void run_testsuite(const char *fn)
{
    debugf("Running testsuite: %s\n", fn);
    FILE *f = asset_fopen(fn);

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
        debugf("Running test: %s\n", name);

        uint32_t cycles; fread(&cycles, 1, 4, f);
        test_state_t initial, final;
        read_state(f, &initial);
        read_state(f, &final);

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
            debugf("RAM[%lx] = %02lx\n", addr, value);
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
                debugf("D%d: %08lx != %08lx\n", i, m64k.dregs[i], final.dregs[i]);
                failed = true;
            }
            if (i<7 && m64k.aregs[i] != final.aregs[i])  {
                debugf("A%d: %08lx != %08lx\n", i, m64k.dregs[i], final.dregs[i]);
                failed = true;
            }
        }
        if (m64k.usp != final.usp) {
            debugf("USP: %08lx != %08lx\n", m64k.usp, final.usp);
            failed = true;
        }
        if (m64k.ssp != final.ssp) {
            debugf("SSP: %08lx != %08lx\n", m64k.ssp, final.ssp);
            failed = true;
        }
        if (m64k.sr != final.sr) {
            debugf("SR: %08lx != %08lx\n", m64k.sr, final.sr);
            failed = true;
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

    for (int i=0; i<num_tests; i++) {
        run_testsuite(testfns[i]);
    }
    debugf("Finished testsuite\n");
}
