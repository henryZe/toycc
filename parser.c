// This file contains a recursive descent parser for C.
//
// Most functions in this file are named after the symbols they are
// supposed to read from an input token list. For example, stmt() is
// responsible for reading a statement from a token list. The function
// then construct an AST node representing a statement.
//
// Each function conceptually returns two values, an AST node and
// remaining part of the input tokens. Since C doesn't support
// multiple return values, the remaining tokens are returned to the
// caller via a pointer argument.

#include <toycc.h>

// All local variable instances created during parsing are
// accumulated to this list.
struct Obj *locals;

static struct Token *skip(struct Token *tok, const char *s)
{
	if (!equal(tok, s))
		error_tok(tok, "expected '%s'", s);
	return tok->next;
}

static struct Node *new_node(enum NodeKind kind, struct Token *tok)
{
	struct Node *node = malloc(sizeof(struct Node));
	memset(node, 0, sizeof(struct Node));
	node->kind = kind;
	node->tok = tok;
	return node;
}

static struct Node *new_binary(enum NodeKind kind,
				struct Node *lhs, struct Node *rhs,
				struct Token *tok)
{
	struct Node *node = new_node(kind, tok);
	node->lhs = lhs;
	node->rhs = rhs;
	node->tok = tok;
	return node;
}

static struct Node *new_unary(enum NodeKind kind, struct Node *expr, struct Token *tok)
{
	struct Node *node = new_node(kind, tok);
	node->lhs = expr;
	node->tok = tok;
	return node;
}

static struct Node *new_var_node(struct Obj *var, struct Token *tok)
{
	struct Node *node = new_node(ND_VAR, tok);
	node->var = var;
	node->tok = tok;
	return node;
}

static struct Node *new_num(int val, struct Token *tok)
{
	struct Node *node = new_node(ND_NUM, tok);
	node->val = val;
	node->tok = tok;
	return node;
}

static struct Obj *find_var(struct Token *tok)
{
	for (struct Obj *var = locals; var; var = var->next)
		if (strlen(var->name) == tok->len &&
			!strncmp(tok->loc, var->name, tok->len))
			return var;
	return NULL;
}

static struct Obj *new_lvar(const char *name, struct Type *ty)
{
	struct Obj *var = malloc(sizeof(struct Obj));
	var->name = name;
	var->ty = ty;

	var->next = locals;
	locals = var;
	return var;
}

// parse AST(abstract syntax tree)
// expr -> assign -> equality -> relational -> add ->
// mul -> unary -> primary(num -> identifier -> bracket)
// -> expr -> ...
// expr:
// 	tok: current tok pointer
// 	rest: return current tok pointer
static struct Node *expr(struct Token **rest, struct Token *tok);
static struct Node *assign(struct Token **rest, struct Token *tok);

// funcall = ident "(" (assign ("," assign)*)? ")"
static struct Node *funcall(struct Token **rest, struct Token *tok)
{
	struct Token *start = tok;
	tok = tok->next->next;

	struct Node head = {};
	struct Node *cur = &head;

	while (!equal(tok, ")")) {
		if (cur != &head)
			tok = skip(tok, ",");

		cur->next = assign(&tok, tok);
		cur = cur->next;
	}
	*rest = skip(tok, ")");

	struct Node *node = new_node(ND_FUNCALL, tok);
	node->funcname = strndup(start->loc, start->len);
	node->args = head.next;

	return node;
}

// primary = "(" expr ")" | ident (func-args)? | num
static struct Node *primary(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "(")) {
		struct Node *node = expr(&tok, tok->next);
		*rest = skip(tok, ")");
		return node;
	}

	if (tok->kind == TK_IDENT) {
		// function call
		if (equal(tok->next, "("))
			return funcall(rest, tok);

		// variable
		struct Obj *var = find_var(tok);
		if (!var)
			error_tok(tok, "undefined variable");
		*rest = tok->next;
		return new_var_node(var, tok);
	}

	if (tok->kind == TK_NUM) {
		struct Node *node = new_num(tok->val, tok);
		*rest = tok->next;
		return node;
	}

	error_tok(tok, "expected an expression");
}

// unary = ("+" | "-" | "*" | "&") unary
// 		| primary
static struct Node *unary(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "+"))
		// ignore
		return unary(rest, tok->next);

	if (equal(tok, "-"))
		// depth--
		return new_unary(ND_NEG, unary(rest, tok->next), tok);

	if (equal(tok, "&"))
		return new_unary(ND_ADDR, unary(rest, tok->next), tok);

	if (equal(tok, "*"))
		return new_unary(ND_DEREF, unary(rest, tok->next), tok);

	return primary(rest, tok);
}

