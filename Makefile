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
	$(CC) -c -o $@ $< $(CFLAGS) -I$(INCDIR)

$(OUTPUT)/$(TARGET): $(SRC_OBJFILES)
	@mkdir -p $(@D)
	$(CC) -o $@ $(SRC_OBJFILES) $(CFLAGS)

test: $(OUTPUT)/$(TARGET)
	@sh $(TEST)

clean:
	rm -rf $(OUTPUT)

.PHONY: test clean
