#ifndef __TOYCC_H__
#define __TOYCC_H__

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>

#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define unreachable() \
	error("internal error at %s:%d", __FILE__, __LINE__)

// main.c
extern const char *base_file;

// tokenize.c
enum TokenKind {
	TK_IDENT,	// Identifiers
	TK_PUNCT,	// punctuators
	TK_KEYWORD,	// keywords
	TK_STR,		// String literals
	TK_NUM,		// numeric literals
	TK_EOF,		// End-of-file markers
};

struct Token {
	enum TokenKind kind;	// Token kind
	struct Token *next;	// next token
	int64_t val;		// if kind is TK_NUM, its value
	double fval;		// If kind is TK_NUM, its value
	const char *loc;	// Token location
	size_t len;		// Token length
	struct Type *ty;	// used if TK_NUM or TK_STR
	const char *str;	// string literal contents including terminating '\0'
	struct File *file;	// source location
	int line_no;		// line number
	bool at_bol;		// True if this token is at beginning of line
};

struct File {
	const char *name;
	int file_no;
	const char *contents;
};

bool consume(struct Token **rest, struct Token *tok, const char *str);
void convert_keywords(struct Token *tok);
struct File **get_input_files(void);
struct Token *tokenize_file(const char *filename);

// type.c
enum TypeKind {
	TY_VOID,
	TY_BOOL,
	TY_CHAR,
	TY_SHORT,
	TY_INT,
	TY_LONG,
	TY_FLOAT,
	TY_DOUBLE,
	TY_ENUM,
	TY_PTR,
	TY_FUNC,
	TY_ARRAY,
	TY_STRUCT,
	TY_UNION,
};

// member of struct
struct Member {
	struct Member *next;
	struct Type *ty;
	struct Token *tok;	// for error message
	struct Token *name;
	int idx;
	int align;
	int offset;
};

// preprocess.c
struct Token *preprocessor(struct Token *tok);

// parser.c
// AST(abstract syntax tree) node type
enum NodeKind {
	ND_NULL_EXPR,	// do nothing
	ND_ADD,
	ND_SUB,
	ND_MUL,
	ND_DIV,
	ND_NEG,		// unary -/+
	ND_MOD,		// %
	ND_BITAND,	// &
	ND_BITOR,	// |
	ND_BITXOR,	// ^
	ND_SHL,		// <<
	ND_SHR,		// >>
	ND_EQ,		// ==
	ND_NE,		// !=
	ND_LT,		// <
	ND_LE,		// <=
	ND_ASSIGN,	// =
	ND_COND,	// ?:
	ND_COMMA,	// ,
	ND_MEMBER,	// . (struct member access)
	ND_ADDR,	// unary &, address
	ND_DEREF,	// unary *, dereference
	ND_NOT,		// !
	ND_BITNOT,	// ~
	ND_LOGAND,	// &&
	ND_LOGOR,	// ||
	ND_RETURN,	// "return"
	ND_IF,		// "if"
	ND_FOR,		// "for" or "while"
	ND_DO,		// "do"
	ND_SWITCH,	// "switch"
	ND_CASE,	// "case"
	ND_BLOCK,	// { ... }
	ND_GOTO,	// "goto"
	ND_LABEL,	// Labeled statement
	ND_FUNCALL,	// Function call
	ND_EXPR_STMT,	// Expression statement
	ND_STMT_EXPR,	// Statement expression
	ND_VAR,		// Variable
	ND_NUM,		// Integer
	ND_CAST,	// Type cast
	ND_MEMZERO,	// Zero-clear a stack variable
};

// AST node
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

	// "break" label
	const char *brk_label;
	// "continue" label
	const char *cont_label;

	// Block or statement expression
	struct Node *body;

	// struct member access
	struct Member *member;

	// function call
	struct Type *func_ty;
	struct Node *args;

	// goto or labeled statement
	const char *label;
	const char *unique_label;
	struct Node *goto_next;

	// Switch-cases
	struct Node *case_next;
	struct Node *default_case;

	// variable
	struct Obj *var;

	// numeric literal
	int64_t val;
	double fval;
};

// Global variable can be initialized either by
// 1. a constant expression or
// 2. a pointer to another global variable.
//
// This struct represents the latter.
struct Relocation {
	struct Relocation *next;
	int offset;
	const char *label;
	long addend;
};

// local variable
struct Obj {
	struct Obj *next;
	const char *name;
	struct Type *ty;	// Type
	bool is_local;		// local or global/function
	int align;		// alignment

	// local variable
	int offset;		// Offset from fp

	// global variable or function
	bool is_function;
	// function definition
	bool is_definition;
	bool is_static;

	// global variable
	const char *init_data;
	struct Relocation *rel;

	// function
	struct Obj *params;
	struct Node *body;
	struct Obj *locals;
	struct Obj *va_area;
	int stack_size;
};

struct Node *new_cast(struct Node *expr, struct Type *ty);
int64_t const_expr(struct Token **rest, struct Token *tok);
struct Obj *parser(struct Token *tok);

// codegen.c
void codegen(struct Obj *prog, FILE *out);
int align_to(int n, int align);

// utils.c
bool equal(struct Token *tok, const char *op);
void __attribute__((noreturn)) error(const char *fmt, ...);
void verror_at(const char *filename, const char *input, int line_no,
	       const char *loc, const char *fmt, va_list ap);
void __attribute__((noreturn)) error_tok(struct Token *tok, const char *fmt, ...);
void warn_tok(struct Token *tok, const char *fmt, ...);
int llog2(int num);

// string.c
struct StringArray {
	const char **data;
	int capacity;
	int len;
};

const char *format(const char *fmt, ...);
void strarray_push(struct StringArray *arr, const char *s);

#endif
