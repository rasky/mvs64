#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>

#include "m68k.h"
#include "genhle_68kops.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

const opcode_handler_struct* decode_opcode(uint16_t op) {
	const int len = sizeof(m68k_opcode_handler_table) / sizeof(opcode_handler_struct);

	for (int i=len-2; i>=0; i--) {
		const opcode_handler_struct *s = &m68k_opcode_handler_table[i];
		if ((op & s->mask) == s->match)
			return s;
	}
	fprintf(stderr, "unknown opcode: %02x\n", op);
	exit(1);
}

#define be16(a)  (((uint16_t)((a)[0]) << 8) | (a)[1])
#define be32(a)  (((uint32_t)be16(a) << 16) | be16((a)+2))
#define strstartwith(s, prefix) !strncmp(s, prefix, strlen(prefix))
#define panic(s, ...) ({ fprintf(stderr, s, ##__VA_ARGS__); exit(1); })

bool stranyprefix(const char *line, const char **prefixes) {
	while (*prefixes) {
		if (strstartwith(line, *prefixes))
			return true;
		prefixes++;
	}
	return false;
}

void replace_word(char *s, char *from, char *to, int n) {
	int slen = strlen(s), flen = strlen(from), tlen = strlen(to);
	char *p = s;
	while ((p = strstr(p, from))) {
		if (!isalnum(p[-1]) && !isalnum(p[flen])) {
			int rest = slen-(p-s)-flen;
			memmove(p+tlen, p+flen, rest);
			strncpy(p, to, tlen);
			slen += tlen-flen;
			s[slen] = 0;
			p += tlen;
			if (n>0 && !--n) return;
		} else {
			p += flen;
		}
	}
	if (n>0) panic("replace_word: wrong number of replacements: \"%s\" => \"%s\"\n", from, to);
}

bool find_word_prefix(char *s, char *prefix, char *out) {
	int plen = strlen(prefix);
	while ((s = strstr(s, prefix))) {
		if (!isalnum(s[-1])) {
			char *w = s+plen;
			while (isalnum(*w)) w++;
			strlcpy(out, s+plen, w-(s+plen));
			return true;
		} else {
			s += plen;
		}
	}
	return false;
}

