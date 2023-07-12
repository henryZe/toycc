CROSS_COMPILE = riscv64-linux-gnu-
CC = gcc
OBJDUMP = objdump

CFLAGS = -std=c2x -g -O0
CFLAGS += -fno-common -Wall -Wextra -Werror
CFLAGS += -DDEBUG

TARGET = toycc

INCDIR = -I. -Iparser -Ipreprocessor
INCLUDE = -Iinclude $(INCDIR)
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

SRC_OBJFILES := $(patsubst %.c, output/%.o, $(SRCFILES))
TEST_DRV = test/driver.sh
TEST_QEMU = qemu_script/qemu.sh

output/%.o: %.c $(HEADERFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCDIR) -c $< -o $@

output/$(TARGET): $(SRC_OBJFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(SRC_OBJFILES) -o $@
	# $(OBJDUMP) -S $@ > $@.asm

# test
output/test/%.s: test/%.c output/$(TARGET)
	@mkdir -p $(@D)
	output/$(TARGET) $(TEST_INCLUDE) -c -S $< -o $@

TEST_ASM := $(patsubst %.c, output/test/%.s, $(TEST_SRCS))
test_build: $(TEST_ASM)

TESTS = $(patsubst %.c, output/test/%, $(TEST_SRCS))
# -E: preprocess C files
# -xc: compile following files as C language
# -o-: set output as stdout
# test/common.c is designed for lib function invocation test
output/test/%: test/%.c output/$(TARGET) test/common.c
	@mkdir -p $(@D)
	output/$(TARGET) $(TEST_INCLUDE) -c $< -o $@.o
	$(CROSS_COMPILE)$(CC) -march=rv64g -static $@.o test/common.c -o $@
	# $(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

# test with qemu-riscv64
test: $(TESTS)
	for i in $^; do echo $$i; qemu-riscv64 $$i || exit 1; echo; done
	@sh $(TEST_DRV) output/$(TARGET)

# self-host
# todo: remove self.py
selfhost/src/%.s: %.c output/$(TARGET) self.py
	@mkdir -p $(@D)
	python3 self.py $< > selfhost/src/$<
	output/$(TARGET) $(INCLUDE) -c -S selfhost/src/$< -o $@
	# output/$(TARGET) $(INCLUDE) -c -S $< -o $@

SELFHOST_SRCASM := $(patsubst %.c, selfhost/src/%.s, $(SRCFILES))
selfhost_build: $(SELFHOST_SRCASM)

# todo: remove self.py
selfhost/src/%.c: %.c self.py
	@mkdir -p $(@D)
	python3 self.py $< > selfhost/src/$<

SELFHOST_SRC := $(patsubst %.c, selfhost/src/%.c, $(SRCFILES))
selfhost/$(TARGET): $(SELFHOST_SRC) output/$(TARGET) $(HEADERFILES)
	@mkdir -p $(@D)
	output/$(TARGET) $(INCLUDE) $(SELFHOST_SRC) -o $@

selfhost: selfhost/$(TARGET)

test_all: test selfhost

# selfhost test-cases
selfhost/test/%.c: test/%.c
	@mkdir -p $(@D)
	cp $< $@

SELFHOST_PRE := $(patsubst output/test/%, selfhost/test/%.c, $(TESTS))
SELFHOST_ASM := $(patsubst output/test/%, selfhost/test/%.s, $(TESTS))
selfhost_test_asm: $(SELFHOST_PRE)
	cp test/*.h selfhost/test/
	cp -r include/ selfhost/
	touch $(SELFHOST_ASM)
	cp qemu_script/run_compile/default.sh selfhost/
	@sh $(TEST_QEMU) selfhost

selfhost/test/%: test/common.c selfhost_test_asm
	$(CROSS_COMPILE)$(CC) -march=rv64g -static $@.s $< -o $@
	# $(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

SELFHOST_TESTS = $(patsubst output/test/%, selfhost/test/%, $(TESTS))
# test with qemu-riscv64
selfhost_test: $(SELFHOST_TESTS)
	for i in $^; do echo $$i; qemu-riscv64 $$i || exit 1; echo; done

extra: test_all selfhost_test

clean:
	rm -rf output selfhost

.PHONY: clean test selfhost test_all extra test_build selfhost_build
