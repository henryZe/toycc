CROSS_COMPILE = riscv64-linux-gnu-
CC = gcc
OBJDUMP = objdump

CFLAGS = -std=c2x -g -O0 -fno-common
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -DDEBUG

CROSS_CFLAGS = -march=rv64g -pthread
# CROSS_CFLAGS = -march=rv64g -pthread -static

QEMU_USER = qemu-riscv64
QEMU_LIBOPT = -L /usr/riscv64-linux-gnu/

TARGET = toycc

INCLUDE = -I. -Iparser -Ipreprocessor
TEST_INCLUDE = -Itest

HEADERFILES = \
	toycc.h \
	type.h \
	hashmap.h \
	parser/declarator.h \
	parser/initializer.h \
	parser/parser.h \
	parser/scope.h \
	preprocessor/preprocessor.h \

SRCFILES = \
	utils.c \
	unicode.c \
	string.c \
	hashmap.c \
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
	typeof.c \
	builtin.c \
	generic.c \
	asm.c \
	offsetof.c \
	commonsym.c \
	tls.c \
	alloca.c \
	vla.c \
	pragma-once.c \

THIRDPARTY = cpython.sh
# cpython.sh
# tinycc.sh
# sqlite.sh
# libpng.sh
# git.sh

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
	cp -r include/ output/

# test
# -E: preprocess C files
# -xc: compile following files as C language
# -o-: set output as stdout
# test/lin.c is designed for lib function invocation test
output/test/%: test/%.c output/$(TARGET) test/lib.c
	@mkdir -p $(@D)
	# output/$(TARGET) $(TEST_INCLUDE) -c -E $< -o $@.c
	output/$(TARGET) $(TEST_INCLUDE) -c -S $< -o $@.s
	output/$(TARGET) $(TEST_INCLUDE) -c $< -o $@.o
	$(CROSS_COMPILE)$(CC) $(CROSS_CFLAGS) $@.o test/lib.c -o $@
	$(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

TESTS = $(patsubst %.c, output/test/%, $(TEST_SRCS))
test: $(TESTS)
	for i in $^; do echo $$i; $(QEMU_USER) $(QEMU_LIBOPT) $$i || exit 1; echo; done
	@bash $(TEST_DRV) output/$(TARGET)

# self-host
output/selfhost/$(TARGET): $(SRCFILES) output/$(TARGET) $(HEADERFILES)
	@mkdir -p $(@D)
	output/$(TARGET) -static $(INCLUDE) $(SRCFILES) -o $@
	$(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm
	cp -r include/ output/selfhost/

selfhost: output/selfhost/$(TARGET)

# selfhost test-cases
SELFHOST_ASM := $(patsubst output/test/%, output/selfhost/test/%.s, $(TESTS))
selfhost_test_asm: selfhost
	@mkdir -p output/selfhost/test
	touch $(SELFHOST_ASM)
	@sh $(TEST_QEMU) .

output/selfhost/test/%: test/lib.c selfhost_test_asm
	$(CROSS_COMPILE)$(CC) $(CROSS_CFLAGS) $@.s $< -o $@
	# $(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

SELFHOST_TESTS = $(patsubst output/test/%, output/selfhost/test/%, $(TESTS))
selfhost_test: $(SELFHOST_TESTS)
	for i in $^; do echo $$i; $(QEMU_USER) $(QEMU_LIBOPT) $$i || exit 1; echo; done

all: test selfhost_test

THIRDPARTY_TEST = $(patsubst %, test/thirdparty/%, $(THIRDPARTY))
thirdparty_test: $(THIRDPARTY_TEST) output/$(TARGET)
	for i in $(THIRDPARTY_TEST); do sh $$i || exit 1; done

clean:
	rm -rf output

.PHONY: clean test selfhost selfhost_test all
