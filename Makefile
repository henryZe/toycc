CROSS_COMPILE = riscv64-linux-gnu-
CC = gcc
OBJDUMP = objdump

CFLAGS = -std=c2x -g -O0
CFLAGS += -fno-common -Wall -Wextra -Werror
CFLAGS += -DDEBUG

CROSS_CFLAGS = -march=rv64g -static

TARGET = toycc

INCLUDE = -I. -Iparser -Ipreprocessor
SELFHOST_INCLUDE = -Iinclude $(INCLUDE)
TEST_INCLUDE = -Iinclude -Itest

HEADERFILES = \
	toycc.h \
	type.h \
	parser/declarator.h \
	parser/initializer.h \
	parser/parser.h \
	parser/scope.h \
	preprocessor/preprocessor.h \

SRCFILES = \
	utils.c \
	unicode.c \
	string.c \
	tokenize.c \
	type.c \
	preprocessor/predefined_macro.c \
	preprocessor/preprocess.c \
	parser/common.c \
	parser/initializer.c \
	parser/declarator.c \
	parser/scope.c \
	parser/parser.c \
	codegen.c \
	main.c \

TEST_SRCS = \
	alignof.c \
	arith.c \
	cast.c \
	comp_lit.c \
	compat.c \
	const.c \
	constexpr.c \
	control.c \
	decl.c \
	enum.c \
	extern.c \
	float.c \
	function.c \
	initializer.c \
	literal.c \
	pointer.c \
	sizeof.c \
	string.c \
	struct.c \
	typedef.c \
	union.c \
	usualconv.c \
	variable.c \
	macro.c \
	stdhdr.c \
	varargs.c \
	bitfield.c \
	unicode.c \
	line.c \

SRC_OBJFILES := $(patsubst %.c, output/%.o, $(SRCFILES))
TEST_DRV = test/driver.sh
TEST_QEMU = qemu_script/qemu.sh

output/%.o: %.c $(HEADERFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

output/$(TARGET): $(SRC_OBJFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(SRC_OBJFILES) -o $@
	$(OBJDUMP) -S $@ > $@.asm

# build test
output/test/%.preprocess.c: test/%.c output/$(TARGET)
	@mkdir -p $(@D)
	output/$(TARGET) $(TEST_INCLUDE) -c -E $< -o $@

TEST_PRE := $(patsubst %.c, output/test/%.preprocess.c, $(TEST_SRCS))
test_prebuild: $(TEST_PRE)

output/test/%.s: test/%.c output/$(TARGET)
	@mkdir -p $(@D)
	output/$(TARGET) $(TEST_INCLUDE) -c -S $< -o $@

TEST_ASM := $(patsubst %.c, output/test/%.s, $(TEST_SRCS))
test_build: $(TEST_ASM)

# test
# -E: preprocess C files
# -xc: compile following files as C language
# -o-: set output as stdout
# test/common.c is designed for lib function invocation test
output/test/%: test/%.c output/$(TARGET) test/common.c
	@mkdir -p $(@D)
	output/$(TARGET) $(TEST_INCLUDE) -c $< -o $@.o
	$(CROSS_COMPILE)$(CC) $(CROSS_CFLAGS) $@.o test/common.c -o $@
	$(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

TESTS = $(patsubst %.c, output/test/%, $(TEST_SRCS))
# test with qemu-riscv64
test: $(TESTS)
	for i in $^; do echo $$i; qemu-riscv64 $$i || exit 1; echo; done
	@bash $(TEST_DRV) output/$(TARGET)

# self-host
output/selfhost/$(TARGET): $(SRCFILES) output/$(TARGET) $(HEADERFILES)
	@mkdir -p $(@D)
	output/$(TARGET) $(SELFHOST_INCLUDE) $(SRCFILES) -o $@
	$(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

selfhost: output/selfhost/$(TARGET)
test_all: test selfhost

# selfhost test-cases
SELFHOST_ASM := $(patsubst output/test/%, output/selfhost/test/%.s, $(TESTS))
selfhost_test_asm:
	@mkdir -p output/selfhost/test
	touch $(SELFHOST_ASM)
	@sh $(TEST_QEMU) .

output/selfhost/test/%: test/common.c selfhost_test_asm
	$(CROSS_COMPILE)$(CC) $(CROSS_CFLAGS) $@.s $< -o $@
	# $(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

SELFHOST_TESTS = $(patsubst output/test/%, output/selfhost/test/%, $(TESTS))
# test with qemu-riscv64
selfhost_test: $(SELFHOST_TESTS)
	for i in $^; do echo $$i; qemu-riscv64 $$i || exit 1; echo; done

extra: test_all selfhost_test

clean:
	rm -rf output

.PHONY: clean test selfhost test_all extra test_prebuild test_build
