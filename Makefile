.PHONY: all clean mv64 pctest pctest-clean

V ?= 0
D ?= 0

help:
	@echo "make mvs64:    Build mvs64 ROM"
	@echo "make pctest:   Build tests to run on PC"
	@echo
	@echo "Use make <target> D=1     to generate debugging symbols"
	@echo "Use make <target> V=1     to generate verbose output"

all: mv64 pctest

mvs64:
	@echo "Building mvs64"
	@libdragon make -f Makefile.mvs64 D=$(D) V=$(V)

mvs64-clean:
	@echo "Cleaning mvs64"
	@libdragon make -f Makefile.mvs64 clean

pctest:
	@echo "Building pctest"
	@make -f Makefile.pctests D=$(D) V=$(V)

pctest-clean:
	@echo "Cleaning pctest"
	@make -f Makefile.pctests clean

clean: mvs64-clean pctest-clean
