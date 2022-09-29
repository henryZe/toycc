#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

// tokenize.c

// token
enum TokenKind {
	TK_IDENT,	// Identifiers
	TK_PUNCT,	// punctuators
	TK_KEYWORD,	// keywords
	TK_NUM,		// numeric literals
	TK_EOF,		// End-of-file markers
};

struct Token {
	enum TokenKind kind;	// Token kind
	struct Token *next;	// Next token
	int val;		// if kind is TK_NUM, its value
	const char *loc;	// Token location
	size_t len;		// Token length
};

struct Token *tokenize(const char *p);

// parser.c

// local variable
struct Obj {
	struct Obj *next;
	char *name;
	int offset;		// Offset from fp
};

// function
struct Function {
	struct Node *body;
	struct Obj *locals;
	int stack_size;
};

// AST node
enum NodeKind {
	ND_ADD,
	ND_SUB,
	ND_MUL,
	ND_DIV,
	ND_NEG,		// unary -/+
	ND_EQ,		// ==
	ND_NE,		// !=
	ND_LT,		// <
	ND_LE,		// <=
	ND_ASSIGN,	// =
	ND_RETURN,	// "return"
	ND_BLOCK,	// { ... }
	ND_EXPR_STMT,	// Expression statement
	ND_VAR,		// Variable
	ND_NUM,		// Integer
};

// AST(abstract syntax tree) node type
struct Node {
	enum NodeKind kind;
	struct Node *next;
	struct Node *lhs;
	struct Node *rhs;
	struct Node *body;
	struct Obj *var;	// Used if kind == ND_VAR
	int val;		// Used if kind == ND_NUM
};

struct Function *parser(struct Token *tok);

// codegen.c
void codegen(struct Function *prog);

// utils.c
bool equal(struct Token *tok, const char *op);
void error_set_current_input(const char *p);
void __attribute__((noreturn)) error(const char *fmt, ...);
void __attribute__((noreturn)) verror_at(const char *loc, const char *fmt, va_list ap);
void __attribute__((noreturn)) error_tok(struct Token *tok, const char *fmt, ...);
