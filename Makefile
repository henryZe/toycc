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
	parser/common.c \
	parser/initializer.c \
	parser/declarator.c \
	parser/scope.c \
	parser/parser.c \
	codegen.c \
	main.c \

SRC_OBJFILES := $(patsubst %.c, output/%.o, $(SRCFILES))

TEST_SRCS = $(wildcard test/*.c)
TESTS = $(patsubst test/%.c, output/test/%, $(TEST_SRCS))
TEST_DRV = test/driver.sh
TEST_QEMU = qemu_script/qemu.sh

output/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCDIR) -c $< -o $@

output/$(TARGET): $(SRC_OBJFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(SRC_OBJFILES) -o $@
	$(OBJDUMP) -S $@ > $@.asm

# -E: preprocess C files
# -xc: compile following files as C language
# -o-: set output as stdout
output/test/%.o: output/$(TARGET) test/%.c
	@mkdir -p $(@D)
	$(CROSS_COMPILE)$(CC) -E -P -C test/$*.c -o output/test/$*.c
	output/$(TARGET) output/test/$*.c -o output/test/$*.o

output/test/%: output/test/%.o
	$(CROSS_COMPILE)$(CC) -march=rv64g -static -o $@ output/test/$*.o -xc test/common
	$(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

# test with spike
# test: $(TESTS)
# 	for i in $^; do echo $$i; /opt/RV64/bin/spike /usr/riscv64-linux-gnu/bin/pk $$i || exit 1; echo; done
# 	@sh $(TEST_DRV) output/$(TARGET)

# test with qemu
test: $(TESTS)
	@sh $(TEST_DRV) output/$(TARGET)
	cp qemu_script/run_test/default.sh output/test
	@sh $(TEST_QEMU) output/test

# bootstrap

HEADERFILES = \
	toycc.h \
	type.h \
	parser/declarator.h \
	parser/initializer.h \
	parser/parser.h \
	parser/scope.h \

bootstrap/src/%.o: output/$(TARGET) self.py $(SRCFILES)
	@mkdir -p $(@D)
	python3 self.py $(HEADERFILES) $*.c > bootstrap/src/$*.c
	output/$(TARGET) bootstrap/src/$*.c -o bootstrap/src/$*.o

BOOTSTRAP_OBJS := $(patsubst %.c, bootstrap/src/%.o, $(SRCFILES))
bootstrap_build: $(BOOTSTRAP_OBJS)

bootstrap/$(TARGET): $(BOOTSTRAP_OBJS)
	@mkdir -p $(@D)
	$(CROSS_COMPILE)$(CC) -march=rv64g -static $(BOOTSTRAP_OBJS) -o $@

bootstrap: bootstrap/$(TARGET)

test_all: bootstrap test

# bootstrap test-cases
bootstrap/test/%.c: bootstrap/$(TARGET) test/%.c
	@mkdir -p $(@D)
	$(CROSS_COMPILE)$(CC) -E -P -C test/$*.c -o bootstrap/test/$*.c

BOOTSTRAP_PRE := $(patsubst test/%.c, bootstrap/test/%.c, $(TEST_SRCS))
BOOTSTRAP_ASM := $(patsubst test/%.c, bootstrap/test/%.s, $(TEST_SRCS))
bootstrap/test/%.s: $(BOOTSTRAP_PRE)
	touch $(BOOTSTRAP_ASM)
	cp qemu_script/compile_bootstrap/default.sh bootstrap/
	@sh $(TEST_QEMU) bootstrap

bootstrap/test/%: bootstrap/test/%.s
	$(CROSS_COMPILE)$(CC) -march=rv64g -static -o $@ bootstrap/test/$*.s -xc test/common
	$(CROSS_COMPILE)$(OBJDUMP) -S $@ > $@.asm

BOOTSTRAP_TESTS = $(patsubst test/%.c, bootstrap/test/%, $(TEST_SRCS))
bootstrap_test: $(BOOTSTRAP_TESTS)
	cp qemu_script/run_test/default.sh bootstrap/test
	@sh $(TEST_QEMU) bootstrap/test

extra: test_all bootstrap_test

clean:
	rm -rf output bootstrap

.PHONY: clean test bootstrap_build test_all extra
.PRECIOUS: output/test/%.o bootstrap/test/%.c bootstrap/test/%.s
