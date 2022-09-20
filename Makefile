CFLAGS=-std=c18 -g -fno-common -Wall -Werror
TARGET=toycc
OUTPUT=output
TEST=test.sh
INCDIR=.
SRCDIR=.
SRCFILES := \
	$(SRCDIR)/utils.c \
	$(SRCDIR)/tokenize.c \
	$(SRCDIR)/parser.c \
	$(SRCDIR)/codegen.c \
	$(SRCDIR)/main.c \

SRC_OBJFILES := $(patsubst $(SRCDIR)/%.c, $(OUTPUT)/%.o, $(SRCFILES))

$(OUTPUT)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(OUTPUT)/$(TARGET): $(SRC_OBJFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(SRC_OBJFILES) -o $@

test: $(OUTPUT)/$(TARGET)
	@sh $(TEST)

clean:
	rm -rf $(OUTPUT)

.PHONY: test clean
