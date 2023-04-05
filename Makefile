CROSS_COMPILE = riscv64-linux-gnu-
CC = gcc

CFLAGS = -std=c2x -g -O0
CFLAGS += -fno-common -Wall -Wextra -Werror
CFLAGS += -DDEBUG

TARGET = toycc
OUTPUT = output

INCDIR = .
SRCDIR = .
SRCFILES = \
	$(SRCDIR)/utils.c \
	$(SRCDIR)/string.c \
	$(SRCDIR)/tokenize.c \
	$(SRCDIR)/type.c \
	$(SRCDIR)/parser.c \
	$(SRCDIR)/codegen.c \
	$(SRCDIR)/main.c \

SRC_OBJFILES = $(patsubst $(SRCDIR)/%.c, $(OUTPUT)/%.o, $(SRCFILES))

TESTDIR = test
TEST_SRCS = $(wildcard $(TESTDIR)/*.c)
TESTS = $(patsubst $(TESTDIR)/%.c, $(OUTPUT)/$(TESTDIR)/%, $(TEST_SRCS))
TEST_DRV = $(TESTDIR)/driver.sh
TEST_QEMU = qemu.sh

$(OUTPUT)/%.o: $(SRCDIR)/%.c $(INCDIR)/toycc.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(OUTPUT)/$(TARGET): $(SRC_OBJFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(SRC_OBJFILES) -o $@

# -E: preprocess C files
# -xc: compile following files as C language
# -o-: set output as stdout
$(OUTPUT)/$(TESTDIR)/%: $(OUTPUT)/$(TARGET) $(TESTDIR)/%.c
	@mkdir -p $(@D)
	$(CROSS_COMPILE)$(CC) -E -P -C $(TESTDIR)/$*.c -o $(OUTPUT)/$(TESTDIR)/$*.c
	$(OUTPUT)/$(TARGET) $(OUTPUT)/$(TESTDIR)/$*.c -o $(OUTPUT)/$(TESTDIR)/$*.s
	$(CROSS_COMPILE)$(CC) -march=rv64g -static -o $@ $(OUTPUT)/$(TESTDIR)/$*.s -xc $(TESTDIR)/common

test: $(TESTS)
	for i in $^; do echo $$i; /opt/RV64/bin/spike /usr/riscv64-linux-gnu/bin/pk $$i || exit 1; echo; done
	@sh $(TEST_DRV)

qemu: $(TESTS)
	@sh $(TEST_DRV)
	@sh $(TEST_QEMU)

clean:
	rm -rf $(OUTPUT)

.PHONY: test qemu clean
