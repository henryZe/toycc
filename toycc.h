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

bool consume(struct Token **rest, struct Token *tok, const char *str);
struct Token *tokenize(const char *p);

// type.c

enum TypeKind {
	TY_CHAR,
	TY_INT,
	TY_PTR,
	TY_FUNC,
	TY_ARRAY,
};

struct Type {
	enum TypeKind kind;
	int size;		// sizeof() value

	// pointer-to or array-of type.
	// We intentionally use the same member to
	// represent pointer/array duality in C.
	struct Type *base;

	// declaration
	struct Token *name;

	// Array
	int array_len;

	// function type
	struct Type *return_ty;
	struct Type *params;
	struct Type *next;
};

struct Type *p_ty_char(void);
struct Type *p_ty_int(void);
bool is_integer(struct Type *ty);
struct Type *copy_type(struct Type *ty);
struct Type *pointer_to(struct Type *base);
struct Type *func_type(struct Type *return_ty);
struct Type *array_of(struct Type *base, int size);

struct Node;
void add_type(struct Node *node);

// parser.c

// local variable
struct Obj {
	struct Obj *next;
	const char *name;
	struct Type *ty;	// Type
	bool is_local;		// local or global/function

	// local variable
	int offset;		// Offset from fp

	// global variable or function
	bool is_function;

	// function
	struct Obj *params;
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
	ND_ADDR,	// unary &, address
	ND_DEREF,	// unary *, dereference
	ND_RETURN,	// "return"
	ND_IF,		// "if"
	ND_FOR,		// "for" or "while"
	ND_BLOCK,	// { ... }
	ND_FUNCALL,	// Function call
	ND_EXPR_STMT,	// Expression statement
	ND_VAR,		// Variable
	ND_NUM,		// Integer
};

// AST(abstract syntax tree) node type
struct Node {
	enum NodeKind kind;
	struct Node *next;
	struct Type *ty;	// Type, e.g. int or pointer to int
	struct Token *tok;	// Representative token

	struct Node *lhs;
	struct Node *rhs;

	// if or for statement
	struct Node *cond;
	struct Node *then;
	struct Node *els;
	struct Node *init;
	struct Node *inc;

	// function call
	const char *funcname;
	struct Node *args;

	// block
	struct Node *body;
	struct Obj *var;	// Used if kind == ND_VAR
	int val;		// Used if kind == ND_NUM
};

struct Obj *parser(struct Token *tok);

// codegen.c
#ifdef DEBUG
#define debug(fmt, args...) printf(fmt, ##args)
#else
#define debug(fmt, args...)
#endif

void codegen(struct Obj *prog);

// utils.c
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

bool equal(struct Token *tok, const char *op);
void error_set_current_input(const char *p);
void __attribute__((noreturn)) error(const char *fmt, ...);
void __attribute__((noreturn)) verror_at(const char *loc, const char *fmt, va_list ap);
void __attribute__((noreturn)) error_tok(struct Token *tok, const char *fmt, ...);
