#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

// tokenize.c
enum TokenKind {
	TK_PUNCT,	// punctuators
	TK_NUM,		// Numeric literals
	TK_EOF,		// End-of-file markers
};

struct Token {
	enum TokenKind kind;	// Token kind
	struct Token *next;	// Next token
	int val;		// if kind is TK_NUM, its value
	const char *loc;		// Token location
	int len;		// Token length
};

struct Token *tokenize(const char *p);

// parser.c
enum NodeKind {
	ND_ADD,
	ND_SUB,
	ND_MUL,
	ND_DIV,
	ND_NEG,	// unary -/+
	ND_EQ,	// ==
	ND_NE,	// !=
	ND_LT,	// <
	ND_LE,	// <=
	ND_NUM,
};

// AST(abstract syntax tree) node type
struct Node {
	enum NodeKind kind;
	struct Node *lhs;
	struct Node *rhs;
	int val;
};

struct Node *parser(struct Token *tok);

// codegen.c
void codegen(struct Node *node);

// utils.c
void error_set_current_input(const char *p);
void __attribute__((noreturn)) error(const char *fmt, ...);
void __attribute__((noreturn)) verror_at(const char *loc, const char *fmt, va_list ap);
void __attribute__((noreturn)) error_tok(struct Token *tok, const char *fmt, ...);
