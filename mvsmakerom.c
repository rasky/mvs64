#include <stdio.h>
#include <memory.h>
#include <ctype.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h> // mkdir
#include "miniz.h"

#define panic(s, ...) ({ fprintf(stderr, s, ##__VA_ARGS__); exit(1); })
#define strstartwith(s, prefix) !strncmp(s, prefix, strlen(prefix))

typedef struct {
	char *fn[32];
	int size[32];
	int total_size;
} Romset;

typedef struct {
	int code;
	uint8_t *PROM; int prom_size;
	uint8_t *CROM; int crom_size;
	uint8_t *SROM; int srom_size;
} Game;

typedef struct {
	uint8_t *PROM; int prom_size;
	uint8_t *SROM; int srom_size;
} Bios;

void fixrom_preprocess(uint8_t *rom, int sz) {
	#define NIBBLE_SWAP(v_) ({ uint8_t v = (v_); (((v)>>4) | ((v)<<4)); })
	uint8_t buf[4*8];

	for (int i=0; i<sz; i+=4*8) {
		uint8_t *c0 = &rom[16], *c1 = &rom[24], *c2 = &rom[0], *c3 = &rom[8];
		uint8_t *d = buf;
		for (int j=0;j<8;j++) {
			*d++ = NIBBLE_SWAP(*c0++);
			*d++ = NIBBLE_SWAP(*c1++);
			*d++ = NIBBLE_SWAP(*c2++);
			*d++ = NIBBLE_SWAP(*c3++);
		}
		memcpy(rom, buf, 4*8);
		rom += 4*8;
	}
}

void crom_preprocess(uint8_t *rom0, int sz) {
	uint8_t *buf0 = calloc(1, sz), *buf = buf0, *rom = rom0;
	uint8_t *c1 = rom, *c2 = rom+sz/2;
	
	for (int i=0;i<sz;i+=8*16) {
		for (int b=0;b<4;b++) {
			uint8_t *dst = buf + (b&1)*64 + ((b^2)&2)*2;
			for (int y=0;y<8;y++) {
				for (int x=0;x<8;x+=2) {
					uint8_t px4 = (c1[0] >> (x)) & 1;
					uint8_t px5 = (c1[1] >> (x)) & 1;
					uint8_t px6 = (c2[0] >> (x)) & 1;
					uint8_t px7 = (c2[1] >> (x)) & 1;
					uint8_t px0 = (c1[0] >> (x+1)) & 1;
					uint8_t px1 = (c1[1] >> (x+1)) & 1;
					uint8_t px2 = (c2[0] >> (x+1)) & 1;
					uint8_t px3 = (c2[1] >> (x+1)) & 1;
					dst[x/2] = (px7<<7)|(px6<<6)|(px5<<5)|(px4<<4)|(px3<<3)|(px2<<2)|(px1<<1)|(px0<<0);
				}
				c1 += 2; c2 += 2;
				dst += 8;
			}
		}
		buf += 8*16;
	}

	memcpy(rom0, buf0, sz);
	free(buf0);
}

void chfn(char *path, char* fn) {
	int i = strlen(path);
	while (--i >= 0) if (path[i] == '/' || path[i] == '\\') break;
	strcpy(path+i+1, fn);
}

void chext(char *fn, char* ext) {
	int i = strlen(fn);
	while (--i >= 0) if (fn[i] == '.') break;
	assert(i >= 0);
	strcpy(fn+i, ext);
}

bool stranyprefix(const char *line, const char **prefixes) {
	while (*prefixes) {
		if (strstartwith(line, *prefixes))
			return true;
		prefixes++;
	}
	return false;
}

void byteswap(uint8_t *p0, int s0, uint8_t *p1, int s1, int sz) {
	for (int i=0;i<sz;i++) {
		uint8_t v = *p0;
		*p0 = *p1;
		*p1 = v;
		p0+=s0; p1+=s1;
	}
}

uint8_t* readall(const char *fn, int *sz) {
	FILE *f = fopen(fn, "rb"); if (!f) panic("error: cannot open: %s\n", fn);
	fseek(f, 0, SEEK_END);
	*sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *ROM = malloc(*sz);
	fread(ROM, 1, *sz, f);
	fclose(f);
	return ROM;
}

void saveto(uint8_t *data, int size, const char *fn) {
	FILE *f = fopen(fn, "wb"); if (!f) panic("error: cannot create: %s", fn);
	fwrite(data, 1, size, f);
	fclose(f);
}

bool is_bios(char *fn) {
	static const char *bios[] = { "sp-", "sp1.", "uni-bios", "asia-", "japan-", "sfix.", "sm1.", NULL };
	return stranyprefix(fn, bios);
}

int romtype(char *fn, char ch) {
	char *pt = fn-1;
	while ((pt = strchr(pt+1, ch))) {
		if (pt[1] >= '1' && pt[1] <= '9')
			return pt[1]-'0';
	}
	return 0;
}

void romset_add(Romset *r, int idx, mz_zip_archive_file_stat *stat) {
	if (idx >= sizeof(r->fn)/sizeof(r->fn[0])) panic("invalid ROM index: %d (%s)\n", idx, stat->m_filename);
	if (r->fn[idx-1]) panic("duplicated rom at %d: %s %s\n", idx, r->fn[idx-1], stat->m_filename);
	r->fn[idx-1] = strdup(stat->m_filename);
	r->size[idx-1] = stat->m_uncomp_size;
	r->total_size += stat->m_uncomp_size;
}

int romset_count(Romset *r) {
	int count = 0;
	while (r->fn[count]) count++;
	return count;
}

#define ROMSET_LOAD_ODDEVEN  1

