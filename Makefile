
CFLAGS = -std=c18 -fno-common -O0 -g
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -DDEBUG
TARGET = toycc
OUTPUT = output
TEST = test.sh
TEST_DRV = test_driver.sh
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

$(OUTPUT)/%.o: $(SRCDIR)/%.c $(INCDIR)/toycc.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(OUTPUT)/$(TARGET): $(SRC_OBJFILES)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(SRC_OBJFILES) -o $@

test: $(OUTPUT)/$(TARGET)
	@sh $(TEST)
	@sh $(TEST_DRV)

clean:
	rm -rf $(OUTPUT)

.PHONY: test clean
