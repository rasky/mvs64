BUILD_DIR ?= .

N64_ROOTDIR = $(N64_INST)
N64_GCCPREFIX = $(N64_ROOTDIR)/bin/mips64-elf-
N64_CHKSUMPATH = $(N64_ROOTDIR)/bin/chksum64
N64_MKDFSPATH = $(N64_ROOTDIR)/bin/mkdfs
N64_HEADERPATH = $(N64_ROOTDIR)/mips64-elf/lib
N64_TOOL = $(N64_ROOTDIR)/bin/n64tool
N64_HEADERNAME = header

N64_CFLAGS = -DN64 -falign-functions=32 -ffunction-sections -fdata-sections -std=gnu99 -march=vr4300 -mtune=vr4300 -O2 -Wall -Werror -I$(ROOTDIR)/mips64-elf/include
N64_ASFLAGS = -mtune=vr4300 -march=vr4300 -Wa,--fatal-warnings
N64_LDFLAGS = -L$(N64_ROOTDIR)/mips64-elf/lib -ldragon -lc -lm -ldragonsys -Tn64.ld --gc-sections

N64_CC = $(N64_GCCPREFIX)gcc
N64_AS = $(N64_GCCPREFIX)as
N64_LD = $(N64_GCCPREFIX)ld
N64_OBJCOPY = $(N64_GCCPREFIX)objcopy

N64_ROM_TITLE = "N64 ROM"

CFLAGS += -fdiagnostics-color=always

ifeq ($(D),1)
CFLAGS+=-g3
ASFLAGS+=-g
LDFLAGS+=-g
endif

ifeq ($(N64_BYTE_SWAP),true)
ROM_EXTENSION = .v64
N64_FLAGS = -b -l 2M -h $(N64_HEADERPATH)/$(N64_HEADERNAME)
else
ROM_EXTENSION = .z64
N64_FLAGS = -l 2M -h $(N64_HEADERPATH)/$(N64_HEADERNAME)
endif

CFLAGS+=-MMD     # automatic .d dependency generation
ASFLAGS+=-MMD    # automatic .d dependency generation

%$(ROM_EXTENSION): CC=$(N64_CC)
%$(ROM_EXTENSION): AS=$(N64_AS)
%$(ROM_EXTENSION): LD=$(N64_LD)
%$(ROM_EXTENSION): CFLAGS+=$(N64_CFLAGS)
%$(ROM_EXTENSION): ASFLAGS+=$(N64_ASFLAGS)
%$(ROM_EXTENSION): LDFLAGS+=$(N64_LDFLAGS)
%$(ROM_EXTENSION): %.elf
	@echo "    [N64] $@"
	$(N64_OBJCOPY) $< $(BUILD_DIR)/$<.bin -O binary
	@rm -f $@
	DFS_FILE=$(filter %.dfs, $^); \
	if [ -z "$$DFS_FILE" ]; then \
		$(N64_TOOL) $(N64_FLAGS) -o $(BUILD_DIR)/$@.tmp  -t $(N64_ROM_TITLE) $(BUILD_DIR)/$<.bin; \
	else \
		$(N64_TOOL) $(N64_FLAGS) -o $(BUILD_DIR)/$@.tmp  -t $(N64_ROM_TITLE) $(BUILD_DIR)/$<.bin -s 1M $$DFS_FILE; \
	fi
	@mv -f $(BUILD_DIR)/$@.tmp $@
	$(N64_CHKSUMPATH) $@ >/dev/null

%.dfs:
	@echo "    [DFS] $@"
	$(N64_MKDFSPATH) $@ $(<D) >/dev/null

$(BUILD_DIR)/%.rsp: %.S
	@echo "    [AS] $<"
	$(N64_CC) $(N64_ASFLAGS) -nostartfiles -MMD -Wl,-Ttext=0x1000 -Wl,-Tdata=0x0 -o $@ $<

$(BUILD_DIR)/%.o: %.S
	@echo "    [AS] $<"
	$(N64_CC) $(N64_ASFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "    [CC] $<"
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD_DIR)/%.text.o $(BUILD_DIR)/%.data.o: $(BUILD_DIR)/%.rsp
	@mkdir -p $(dir $@)
	@echo "    [RSP] $<"
	@$(N64_OBJCOPY) -O binary -j .text $< $<.text.bin
	@$(N64_OBJCOPY) -O binary -j .data $< $<.data.bin
	@$(N64_OBJCOPY) -I binary -O elf32-bigmips -B mips4300 \
			--redefine-sym _binary_$(subst /,_,$(basename $<))_rsp_text_bin_start=$(notdir $(basename $<))_text_start \
			--redefine-sym _binary_$(subst /,_,$(basename $<))_rsp_text_bin_end=$(notdir $(basename $<))_text_end \
			--redefine-sym _binary_$(subst /,_,$(basename $<))_rsp_text_bin_size=$(notdir $(basename $<))_text_size \
			--set-section-alignment .data=8 \
			--rename-section .text=.data $<.text.bin $(basename $<).text.o
	@$(N64_OBJCOPY) -I binary -O elf32-bigmips -B mips4300 \
			--redefine-sym _binary_$(subst /,_,$(basename $<))_rsp_data_bin_start=$(notdir $(basename $<))_data_start \
			--redefine-sym _binary_$(subst /,_,$(basename $<))_rsp_data_bin_end=$(notdir $(basename $<))_data_end \
			--redefine-sym _binary_$(subst /,_,$(basename $<))_rsp_data_bin_size=$(notdir $(basename $<))_data_size \
			--set-section-alignment .data=8 \
			--rename-section .text=.data $<.data.bin $(basename $<).data.o
	@rm $<.text.bin $<.data.bin

%.elf:
	@echo "    [LD] $@"
	$(LD) -o $@ $^ $(LDFLAGS) -Map=$(BUILD_DIR)/$@.map

ifneq ($(V),1)
.SILENT:
endif
