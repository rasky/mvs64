#include <libdragon.h>
#include <stdlib.h>

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
    debugf("Running testsuite: %s", fn);
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
			if (strendswith(sbuf, ".btest"))
				testfns[num_tests++] = strdup(sbuf);
		} while (dfs_dir_findnext(sbuf+5) == FLAGS_FILE);
	}

    for (int i=0; i<num_tests; i++) {
        run_testsuite(testfns[i]);
    }
    debugf("Finished testsuite\n");
}
