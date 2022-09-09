CFLAGS=-std=c18 -g -fno-common -Wall -Werror
TARGET=toycc
OUTPUT=output
TEST=test.sh
SRCDIR=.
SRCFILES := \
	$(SRCDIR)/main.c \

SRC_OBJFILES := $(patsubst $(SRCDIR)/%.c, $(OUTPUT)/%.o, $(SRCFILES))

$(OUTPUT)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) -c -o $@ $< $(CFLAGS)

$(OUTPUT)/$(TARGET): $(SRC_OBJFILES)
	@mkdir -p $(@D)
	$(CC) -o $@ $< $(CFLAGS)

test: $(OUTPUT)/$(TARGET)
	@sh $(TEST)

clean:
	rm -rf $(OUTPUT)

.PHONY: test clean