// "Duff device" is a programming pattern (mostly used in assembly) where the
// programmer jumps in the middle of some repeated code using a PC-relative jump.
// An example would be an unrolled memcpy, with the jump selecting the number
// of actual words being copied.
//
//  This is a 68k example:
//
// ROM:0005BB50                 jmp     loc_5BB54(pc,d6.w)
// ROM:0005BB54 loc_5BB54:
// ROM:0005BB54                 move.l  d7,(a4)
// ROM:0005BB56                 add.l   d4,d7
// ROM:0005BB58                 move.l  d7,(a4)
// ROM:0005BB5A                 add.l   d4,d7
// ROM:0005BB5C                 move.l  d7,(a4)
// ROM:0005BB5E                 add.l   d4,d7
// ROM:0005BB60                 move.l  d7,(a4)
// ROM:0005BB62                 add.l   d4,d7
// ROM:0005BB64                 move.l  d7,(a4)
// ROM:0005BB66                 add.l   d4,d7
//                              [.....]
//
// The first jump decides where to jump in the middle of a long sequence of
// move.l+add.l, and only the rest of the sequence will be executed.
//
// Another example:
//
// ROM:0005B542                 jmp     loc_5B546(pc,d7.w)
// ROM:0005B546
// ROM:0005B546 loc_5B546:
// ROM:0005B546                 bra.w   loc_5B556
// ROM:0005B54A                 bra.w   loc_5B962
// ROM:0005B54E                 bra.w   loc_5B75C
// ROM:0005B552                 bra.w   loc_5BC6C
// ROM:0005B556
// ROM:0005B556 loc_5B556:
// ROM:0005B556                 move.w  #$40,d7 ; '@'
// ROM:0005B55A                 add.w   d1,d7
// ROM:0005B55C                 move.w  d7,d5
// ROM:0005B55E                 addq.w  #1,d5
//                              [.....]
//
// Notice that in this case the duff device is made of branch instructions.
// So this is very similar to a standard jump table, but the jump table is
// "inlined" in the code as a sequence of branches. The code does not fetch
// the target address like it would do with a jump table, but simply jump
// in the middle of a sequence, which happens to be branches themselves.
//
// In other words, a duff device is a form of jump table where the target
// addresses are a sequence of linear, equidistant addresses.
//
// To handle the duff device in the recompiler, we want to reconstruct a
// jump table and use computed gotos in the generated code. The hurdle here is
// to understand how long is the duff device, and what is the length of each
// step. To do this, we inspect the instructions searching for a pattern where
// X instructions (up to 4) are repeated in sequence. We don't need the instructions
// to be identical (otherwise, the second case above wouldn't work because the
// opcode also encodes different branch targets), but they need to be "similar
// enough"; we approximate this with "emulated using the same function in the
// interpreter".
bool decode_duffdevice(unsigned char *rom, unsigned int pc, int *ddlen, int *ddstep) {
	char disasm[256]; 

	#define MAX_DD_STEP 4
	uint16_t op[MAX_DD_STEP+1]; const opcode_handler_struct *oph[MAX_DD_STEP+1]; int oplen[MAX_DD_STEP+1];

	unsigned int nextpc = pc;
	*ddlen = -1;
	*ddstep = -1;
	for (int i=0;i<MAX_DD_STEP+1;i++) {
		op[i] = be16(rom + nextpc);
		oplen[i] = m68k_disassemble_raw(disasm, nextpc, rom+nextpc, NULL, M68K_CPU_TYPE_68000);
		oph[i] = decode_opcode(op[i]);

		if (i != 0 && oph[i] == oph[0]) {
			*ddstep = nextpc-pc;
			break;
		}

		nextpc += oplen[i];
	}
	if (*ddstep < 0) return -1;

	// See how long the sequence is. Iterate until we find a different instruction.
	*ddlen = 2;
	while (decode_opcode(be16(rom + pc + *ddlen * *ddstep)) == oph[0]) {
		*ddlen = *ddlen + 1;
		if (*ddlen == 1000) panic("decode_duffdevice: endless sequence at %x", pc);
	}

	return true;
}

