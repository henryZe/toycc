CROSS_COMPILE = riscv64-linux-gnu-
CC = gcc
OBJDUMP = objdump

CFLAGS = -std=c2x -g -O0
CFLAGS += -fno-common -Wall -Wextra -Werror
CFLAGS += -DDEBUG

TARGET = toycc

INCDIR = -I. -Iparser

SRCFILES = \
	utils.c \
	string.c \
	tokenize.c \
	type.c \
	preprocess.c \
	parser/common.c \
	parser/initializer.c \
	parser/declarator.c \
	parser/scope.c \
	parser/parser.c \
	codegen.c \
	main.c \

HEADERFILES = \
	toycc.h \
	type.h \
	parser/declarator.h \
	parser/initializer.h \
	parser/parser.h \
	parser/scope.h \

TEST_SRCS = \
	test/alignof.c \
	test/arith.c \
	test/cast.c \
	test/comp_lit.c \
	test/compat.c \
	test/const.c \
	test/constexpr.c \
	test/control.c \
	test/decl.c \
	test/enum.c \
	test/extern.c \
	test/float.c \
	test/function.c \
	test/initializer.c \
	test/literal.c \
	test/pointer.c \
	test/sizeof.c \
	test/string.c \
	test/struct.c \
	test/typedef.c \
	test/union.c \
	test/usualconv.c \
	test/variable.c \

SRC_OBJFILES := $(patsubst %.c, output/%.o, $(SRCFILES))
TESTS = $(patsubst test/%.c, output/test/%, $(TEST_SRCS))
TEST_DRV = test/driver.sh
TEST_QEMU = qemu_script/qemu.sh

output/%.o: %.c $(HEADERFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCDIR) -c $< -o $@

output/$(TARGET): $(SRC_OBJFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(SRC_OBJFILES) -o $@
	$(OBJDUMP) -S $@ > $@.asm

# test
# -E: preprocess C files
# -xc: compile following files as C language
# -o-: set output as stdout
output/test/%.o: test/%.c output/$(TARGET)
	@mkdir -p $(@D)
	$(CROSS_COMPILE)$(CC) -E -P -C $< -o output/$<
	output/$(TARGET) -c -S output/$< -o output/test/$*.s
	output/$(TARGET) -c output/$< -o $@

output/test/%: output/test/%.o test/common.c
	$(CROSS_COMPILE)$(CC) -march=rv64g -static -o $@ $< test/common.c
	# $(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

output/test/macro: test/macro.c output/$(TARGET)
	@mkdir -p $(@D)
	output/$(TARGET) -c -S $< -o $@.s
	output/$(TARGET) -c $< -o $@.o
	$(CROSS_COMPILE)$(CC) -march=rv64g -static $@.o test/common.c -o $@
	# $(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

TESTS += output/test/macro

# test with spike
# test: $(TESTS)
# 	for i in $^; do echo $$i; /opt/RV64/bin/spike /usr/riscv64-linux-gnu/bin/pk $$i || exit 1; echo; done
# 	@sh $(TEST_DRV) output/$(TARGET)

# test with qemu
test: $(TESTS)
	cp qemu_script/run_test/default.sh output/test
	@sh $(TEST_QEMU) output/test
	@sh $(TEST_DRV) output/$(TARGET)

# bootstrap
bootstrap/src/%.s: output/$(TARGET) self.py $(HEADERFILES) %.c
	@mkdir -p $(@D)
	python3 self.py $(HEADERFILES) $*.c > bootstrap/src/$*.c
	output/$(TARGET) -c -S bootstrap/src/$*.c -o bootstrap/src/$*.s

BOOTSTRAP_SRCASM := $(patsubst %.c, bootstrap/src/%.s, $(SRCFILES))
bootstrap_build: $(BOOTSTRAP_SRCASM)

bootstrap/$(TARGET): $(BOOTSTRAP_SRCASM)
	@mkdir -p $(@D)
	$(CROSS_COMPILE)$(CC) -march=rv64g -static $(BOOTSTRAP_SRCASM) -o $@

bootstrap: bootstrap/$(TARGET)

test_all: bootstrap test

# bootstrap test-cases
bootstrap/test/%.c: bootstrap/$(TARGET) test/%.c
	@mkdir -p $(@D)
	$(CROSS_COMPILE)$(CC) -E -P -C test/$*.c -o bootstrap/test/$*.c
	cp test/macro.c bootstrap/test/
	cp test/include*.h bootstrap/test/

BOOTSTRAP_PRE := $(patsubst output/test/%, bootstrap/test/%.c, $(TESTS))
BOOTSTRAP_ASM := $(patsubst output/test/%, bootstrap/test/%.s, $(TESTS))
bootstrap/test/%.s: $(BOOTSTRAP_PRE)
	touch $(BOOTSTRAP_ASM)
	cp qemu_script/compile_bootstrap/default.sh bootstrap/
	cp $(TEST_DRV) bootstrap/
	@sh $(TEST_QEMU) bootstrap

bootstrap/test/%: bootstrap/test/%.s test/common.c
	$(CROSS_COMPILE)$(CC) -march=rv64g -static -o $@ $< test/common.c
	# $(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

BOOTSTRAP_TESTS = $(patsubst output/test/%, bootstrap/test/%, $(TESTS))
bootstrap_test: $(BOOTSTRAP_TESTS)
	cp qemu_script/run_test/default.sh bootstrap/test
	@sh $(TEST_QEMU) bootstrap/test

extra: test_all bootstrap_test

clean:
	rm -rf output bootstrap

.PHONY: clean test bootstrap_build test_all extra
.PRECIOUS: output/test/%.o bootstrap/test/%.c bootstrap/test/%.s
