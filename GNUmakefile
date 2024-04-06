ARCH = $(shell uname -a | grep -m 1 -o riscv64)

ifeq ($(ARCH),riscv64)
include Makefrag_riscv
else
include Makefrag_cross
endif