// Given an array of numbers, find the perfect hash with the format
//
//     ((num * k0) >> k1) & k2
//
// where k2+1 is a power of two constants. It can be used to construct a hash
// table of k2+1 elements where the numbers hash perfectly (with no conflicts).
bool find_perfect_hash(uint32_t *nums, uint32_t* ph_mul, uint32_t* ph_shift, uint32_t* ph_mask) {
	// List of 250 equidistant twin primes (upper twin)
	static const uint32_t primes[] = { 3373, 31771, 68713, 106783, 149251, 191671, 237973, 285643, 331693, 385741, 434563, 490249, 541531, 594571, 647839, 703231, 761863, 819619, 875299, 929743, 988111, 1043113, 1099411, 1156849, 1216339, 1273423, 1335619, 1390969, 1454443, 1512829, 1572871, 1631659, 1694353, 1752943, 1815223, 1876453, 1938451, 2002969, 2063731, 2130241, 2194021, 2257441, 2324353, 2387953, 2457349, 2525671, 2591683, 2656321, 2721319, 2788831, 2855431, 2927809, 2992333, 3062881, 3126379, 3193189, 3258163, 3324613, 3394381, 3459751, 3530731, 3598591, 3662761, 3727753, 3798631, 3865009, 3933493, 4006309, 4070821, 4141723, 4211113, 4282471, 4356883, 4425853, 4495993, 4572511, 4643623, 4712683, 4786753, 4860043, 4930381, 5005129, 5082193, 5156551, 5231311, 5304853, 5378701, 5449291, 5518819, 5591743, 5665351, 5741611, 5813443, 5886889, 5957971, 6028513, 6104851, 6174811, 6246439, 6321409, 6399709, 6475831, 6549619, 6625573, 6705619, 6782443, 6860239, 6932911, 7006189, 7082809, 7158691, 7233049, 7308439, 7384549, 7458613, 7533271, 7604671, 7681363, 7752751, 7823833, 7900759, 7976359, 8053219, 8132221, 8206969, 8281459, 8361139, 8436601, 8511649, 8583139, 8656033, 8734513, 8813503, 8891419, 8966359, 9045979, 9125323, 9203101, 9282211, 9356521, 9437191, 9515269, 9591541, 9667501, 9742393, 9814993, 9900601, 9981373, 10058683, 10135201, 10213453, 10288381, 10370281, 10448239, 10529413, 10612831, 10688221, 10765621, 10847731, 10924339, 11011501, 11090293, 11167909, 11247781, 11331871, 11412859, 11493151, 11570443, 11653363, 11731693, 11815159, 11900419, 11976829, 12057841, 12138523, 12219061, 12294643, 12380569, 12458101, 12543679, 12627259, 12710569, 12788119, 12870103, 12958243, 13037749, 13123681, 13205329, 13290463, 13369273, 13446991, 13530661, 13609483, 13693819, 13779013, 13858303, 13940023, 14022733, 14100661, 14182411, 14261089, 14338609, 14421349, 14512111, 14592493, 14677501, 14760763, 14844001, 14929489, 15005701, 15084061, 15169339, 15253501, 15334093, 15414571, 15497203, 15575743, 15657841, 15739819, 15824443, 15906853, 15990901, 16079881, 16162963, 16249423, 16335901, 16417411, 16496509, 16581199, 16659343, 16744801, 16830973, 16915891, 16996561, 17079529, 17166199, 17246083, 17329261, 17419153, 17501371, 17581429, 17672023, 17751361, 17828731, 17914573, 17996611, 18086041, 18173803, 18260071, 18343081 };
	const int NUM_PRIMES = sizeof(primes)/sizeof(primes[0]);

	*ph_mask = arrlen(nums);
	*ph_mask |= *ph_mask >> 1;
	*ph_mask |= *ph_mask >> 2;
	*ph_mask |= *ph_mask >> 4;
	*ph_mask |= *ph_mask >> 8;
	*ph_mask |= *ph_mask >> 16;

	// Increase the mask (and thus hash table size) until a certain upper bound (2**24)
	for (;*ph_mask != 0x00FFFFFF;*ph_mask = (*ph_mask<<1)|1) {	
		int nslots = *ph_mask+1;
		bool slot_used[nslots];
		// Try all primes
		for (int p=0;p<NUM_PRIMES;p++) {
			*ph_mul = primes[p];
			// Try shift factors
			for (*ph_shift=0;*ph_shift<24;*ph_shift=*ph_shift+1) {
				int j;
				// Try the hash function and see if there are conflicts
				memset(slot_used, 0, nslots*sizeof(bool));
				for (j=0;j<arrlen(nums);j++) {
					uint32_t idx = ((nums[j] * *ph_mul) >> *ph_shift) & *ph_mask;
					if (slot_used[idx]) break;
					slot_used[idx] = true;
				}
				if (j == arrlen(nums)) {
					// No conflicts, we found our function
					return true;
				}
			}
		}
	}
	return false;
}

