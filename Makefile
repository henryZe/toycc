CFLAGS=-std=c11 -g -fno-common
TARGET=toycc
OUTPUT=output
TEST=test.sh
SRCDIR=.
SRCFILES := \
	$(SRCDIR)/main.c \

SRC_OBJFILES := $(patsubst $(SRCDIR)/%.c, $(OUTPUT)/%.o, $(SRCFILES))

$(OUTPUT)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) -c -o $@ $< $(LDFLAGS)

$(OUTPUT)/$(TARGET): $(SRC_OBJFILES)
	@mkdir -p $(@D)
	$(CC) -o $@ $< $(LDFLAGS)

test: $(OUTPUT)/$(TARGET)
	@sh $(TEST)

clean:
	rm -rf $(OUTPUT)

.PHONY: test clean