static struct Node *mul(struct Token **rest, struct Token *tok)
{
	struct Node *node = unary(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "*")) {
			node = new_binary(ND_MUL, node, unary(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "/")) {
			node = new_binary(ND_DIV, node, unary(&tok, tok->next), start);
			continue;
		}

		*rest = tok;
		return node;
	}
}

// In C, `+`/`-` operator is overloaded to perform the pointer arithmetic.
//
// If p is a pointer, p+n adds not n but sizeof((*p) * n) to the value of p,
// so that p+n points to the location n elements (not bytes) ahead of p.
// In other words, we need to scale an integer value before adding to a
// pointer value. This function takes care of the scaling.
static struct Node *new_add(struct Node *lhs, struct Node *rhs, struct Token *tok)
{
	add_type(lhs);
	add_type(rhs);

	// num + num
	if (is_integer(lhs->ty) && is_integer(rhs->ty))
		return new_binary(ND_ADD, lhs, rhs, tok);

	// ptr + ptr
	if (lhs->ty->base && rhs->ty->base)
		error_tok(tok, "invalid operands");

	// canonicalized `num + ptr` to `ptr + num`
	if (!lhs->ty->base && rhs->ty->base) {
		struct Node *tmp = lhs;
		lhs = rhs;
		rhs = tmp;
	}

	// ptr + num
	rhs = new_binary(ND_MUL, rhs, new_num(sizeof(long), tok), tok);
	return new_binary(ND_ADD, lhs, rhs, tok);
}

static struct Node *new_sub(struct Node *lhs, struct Node *rhs, struct Token *tok)
{
	add_type(lhs);
	add_type(rhs);

	// num - num
	if (is_integer(lhs->ty) && is_integer(lhs->ty))
		return new_binary(ND_SUB, lhs, rhs, tok);

	// ptr - ptr, which returns how many elements are between the two.
	if (lhs->ty->base && rhs->ty->base) {
		struct Node *node = new_binary(ND_SUB, lhs, rhs, tok);
		node->ty = p_ty_int();
		return new_binary(ND_DIV, node, new_num(sizeof(long), tok), tok);
	}

	// ptr - num
	if (lhs->ty->base && is_integer(rhs->ty)) {
		rhs = new_binary(ND_MUL, rhs, new_num(sizeof(long), tok), tok);
		return new_binary(ND_SUB, lhs, rhs, tok);
	}

	// num - ptr
	error_tok(tok, "invalid operands");
}

static struct Node *add(struct Token **rest, struct Token *tok)
{
	struct Node *node = mul(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "+")) {
			node = new_add(node, mul(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "-")) {
			node = new_sub(node, mul(&tok, tok->next), start);
			continue;
		}

		*rest = tok;
		return node;
	}
}

static struct Node *relational(struct Token **rest, struct Token *tok)
{
	struct Node *node = add(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "<")) {
			node = new_binary(ND_LT, node, add(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "<=")) {
			node = new_binary(ND_LE, node, add(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, ">")) {
			node = new_binary(ND_LT, add(&tok, tok->next), node, start);
			continue;
		}

		if (equal(tok, ">=")) {
			node = new_binary(ND_LE, add(&tok, tok->next), node, start);
			continue;
		}

		*rest = tok;
		return node;
	}
}

static struct Node *equality(struct Token **rest, struct Token *tok)
{
	struct Node *node = relational(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "==")) {
			node = new_binary(ND_EQ, node, relational(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "!=")) {
			node = new_binary(ND_NE, node, relational(&tok, tok->next), start);
			continue;
		}

		*rest = tok;
		return node;
	}
}

// assign = equality ("=" assign)
static struct Node *assign(struct Token **rest, struct Token *tok)
{
	struct Node *node = equality(&tok, tok);
	if (equal(tok, "="))
		node = new_binary(ND_ASSIGN, node, assign(&tok, tok->next), tok);
	*rest = tok;
	return node;
}

static struct Node *expr(struct Token **rest, struct Token *tok)
{
	return assign(rest, tok);
}

// expr_stmt = ";" | expr ";"
static struct Node *expr_stmt(struct Token **rest, struct Token *tok)
{
	if (equal(tok, ";")) {
		*rest = tok->next;
		// generate NULL block
		return new_node(ND_BLOCK, tok);
	}

	struct Node *node = new_node(ND_EXPR_STMT, tok);
	node->lhs = expr(&tok, tok);

	*rest = skip(tok, ";");
	return node;
}

// declspec = "int"
static struct Type *declspec(struct Token **rest, struct Token *tok)
{
	*rest = skip(tok, "int");
	return p_ty_int();
}

// type-suffix = ("(" func-params ")")?
static struct Type *type_suffix(struct Token **rest, struct Token *tok,
				struct Type *ty)
{
	if (equal(tok, "(")) {
		*rest = skip(tok->next, ")");
		return func_type(ty);
	}

	*rest = tok;
	return ty;
}

