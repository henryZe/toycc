CROSS_COMPILE = riscv64-linux-gnu-
CC = gcc

CFLAGS = -std=c18 -fno-common -O0 -g
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -DDEBUG

TARGET_X86 = toycc
TARGET_RISCV = riscv-toycc

INCDIR = .
SRCDIR = .
SOURCE = \
	$(SRCDIR)/utils.c \
	$(SRCDIR)/string.c \
	$(SRCDIR)/tokenize.c \
	$(SRCDIR)/type.c \
	$(SRCDIR)/parser.c \
	$(SRCDIR)/codegen.c \
	$(SRCDIR)/main.c \

X86_SRC = $(SOURCE)
X86_SRC += $(SRCDIR)/x86.c

RISCV_SRC = $(SOURCE)
RISCV_SRC += $(SRCDIR)/riscv.c

SRC_X86_OBJ = $(patsubst $(SRCDIR)/%.c, output/x86/%.o, $(X86_SRC))
SRC_RISCV_OBJ = $(patsubst $(SRCDIR)/%.c, output/riscv/%.o, $(RISCV_SRC))

TEST_SRCS = $(wildcard test/*.c)
TESTS_X86 = $(patsubst test/%.c, output/test/x86/%, $(TEST_SRCS))
TESTS_RISCV = $(patsubst test/%.c, output/test/riscv/%, $(TEST_SRCS))

TEST_DRV = test/driver.sh

output/x86/%.o: $(SRCDIR)/%.c $(INCDIR)/toycc.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DGEN_X86 -I$(INCDIR) -c $< -o $@

output/riscv/%.o: $(SRCDIR)/%.c $(INCDIR)/toycc.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DGEN_RISCV -I$(INCDIR) -c $< -o $@

output/$(TARGET_X86): $(SRC_X86_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@

output/$(TARGET_RISCV): $(SRC_RISCV_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $^ -o $@

x86: output/$(TARGET_X86)
riscv: output/$(TARGET_RISCV)
all: x86 riscv

# -E: preprocess C files
# -xc: compile following files as C language
# -o-: set output as stdout
output/test/x86/%: output/$(TARGET_X86) test/%.c
	@mkdir -p $(@D)
	$(CC) -o- -E -P -C test/$*.c | output/$(TARGET_X86) -o output/test/x86/$*.s -
	$(CC) -static -o $@ output/test/x86/$*.s -xc test/common

output/test/riscv/%: output/$(TARGET_RISCV) test/%.c
	@mkdir -p $(@D)
	$(CROSS_COMPILE)$(CC) -o- -E -P -C test/$*.c | output/$(TARGET_RISCV) -o output/test/riscv/$*.s -
	$(CROSS_COMPILE)$(CC) -march=rv64g -static -o $@ output/test/riscv/$*.s -xc test/common

test_x86: $(TESTS_X86)
	for i in $^; do echo $$i; $$i || exit 1; echo; done
	@sh $(TEST_DRV) $(TARGET_X86)

test_riscv: $(TESTS_RISCV)
	for i in $^; do echo $$i; /opt/RV64/bin/spike /usr/riscv64-linux-gnu/bin/pk $$i || exit 1; echo; done
	@sh $(TEST_DRV) $(TARGET_RISCV)

test: test_x86 test_riscv

clean:
	rm -rf output

.PHONY: all x86 riscv test test_x86 test_riscv clean