unsigned char *load_rom(const char *fn) {
	FILE *f = fopen(fn, "rb"); if (!f) panic("Cannot open: %s\n", fn);
	fseek(f, 0, SEEK_END); size_t prom_size = ftell(f);
	unsigned char *rom = malloc(prom_size);
	fseek(f, 0, SEEK_SET);
	fread(rom, 1, prom_size, f);
	fclose(f);
	return rom;
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		printf("Usage: genhle <prom>[@<addr>] <func-addr> [<func-addr>...] [<prom>[<@addr>] <func-addr>...] \n");
		printf("\n");
		printf("Where:\n");
		printf("   * <prom> is the path to a MVS64 P-ROM or BIOS (that has been previously byteswapped)\n");
		printf("   * @<addr> is the optional loading address of the ROM (default: 0)\n");
		printf("   * <func-addr> are the addresses of functions to HLE\n");
		printf("\nExample:\n");
		printf("   genhle 201-p1.n64.bin 51f94 133e6 133b0 uni-bios.n64.bin@c00000 c1df9a\n");
		return 1;
	}

	struct { char *key; char *value; } *op_interpreter;
	sh_new_strdup(op_interpreter);

	FILE* f = fopen("m68kops.c", "r"); if (!f) panic("Cannot open: m68kops.c");
	char *line = NULL; size_t linecap;
	while (getline(&line, &linecap, f) > 0) {
		if (strstartwith(line, "static void m68k_")) {
			char name[256]; char body[16384] = {0};
			*strchr(line+12, '(') = 0;
			strcpy(name, line+12);

			getline(&line, &linecap, f); // skip open parenthesis

			int nl = 0;
			while (1) {
				getline(&line, &linecap, f);
				if (!strcmp(line, "}\n")) break;
				strcat(body, "\t");
				strcat(body, line);
				if (++nl == 1000) panic("body too big: %s\n", name);
			}

			shput(op_interpreter, name, strdup(body));
		}
	}
	fclose(f);

	uint8_t *PROM = NULL; unsigned int PROM_BASE = 0;
	uint32_t *func_pcs = NULL;

	for (int fx=1;fx<argc;fx++) {
		if (strchr(argv[fx], '.')) {
			char fn[1024]; strcpy(fn, argv[fx]);
			char *at = strrchr(fn, '@');
			if (at) {
				*at = 0;
				PROM_BASE = strtoul(at+1, NULL, 16);
			} else {
				PROM_BASE = 0;
			}
			if (PROM) free(PROM);
			printf("Loading %s (base: %x)...\n", fn, PROM_BASE);
			PROM = load_rom(fn);
			continue;
		}

		unsigned int pc = strtoul(argv[fx], NULL, 16);
		if (pc == 0) {
			printf("Invalid argument: %s (ignoring)\n", argv[fx]);
			continue;
		}
		arrput(func_pcs, pc);

		char outfn[32] = "hle_"; strcat(outfn, argv[fx]); strcat(outfn, ".c");
		FILE *out = fopen(outfn, "w"); if (!out) panic("Cannot create: %s\n", outfn);

		printf("Generating %s...\n", outfn);

		const uint8_t *func = PROM + pc - PROM_BASE;
		struct { unsigned int key; bool value; } *forward_jumps = NULL;

		fprintf(out, "#include \"m68k_recompiler.h\"\n");
		fprintf(out, "#pragma GCC diagnostic ignored \"-Wunused-label\"\n");
		fprintf(out, "#pragma GCC diagnostic ignored \"-Wunused-variable\"\n");
		fprintf(out, "\n");
		fprintf(out, "uint32_t func_%08X(m68ki_cpu_core * restrict __m68ki_cpu, int * restrict __m68ki_remaining_cycles, uint32_t __pc) {\n", pc);
		fprintf(out, "\tuint FLAG_N = m68ki_cpu.n_flag, FLAG_X = m68ki_cpu.x_flag, FLAG_C = m68ki_cpu.c_flag, FLAG_V = m68ki_cpu.v_flag, FLAG_Z = m68ki_cpu.not_z_flag;\n");
		fprintf(out, "\n");

		int opcount = 0;
		while (1) {
			char disasm[256]; char body[16384];
			int oplen = m68k_disassemble_raw(disasm, pc, func, NULL, M68K_CPU_TYPE_68000);

			if (strstr(disasm, "ILLEGAL")) panic("fatal: illegal instruction at %x\n", pc);

			// If there was a forward jump to here, remove it from the list as it's satisfied now.
			if (hmgeti(forward_jumps, pc) >= 0)
				hmdel(forward_jumps, pc);

			uint16_t op = be16(func);
			const opcode_handler_struct *oph = decode_opcode(op);
			strcpy(body, shget(op_interpreter, oph->opcode_handler));

			fprintf(out, "\top_%08X: { // %s\n", pc, disasm);
			fprintf(out, "\t\t// %s\n", oph->opcode_handler);
			fprintf(out, "\t\tREG_PC = 0x%x;\n", pc+2);
			fprintf(out, "\t\tUSE_CYCLES(%d);\n", oph->cycles[0]);
			fprintf(out, "\t\tuint REG_IR = 0x%x;\n", be16(func));
			if (oplen > 2) {		
				fprintf(out, "\t\tuint OPARG[] = { ");
				for (int i=2;i<oplen;i++) fprintf(out, "0x%02x%c", func[i], i==oplen-1 ? ' ' : ',');
				fprintf(out, "}; uint OPARGIDX=0;\n");
			}

			static const char *fake_return[] = { "m68k_op_lsl_", "m68k_op_lsr_", "m68k_op_sne_", NULL };
			static const char *branches_off_8[] =  { "m68k_op_beq_8",  "m68k_op_bne_8",  "m68k_op_blt_8",  "m68k_op_bgt_8",  "m68k_op_bcc_8",  "m68k_op_dbf_8",  "m68k_op_bra_8",  NULL };
			static const char *branches_off_16[] = { "m68k_op_beq_16", "m68k_op_bne_16", "m68k_op_blt_16", "m68k_op_bgt_16", "m68k_op_bcc_16", "m68k_op_dbf_16", "m68k_op_bra_16", NULL };
			static const char *jump_tables[] = { "m68k_op_jmp_32_pc", NULL };

			char goto_next[24]; unsigned int target=0;
			if (stranyprefix(oph->opcode_handler, branches_off_16)) {
				target = pc + 2 + (int16_t)be16(func+2);
				sprintf(goto_next, "goto op_%08X;", target);
				if (!strstartwith(oph->opcode_handler, "m68k_op_bra_")) {
					replace_word(body, "m68ki_branch_16(offset);", "", 1);
					replace_word(body, "return;", goto_next, 1);
				} else {
					replace_word(body, "m68ki_branch_16(offset);", goto_next, 1);					
				}
			} else if (stranyprefix(oph->opcode_handler, branches_off_8)) {
				target = pc + 2 + (int8_t)op;
				sprintf(goto_next, "goto op_%08X;", target);
				replace_word(body, "m68ki_branch_8(MASK_OUT_ABOVE_8(REG_IR));", goto_next, 1);
				if (!strstartwith(oph->opcode_handler, "m68k_op_bra_"))
					replace_word(body, "return;", "", 1);
			} else if (stranyprefix(oph->opcode_handler, fake_return)) {
				target = pc + oplen;
				sprintf(goto_next, "goto op_%08X;", target);
				replace_word(body, "return;", goto_next, -1);
			} else if (stranyprefix(oph->opcode_handler, jump_tables)) {
				// A pc-relative jump is a duffdevice. See if we can handle it
				if (strstr(disasm, "($2,PC")) {
					int ddlen, ddstep; char jumptable[10240] = {0};
					if (decode_duffdevice(PROM, pc+oplen, &ddlen, &ddstep)) {
						strcat(jumptable, "\t\tswitch (REG_PC) { // duff device\n");
						// Generate switch/case for handled targets in duff device.
						// Notice that we also generate entry n+1 because that's potentially
						// a valid target as well: it's the first instruction that has a different
						// layout but it could be used by the programmers as target for skipping
						// the whole sequence.
						for (int i=0;i<ddlen;i++) {
							unsigned int tpc = pc + oplen + i*ddstep;

							char label[64];
							sprintf(label, "\t\t\tcase 0x%08x: goto op_%08X;\n", tpc, tpc);
							strcat(jumptable, label);

							// remember that this is a forward jump, we should see
							// this address later on as part of the current function.
							hmput(forward_jumps, tpc, true);
						}
						strcat(jumptable, "\t\t};\n");
						strcat(body, jumptable);
					} else {
						panic("cannot decode duffdevice at %x, instruction: %s", pc+2, disasm);
					}
				} else
					panic("unsupported jump table instruction: %s", disasm);
			}
			if (target > pc) hmput(forward_jumps, target, true);
		
			fprintf(out, "%s", body);

			// If the opcode body still contains a "return", it's a bug because we
			// need to handle it before to convert it to an appropriate goto.
			if (strstr(body, "return;")) {
				panic("unhandled return in body -- recompiler bug\n");
			}

			// If the opcode body still contains a branch, it's a bug because we
			// need to handle all branches one way or another as goto.
			// FIXME: we might want to change this to treat all residual branches
			// as jumps, and simply exit the HLE function.
			if (strstr(body, "m68ki_branch_")) {
				panic("unhandled branch in body -- recompiler bug\n");	
			}

			// If the function contains a jump, it's probably a jump somewhere
			// outside the current function, so the best course of action is
			// exit the HLE function and fallback to the standard interpreter.
			// For specific cases like direct branches or duff devices, the code
			// above as already tried to decode the target inline to speed it up,
			// but we keep this as a final fallback in case anything fails.
			if (strstr(body, "m68ki_jump")) {
				fprintf(out, "\t\tgoto exit;\n");
			}

			fprintf(out, "\t}\n");

			pc += oplen;
			func += oplen;
			if (++opcount > 10000) panic("function too long -- missing end of function");

			// See if this is a possible exit point
			if (strstr(oph->opcode_handler, "m68k_op_rts_")) {
				// Exit only if the next instruction was not the target of a forward jump.
				// Otherwise, this is not a terminating instruction.
				if (hmgeti(forward_jumps, pc) < 0)
					break;
			}
		}

		fprintf(out, "\n\texit:\n");
		fprintf(out, "\tm68ki_cpu.n_flag = FLAG_N; m68ki_cpu.v_flag = FLAG_V; m68ki_cpu.x_flag = FLAG_X; m68ki_cpu.c_flag = FLAG_C; m68ki_cpu.not_z_flag = FLAG_Z;\n");
		fprintf(out, "\tm68ki_cpu.pc = REG_PC;\n");
		fprintf(out, "\treturn REG_PC;\n");
		fprintf(out, "}\n");
		fclose(out);

		if (hmlen(forward_jumps) > 0) panic("forward jump not satisfied: %x", forward_jumps[0].key);
		hmfree(forward_jumps);
	}

	uint32_t ph_mul, ph_shift, ph_mask;
	if (!find_perfect_hash(func_pcs, &ph_mul, &ph_shift, &ph_mask)) panic("cannot find perfect hash for functions");

	int *htable = calloc((ph_mask+1), sizeof(int));
	for (int i=0;i<arrlen(func_pcs);i++) {
		int idx = ((func_pcs[i] * ph_mul) >> ph_shift) & ph_mask;
		assert(htable[idx] == 0);
		htable[idx] = i+1;
	}

	FILE *out = fopen("hle_index.c", "w"); if (!out) panic("Cannot create: %s\n", "hle_index.c");
	printf("Generating hle_index.c...\n");

	fprintf(out, "#include \"hle_index.h\"\n\n");
	for (int i=0;i<arrlen(func_pcs);i++) 
		fprintf(out, "extern uint32_t func_%08X(m68ki_cpu_core *cpu, int *cycles, uint32_t pc);\n", func_pcs[i]);

	fprintf(out, "\nconst HLEFunc g_hle_funcs[%d] = {\n", ph_mask+1);
	for (int i=0;i<ph_mask+1;i++) {
		int idx = htable[i];
		if (idx > 0)
			fprintf(out, "\t[%d] = { .pc = 0x%08x, .func = func_%08X },\n", i, func_pcs[idx-1], func_pcs[idx-1]);
	}
	fprintf(out, "};\n");
	fclose(out);


	out = fopen("hle_index.h", "w"); if (!out) panic("Cannot create: %s\n", "hle_index.h");
	printf("Generating hle_index.h...\n");

	fprintf(out, "#ifndef HLE_INDEX_H\n");
	fprintf(out, "#define HLE_INDEX_H\n\n");
	fprintf(out, "#include <stdint.h>\n\n");
	fprintf(out, "typedef struct m68ki_cpu_core_s m68ki_cpu_core;\n\n");
	fprintf(out, "typedef struct {\n");
	fprintf(out, "\tuint32_t pc;\n");
	fprintf(out, "\tuint32_t (*func)(m68ki_cpu_core *, int *, uint32_t);\n");
	fprintf(out, "} HLEFunc;\n\n");

	fprintf(out, "static inline const HLEFunc* hle_get_func(uint32_t pc) {\n");
	fprintf(out, "\textern const HLEFunc g_hle_funcs[];\n");
	fprintf(out, "\tconst HLEFunc *f = &g_hle_funcs[((pc * %d) >> %d) & 0x%x];\n", ph_mul, ph_shift, ph_mask);
	fprintf(out, "\treturn pc == f->pc ? f : 0;\n");
	fprintf(out, "}\n");

	fprintf(out, "#endif\n");
}


unsigned int m68k_read_disassembler_16(unsigned int address) { panic("m68k_read_disassembler_16\n"); }
unsigned int m68k_read_disassembler_32(unsigned int address) { panic("m68k_read_disassembler_32\n"); }
