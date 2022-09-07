#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>

enum TokenKind {
	TK_PUNCT,	// punctuators
	TK_NUM,		// Numeric literals
	TK_EOF,		// End-of-file markers
};

struct Token {
	enum TokenKind kind;	// Token kind
	struct Token *next;	// Next token
	int val;		// if kind is TK_NUM, its value
	char *loc;		// Token location
	int len;		// Token length
};

static void error(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	exit(1);
}

// Input string
static char *current_input;

static void verror_at(char *loc, char *fmt, va_list ap)
{
	int pos = loc - current_input;

	fprintf(stderr, "%s\n", current_input);
	// print pos spaces
	fprintf(stderr, "%*s", pos, "");
	fprintf(stderr, "^ ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	exit(1);
}

static void error_at(char *loc, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror_at(loc, fmt, ap);
}

static void error_tok(struct Token *tok, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	error_at(tok->loc, fmt, ap);
}

static bool equal(struct Token *tok, char *op)
{
	return !memcmp(tok->loc, op, tok->len) && !op[tok->len];
}

static struct Token *skip(struct Token *tok, char *s)
{
	if (!equal(tok, s))
		error_tok(tok, "expected '%s'", s);
	return tok->next;
}

static int get_number(struct Token *tok)
{
	if (tok->kind != TK_NUM)
		error_tok(tok, "expected a number");
	return tok->val;
}

static struct Token *new_token(enum TokenKind kind, char *start, char *end)
{
	struct Token *tok = malloc(sizeof(struct Token));

	tok->kind = kind;
	tok->loc = start;
	tok->len = end - start;
	return tok;
}

static struct Token *tokenize(void)
{
	char *p = current_input;
	struct Token head;
	struct Token *cur = &head;

	while (*p) {
		// Skip whitespace characters
		if (isspace(*p)) {
			p++;
			continue;
		}

		// Numeric literal
		if (isdigit(*p)) {
			cur->next = new_token(TK_NUM, p, p);
			cur = cur->next;

			char *q = p;
			cur->val = strtol(p, &p, 10);
			cur->len = p - q;
			continue;
		}

		if ((*p == '+') || (*p == '-')) {
			cur->next = new_token(TK_PUNCT, p, p + 1);
			cur = cur->next;
			p++;
			continue;
		}

		error_at(p, "invalid token");
	}

	cur->next = new_token(TK_EOF, p, p);
	return head.next;
}

int main(int argc, char **argv)
{
	if (argc != 2)
		error("%s: invalid number of arguments", argv[0]);

	current_input = argv[1];
	struct Token *tok = tokenize();

	printf("\t.global main\n");
	printf("main:\n");
	printf("\tli a0, %d\n", get_number(tok));

	tok = tok->next;
	while (tok->kind != TK_EOF) {
		if (tok->kind == TK_PUNCT) {
			if (equal(tok, "+"))
				printf("\tadd a0, a0, %d\n", get_number(tok->next));
			else if (equal(tok, "-"))
				printf("\tadd a0, a0, %d\n", -get_number(tok->next));

			tok = tok->next->next;
		}
	}

	printf("\tret\n");
	return 0;
}
