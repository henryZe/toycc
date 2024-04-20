#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

// Tokenizer
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

static void __attribute__((noreturn))
error(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	exit(1);
}

// Input string
static char *current_input;

static void __attribute__((noreturn))
verror_at(char *loc, char *fmt, va_list ap)
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

static void __attribute__((noreturn))
error_at(char *loc, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror_at(loc, fmt, ap);
}

static void __attribute__((noreturn))
error_tok(struct Token *tok, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror_at(tok->loc, fmt, ap);
}

static bool equal(struct Token *tok, char *op)
{
	return !memcmp(tok->loc, op, tok->len) && !op[tok->len];
}

static struct Token *skip(struct Token *tok, char *s)
{
	if (!equal(tok, s))
		error("expected '%s'", s);
	return tok->next;
}

static struct Token *new_token(enum TokenKind kind, char *start, char *end)
{
	struct Token *tok = calloc(1, sizeof(struct Token));
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

		// Punctuators
		if (ispunct(*p)) {
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

// Parser
enum NodeKind {
	ND_ADD,
	ND_SUB,
	ND_MUL,
	ND_DIV,
	ND_NUM,
};

// AST(abstract syntax tree) node type
struct Node {
	enum NodeKind kind;
	struct Node *lhs;
	struct Node *rhs;
	int val;
};

static struct Node *new_node(enum NodeKind kind)
{
	struct Node *node = calloc(1, sizeof(struct Node));
	node->kind = kind;
	return node;
}

static struct Node *new_binary(enum NodeKind kind, struct Node *lhs, struct Node *rhs)
{
       struct Node *node = new_node(kind);
       node->lhs = lhs;
       node->rhs = rhs;
       return node;
}

static struct Node *new_num(int val)
{
       struct Node *node = new_node(ND_NUM);
       node->val = val;
       return node;
}

// parse AST(abstract syntax tree)
// expr -> mul -> primary -> expr -> ...
static struct Node *expr(struct Token **rest, struct Token *tok);

static struct Node *primary(struct Token **rest, struct Token *tok)
{
       if (equal(tok, "(")) {
               struct Node *node = expr(&tok, tok->next);
               *rest = skip(tok, ")");
               return node;
       }

       if (tok->kind == TK_NUM) {
               struct Node *node = new_num(tok->val);
               *rest = tok->next;
               return node;
       }

       error_tok(tok, "expected an expression");
}

static struct Node *mul(struct Token **rest, struct Token *tok)
{
       struct Node *node = primary(&tok, tok);

       while (1) {
               if (equal(tok, "*")) {
                       node = new_binary(ND_MUL, node, primary(&tok, tok->next));
                       continue;
               }

               if (equal(tok, "/")) {
                       node = new_binary(ND_DIV, node, primary(&tok, tok->next));
                       continue;
               }

               *rest = tok;
               return node;
       }
}

static struct Node *expr(struct Token **rest, struct Token *tok)
{
       struct Node *node = mul(&tok, tok);

       while (1) {
               if (equal(tok, "+")) {
                       node = new_binary(ND_ADD, node, mul(&tok, tok->next));
                       continue;
               }

               if (equal(tok, "-")) {
                       node = new_binary(ND_SUB, node, mul(&tok, tok->next));
                       continue;
               }

               *rest = tok;
               return node;
       }
}

// code generator
static int depth = 0;
static void push(char *reg)
{
       printf("\taddi sp, sp, -8\n");
       printf("\tsd %s, 0(sp)\n", reg);
       depth++;
}

static void pop(char *reg)
{
       printf("\tld %s, 0(sp)\n", reg);
       printf("\taddi sp, sp, 8\n");
       depth--;
}

// Traverse the AST to emit assembly.
static void gen_expr(struct Node *node)
{
       if (node->kind == ND_NUM) {
               printf("\tli a0, %d\n", node->val);
               return;
       }

       gen_expr(node->rhs);
       push("a0");
       gen_expr(node->lhs);
       pop("a1");

       switch (node->kind) {
       case ND_ADD:
               printf("\tadd a0, a0, a1\n");
               break;

       case ND_SUB:
               printf("\tsub a0, a0, a1\n");
               break;

       case ND_MUL:
               printf("\tmul a0, a0, a1\n");
               break;

       case ND_DIV:
               printf("\tdiv a0, a0, a1\n");
               break;

       default:
               error("invalid expression");
               break;
       }
}

int main(int argc, char **argv)
{
	if (argc != 2)
		error("%s: invalid number of arguments", argv[0]);

	current_input = argv[1];
	struct Token *tok = tokenize();
	struct Node *node = expr(&tok, tok);

	if (tok->kind != TK_EOF)
		error_tok(tok, "extra token");

	printf("\t.global main\n");
	printf("main:\n");

	gen_expr(node);
	printf("\tret\n");

	assert(!depth);
	return 0;
}