uint8_t* romset_load(Romset *r, mz_zip_archive *zip, int flags) {
	int cnt = romset_count(r);
	uint8_t *ROM = malloc(r->total_size);

	int offset = 0;
	for (int i=0;i<cnt;i++) {
		int idx = i;
		if (flags & ROMSET_LOAD_ODDEVEN) {
			idx *=2; if (idx >= cnt) idx -= cnt-1;
		}
		if (!mz_zip_reader_extract_file_to_mem(zip, r->fn[idx], ROM+offset, r->total_size-offset, 0))
			panic("%s\n", mz_zip_get_error_string(mz_zip_get_last_error(zip)));
		offset += r->size[idx];
	}
	assert(offset == r->total_size);
	return ROM;
}

void load_bios(const char *fn, Bios *bios) {
	bios->PROM = readall(fn, &bios->prom_size);
	byteswap(bios->PROM, 2, bios->PROM+1, 2, bios->prom_size/2);

	char *sfix = strdup(fn);
	chfn(sfix, "sfix.sfix");

	bios->SROM = readall(sfix, &bios->srom_size);
	fixrom_preprocess(bios->SROM, bios->srom_size);
}

void load_game(const char *fn, Game *game) {
	mz_zip_archive zip;
	mz_zip_zero_struct(&zip);
	memset(game, 0, sizeof(*game));

	if (!mz_zip_reader_init_file(&zip, fn, 0)) panic("%s\n", mz_zip_get_error_string(mz_zip_get_last_error(&zip)));

	Romset P, C, S;

	memset(&P, 0, sizeof(Romset));
	memset(&C, 0, sizeof(Romset));
	memset(&S, 0, sizeof(Romset));

	for (int index = 0;;index++) {
		mz_zip_archive_file_stat stat;
		if (!mz_zip_reader_file_stat(&zip, index, &stat)) break;
		if (stat.m_is_directory) continue;

		char *fn = stat.m_filename;
		for (int i=0;stat.m_filename[i];i++) fn[i] = tolower(fn[i]);

		int idx;
		if (is_bios(fn)) continue;
		if ((idx = romtype(fn, 'p'))) romset_add(&P, idx, &stat);
		if ((idx = romtype(fn, 'g'))) romset_add(&P, idx, &stat); // some PROMs are called pg1/pg2
		if ((idx = romtype(fn, 's'))) romset_add(&S, idx, &stat);
		if ((idx = romtype(fn, 'c'))) romset_add(&C, idx, &stat);
	}

	int num_croms = romset_count(&C);
	if (!num_croms) panic("error: no CROM files found\n");
	if (num_croms & 1) panic("error: odd number of CROM files found: %d\n", num_croms);

	int num_sroms = romset_count(&S);
	if (num_sroms != 1) panic("error: invalid number of SROM files found: %d\n", num_sroms);

	int num_proms = romset_count(&P);
	if (!num_proms) panic("error: no PROM files found\n");

	// Load and preprocess CROM roms
	game->CROM = romset_load(&C, &zip, ROMSET_LOAD_ODDEVEN);
	crom_preprocess(game->CROM, C.total_size);
	game->crom_size = C.total_size;

	// Load and preprocess SROM roms
	game->SROM = romset_load(&S, &zip, 0);
	fixrom_preprocess(game->SROM, S.total_size);
	game->srom_size = S.total_size;

	// Load PROMs
	game->PROM = romset_load(&P, &zip, 0);
	byteswap(game->PROM, 2, game->PROM+1, 2, P.total_size/2);
	game->prom_size = P.total_size;

	// In some cases, the single PROM has the two halves inverted.
	if (num_proms == 1) {
		if (memcmp(game->PROM+P.total_size/2+0x100, "NEO-GEO", 7)==0)
			byteswap(game->PROM, 1, game->PROM+P.total_size/2, 1, P.total_size/2);
	}

	if (memcmp(game->PROM+0x100, "NEO-GEO", 7)) panic("error: cannot detect PROM layout\n");

	game->code = ((int)game->PROM[0x108] << 8) | game->PROM[0x109];
}

void patch_game(Game *game) {
	switch (game->code) {
	case 0x44: // aof
		byteswap(game->CROM+2*1024*1024, 1, game->CROM+4*1024*1024, 1, 2*1024*1024);
		break;
	}
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		fprintf(stderr, "MVS64 ROM conversion tool\n\n");
		fprintf(stderr, "Usage:\n");
		fprintf(stderr, "   mvsmakerom <bios> <game.zip>\n");
		fprintf(stderr, "\n");
		fprintf(stderr, "Notes:\n");
		fprintf(stderr, "  * <bios> must be a valid NeoGeo BIOS (original or homebrew)\n");
		fprintf(stderr, "  * Make sure sfix.sfix is in the same directory of the BIOS\n");
		exit(2);
	}

	Bios bios;
	load_bios(argv[1], &bios);

	Game game;
	load_game(argv[2], &game);

	patch_game(&game);

	char outfn[strlen(argv[2])+16];
	strcpy(outfn, argv[2]);
	chext(outfn, ".n64");
	mkdir(outfn, 0777);
	strcat(outfn, "/?.rom");

	int off = strlen(outfn)-5;

	outfn[off] = 'p';
	saveto(game.PROM, game.prom_size, outfn);

	outfn[off] = 'c';
	saveto(game.CROM, game.crom_size, outfn);

	outfn[off] = 's';
	saveto(game.SROM, game.srom_size, outfn);

	strcpy(outfn+off, "?.bios");

	outfn[off] = 'p';
	saveto(bios.PROM, bios.prom_size, outfn);

	outfn[off] = 's';
	saveto(bios.SROM, bios.srom_size, outfn);
}
