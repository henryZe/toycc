#ifndef __PARSER_H__
#define __PARSER_H__

#include <toycc.h>

// common
bool consume_end(struct Token **rest, struct Token *tok);

int64_t eval(struct Node *node);
int64_t eval2(struct Node *node, const char **label);
double eval_double(struct Node *node);

struct Node *new_node(enum NodeKind kind, struct Token *tok);
struct Node *new_num(int64_t val, struct Token *tok);
struct Node *new_float(struct Token *tok);
struct Node *new_binary(enum NodeKind kind,
			struct Node *lhs, struct Node *rhs,
			struct Token *tok);
struct Node *new_unary(enum NodeKind kind, struct Node *expr, struct Token *tok);
struct Node *new_var_node(struct Obj *var, struct Token *tok);

const char *new_unique_name(void);

struct Node *new_ulong(long val, struct Token *tok);

// parser
bool is_typename(struct Token *tok);
struct Type *typename(struct Token **rest, struct Token *tok);

struct VarScope *find_var(struct Token *tok);

struct Node *new_add(struct Node *lhs, struct Node *rhs, struct Token *tok);

struct Node *assign(struct Token **rest, struct Token *tok);
const char *get_ident(struct Token *tok);
struct Node *expr(struct Token **rest, struct Token *tok);
struct Node *conditional(struct Token **rest, struct Token *tok);

#endif