// declarator = "*"* ident (type-suffix)?
static struct Type *declarator(struct Token **rest, struct Token *tok,
				struct Type *ty)
{
	while (consume(&tok, tok, "*"))
		ty = pointer_to(ty);

	if (tok->kind != TK_IDENT)
		error_tok(tok, "expected a variable name");

	ty = type_suffix(rest, tok->next, ty);
	ty->name = tok;
	return ty;
}

static char *get_ident(struct Token *tok)
{
	if (tok->kind != TK_IDENT)
		error_tok(tok, "expected an identifier");
	return strndup(tok->loc, tok->len);
}

// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static struct Node *declaration(struct Token **rest, struct Token *tok)
{
	struct Type *basety = declspec(&tok, tok);

	// memset 'head' with 0
	struct Node head = {};
	struct Node *cur = &head;
	int i = 0;

	while (!equal(tok, ";")) {
		if (i++ > 0)
			tok = skip(tok, ",");

		struct Type *ty = declarator(&tok, tok, basety);
		struct Obj *var = new_lvar(get_ident(ty->name), ty);

		if (!equal(tok, "="))
			continue;

		// "="
		struct Node *lhs = new_var_node(var, ty->name);
		struct Node *rhs = assign(&tok, tok->next);
		struct Node *node = new_binary(ND_ASSIGN, lhs, rhs, tok);

		// "," | ";"
		cur->next = new_unary(ND_EXPR_STMT, node, tok);
		cur = cur->next;
	}

	// might empty block here
	struct Node *node = new_node(ND_BLOCK, tok);
	node->body = head.next;

	*rest = tok->next;
	return node;
}

static struct Node *compound_stmt(struct Token **rest, struct Token *tok);

// stmt = "return" expr ";"
// 	| "if" "(" expr ")" stmt ("else" stmt)?
// 	| "for" "(" expr-stmt expr? ";" expr? ")" stmt
// 	| "while" "(" expr ")" stmt
// 	| "{" compound-stmt
// 	| expr_stmt
static struct Node *stmt(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "return")) {
		struct Node *node = new_node(ND_RETURN, tok);
		node->lhs = expr(&tok, tok->next);
		*rest = skip(tok, ";");
		return node;
	}

	if (equal(tok, "if")) {
		struct Node *n = new_node(ND_IF, tok);

		tok = skip(tok->next, "(");
		n->cond = expr(&tok, tok);

		tok = skip(tok, ")");
		n->then = stmt(&tok, tok);

		if (equal(tok, "else"))
			n->els = stmt(&tok, tok->next);

		*rest = tok;
		return n;
	}

	if (equal(tok, "for")) {
		struct Node *n = new_node(ND_FOR, tok);

		tok = skip(tok->next, "(");
		n->init = expr_stmt(&tok, tok);

		if (!equal(tok, ";"))
			n->cond = expr(&tok, tok);
		tok = skip(tok, ";");

		if (!equal(tok, ")"))
			n->inc = expr(&tok, tok);
		tok = skip(tok, ")");

		n->then = stmt(rest, tok);
		return n;
	}

	if (equal(tok, "while")) {
		struct Node *n = new_node(ND_FOR, tok);

		tok = skip(tok->next, "(");
		n->cond = expr(&tok, tok);

		tok = skip(tok, ")");
		n->then = stmt(rest, tok);

		return n;
	}

	if (equal(tok, "{"))
		return compound_stmt(rest, tok->next);

	return expr_stmt(rest, tok);
}

// compound-stmt = (declaration | stmt)* "}"
static struct Node *compound_stmt(struct Token **rest, struct Token *tok)
{
	struct Node head;
	struct Node *cur = &head;
	struct Node *node = new_node(ND_BLOCK, tok);

	while (!equal(tok, "}")) {
		if (equal(tok, "int"))
			cur->next = declaration(&tok, tok);
		else
			cur->next = stmt(&tok, tok);
		cur = cur->next;
		add_type(cur);
	}
	node->body = head.next;

	// skip "}"
	*rest = tok->next;
	return node;
}

static struct Function *function(struct Token **rest, struct Token *tok)
{
	struct Type *ty = declspec(&tok, tok);
	ty = declarator(&tok, tok, ty);

	// initialize local variables list
	locals = NULL;

	struct Function *fn = malloc(sizeof(struct Function));
	fn->name = get_ident(ty->name);

	tok = skip(tok, "{");

	fn->body = compound_stmt(rest, tok);
	fn->locals = locals;
	return fn;
}

// program = (function-definition)*
struct Function *parser(struct Token *tok)
{
	struct Function head = {};
	struct Function *cur = &head;

	while (tok->kind != TK_EOF) {
		cur->next = function(&tok, tok);
		cur = cur->next;
	}
	return head.next;
}
