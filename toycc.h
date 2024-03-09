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

#ifndef __GNUC__
#define __attribute__(x)
#endif

// main.c
const char *get_base_file(void);
const struct StringArray *get_include_paths(void);
bool get_opt_fcommon(void);

// tokenize.c
enum TokenKind {
	TK_IDENT,	// Identifiers
	TK_PUNCT,	// punctuators
	TK_KEYWORD,	// keywords
	TK_STR,		// String literals
	TK_NUM,		// numeric literals
	TK_PP_NUM,	// Preprocessing numbers
	TK_EOF,		// End-of-file markers
};

struct Hideset {
	struct Hideset *next;
	const char *name;
};

struct Token {
	enum TokenKind kind;	// Token kind
	struct Token *next;	// next token
	int64_t val;		// if kind is TK_NUM, its value
	long double fval;	// If kind is TK_NUM, its value
	const char *loc;	// Token location
	size_t len;		// Token length
	struct Type *ty;	// used if TK_NUM or TK_STR
	const char *str;	// string literal contents including terminating '\0'
	struct File *file;	// source location
	const char *filename;	// Filename
	int line_no;		// line number
	int line_delta;
	bool at_bol;		// True if this token is at beginning of line
	bool has_space;		// True if this token follows a space character
	struct Hideset *hideset;// for macro expansion
	struct Token *origin;	// If this is expanded from a macro, the original token
};

struct File {
	const char *name;
	int file_no;
	const char *contents;

	// For #line directive
	const char *display_name;
	int line_delta;
};

bool consume(struct Token **rest, struct Token *tok, const char *str);
void convert_pp_tokens(struct Token *tok);
struct File **get_input_files(void);
struct Token *tokenize_file(const char *filename);
struct Token *tokenize(struct File *file);
struct File *new_file(const char *name, int file_no, const char *contents);
void __attribute__((noreturn))
error_at(const char *loc, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
struct Token *tokenize_string_literal(struct Token *tok, struct Type *basety);

// string.c
struct StringArray {
	const char **data;
	int capacity;
	int len;
};

const char *format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void strarray_push(struct StringArray *arr, const char *s);

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
	TY_LDOUBLE,
	TY_ENUM,
	TY_PTR,
	TY_FUNC,
	TY_ARRAY,
	TY_VLA,		// variable-length array
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

	// Bitfield
	bool is_bitfield;
	int bit_offset;
	int bit_width;
};

// preprocess.c
void init_macros(void);
void define_macro(const char *name, const char *buf);
void undef_macro(const char *name);
struct Token *preprocessor(struct Token *tok);
bool file_exists(const char *path);
const char *search_include_paths(const char *filename);

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
	ND_GOTO_EXPR,	// "goto" labels-as-values
	ND_LABEL,	// Labeled statement
	ND_LABEL_VAL,	// [GNU] Labels-as-values
	ND_FUNCALL,	// Function call
	ND_EXPR_STMT,	// Expression statement
	ND_STMT_EXPR,	// Statement expression
	ND_VAR,		// Variable
	ND_VLA_PTR,	// VLA designator
	ND_NUM,		// Integer
	ND_CAST,	// Type cast
	ND_MEMZERO,	// Zero-clear a stack variable
	ND_ASM,		// "asm"
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
	bool pass_by_stack;
	struct Obj *ret_buffer;

	// Goto or labeled statement, or labels-as-values
	const char *label;
	const char *unique_label;
	struct Node *goto_next;

	// Switch
	struct Node *case_next;
	struct Node *default_case;

	// Case
	long begin;
	long end;

	// "asm" string literal
	const char *asm_str;

	// variable
	struct Obj *var;

	// numeric literal
	int64_t val;
	long double fval;
};

// Global variable can be initialized either by
// 1. a constant expression or
// 2. a pointer to another global variable.
//
// This struct represents the latter.
struct Relocation {
	struct Relocation *next;
	int offset;
	const char **label;
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
	bool is_tentative;
	bool is_tls;
	const char *init_data;
	struct Relocation *rel;

	// function
	bool is_inline;
	struct Obj *params;
	struct Node *body;
	struct Obj *locals;
	struct Obj *va_area;
	struct Obj *alloca_bottom;
	int stack_size;

	// for static inline function
	bool is_live;		// referenced function
	bool is_root;		// !(static && inline)
	struct StringArray refs;// referencing functions
};

struct Node *new_cast(struct Node *expr, struct Type *ty);
int64_t const_expr(struct Token **rest, struct Token *tok);
struct Obj *parser(struct Token *tok);

// codegen.c
void codegen(struct Obj *prog, FILE *out);
int align_to(int n, int align);

// utils.c
bool equal(struct Token *tok, const char *op);
struct Token *skip(struct Token *tok, const char *s);
void __attribute__((noreturn))
error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void verror_at(const char *filename, const char *input, int line_no,
	       const char *loc, const char *fmt, va_list ap);
void __attribute__((noreturn))
error_tok(struct Token *tok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void warn_tok(struct Token *tok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int llog2(int num);

// unicode.c
int encode_utf8(char *buf, uint32_t c);
uint32_t decode_utf8(const char **new_pos, const char *p);
bool is_ident1(uint32_t c);
bool is_ident2(uint32_t c);
int display_width(const char *p, int len);

#endif
