.PHONY: all clean pctest pctest-clean

V ?= 0
D ?= 0

help:
	@echo "make pctest:   Build tests to run on PC"
	@echo
	@echo "Use make <target> D=1     to generate debugging symbols"
	@echo "Use make <target> V=1     to generate verbose output"

all: pctest

pctest:
	@echo "Building pctest"
	@make -f Makefile.pctests D=$(D) V=$(V)

pctest-clean:
	@echo "Cleaning pctest"
	@make -f Makefile.pctests clean

clean: pctest-clean

