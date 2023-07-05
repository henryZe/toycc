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

SRC_OBJFILES := $(patsubst %.c, output/%.o, $(SRCFILES))
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
output/test/%.s: test/%.c output/$(TARGET)
	@mkdir -p $(@D)
	output/$(TARGET) -c -S $< -o $@

TEST_ASM := $(patsubst %.c, output/test/%.s, $(TEST_SRCS))
test_build: $(TEST_ASM)

TESTS = $(patsubst %.c, output/test/%, $(TEST_SRCS))
# -E: preprocess C files
# -xc: compile following files as C language
# -o-: set output as stdout
output/test/%: output/test/%.s output/$(TARGET) test/common.c
	output/$(TARGET) -c $< -o $@.o
	$(CROSS_COMPILE)$(CC) -march=rv64g -static -o $@ $@.o test/common.c
	# $(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

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
bootstrap/src/%.s: %.c output/$(TARGET) self.py $(HEADERFILES)
	@mkdir -p $(@D)
	python3 self.py $(HEADERFILES) $< > bootstrap/src/$<
	output/$(TARGET) -c -S bootstrap/src/$< -o $@

BOOTSTRAP_SRCASM := $(patsubst %.c, bootstrap/src/%.s, $(SRCFILES))
bootstrap_build: $(BOOTSTRAP_SRCASM)

bootstrap/src/%.o: %.c output/$(TARGET) self.py $(HEADERFILES)
	@mkdir -p $(@D)
	python3 self.py $(HEADERFILES) $< > bootstrap/src/$<
	output/$(TARGET) -c bootstrap/src/$< -o $@

BOOTSTRAP_SRCOBJ := $(patsubst %.c, bootstrap/src/%.o, $(SRCFILES))
bootstrap/$(TARGET): $(BOOTSTRAP_SRCOBJ)
	output/$(TARGET) $(BOOTSTRAP_SRCOBJ) -o $@

bootstrap: bootstrap/$(TARGET)

test_all: bootstrap test

# bootstrap test-cases
bootstrap/test/%.c: test/%.c
	@mkdir -p $(@D)
	cp test/*.h bootstrap/test/
	cp $< $@

BOOTSTRAP_PRE := $(patsubst output/test/%, bootstrap/test/%.c, $(TESTS))
BOOTSTRAP_ASM := $(patsubst output/test/%, bootstrap/test/%.s, $(TESTS))
bootstrap/test/%.s: $(BOOTSTRAP_PRE)
	touch $(BOOTSTRAP_ASM)
	cp qemu_script/compile_bootstrap/default.sh bootstrap/
	cp $(TEST_DRV) bootstrap/
	@sh $(TEST_QEMU) bootstrap

bootstrap/test/%: bootstrap/test/%.s test/common.c
	$(CROSS_COMPILE)$(CC) -march=rv64g -static $< test/common.c -o $@
	# $(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

BOOTSTRAP_TESTS = $(patsubst output/test/%, bootstrap/test/%, $(TESTS))
bootstrap_test: $(BOOTSTRAP_TESTS)
	cp qemu_script/run_test/default.sh bootstrap/test
	@sh $(TEST_QEMU) bootstrap/test

extra: test_all bootstrap_test

clean:
	rm -rf output bootstrap

.PHONY: clean test bootstrap test_all extra test_build bootstrap_build
.PRECIOUS: output/test/%.o bootstrap/test/%.c bootstrap/test/%.s
