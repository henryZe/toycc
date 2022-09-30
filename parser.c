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

static struct Node *new_node(enum NodeKind kind)
{
	struct Node *node = malloc(sizeof(struct Node));
	memset(node, 0, sizeof(struct Node));
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

static struct Node *new_unary(enum NodeKind kind, struct Node *expr)
{
	struct Node *node = new_node(kind);
	node->lhs = expr;
	return node;
}

static struct Node *new_var_node(struct Obj *var)
{
	struct Node *node = new_node(ND_VAR);
	node->var = var;
	return node;
}

static struct Node *new_num(int val)
{
	struct Node *node = new_node(ND_NUM);
	node->val = val;
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

static struct Obj *new_lvar(char *name)
{
	struct Obj *var = malloc(sizeof(struct Obj));
	var->name = name;
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

// primary = "(" expr ")" | ident | num
static struct Node *primary(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "(")) {
		struct Node *node = expr(&tok, tok->next);
		*rest = skip(tok, ")");
		return node;
	}

	if (tok->kind == TK_IDENT) {
		struct Obj *var = find_var(tok);
		if (!var)
			var = new_lvar(strndup(tok->loc, tok->len));
		*rest = tok->next;
		return new_var_node(var);
	}

	if (tok->kind == TK_NUM) {
		struct Node *node = new_num(tok->val);
		*rest = tok->next;
		return node;
	}

	error_tok(tok, "expected an expression");
}

static struct Node *unary(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "+"))
		// ignore
		return unary(rest, tok->next);
	if (equal(tok, "-"))
		// depth--
		return new_unary(ND_NEG, unary(rest, tok->next));
	return primary(rest, tok);
}

static struct Node *mul(struct Token **rest, struct Token *tok)
{
	struct Node *node = unary(&tok, tok);

	while (1) {
		if (equal(tok, "*")) {
			node = new_binary(ND_MUL, node, unary(&tok, tok->next));
			continue;
		}

		if (equal(tok, "/")) {
			node = new_binary(ND_DIV, node, unary(&tok, tok->next));
			continue;
		}

		*rest = tok;
		return node;
	}
}

static struct Node *add(struct Token **rest, struct Token *tok)
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

static struct Node *relational(struct Token **rest, struct Token *tok)
{
	struct Node *node = add(&tok, tok);

	while (1) {
		if (equal(tok, "<")) {
			node = new_binary(ND_LT, node, add(&tok, tok->next));
			continue;
		}

		if (equal(tok, "<=")) {
			node = new_binary(ND_LE, node, add(&tok, tok->next));
			continue;
		}

		if (equal(tok, ">")) {
			node = new_binary(ND_LT, add(&tok, tok->next), node);
			continue;
		}

		if (equal(tok, ">=")) {
			node = new_binary(ND_LE, add(&tok, tok->next), node);
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
		if (equal(tok, "==")) {
			node = new_binary(ND_EQ, node, relational(&tok, tok->next));
			continue;
		}

		if (equal(tok, "!=")) {
			node = new_binary(ND_NE, node, relational(&tok, tok->next));
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
		node = new_binary(ND_ASSIGN, node, assign(&tok, tok->next));
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
		return new_node(ND_BLOCK);
	}

	struct Node *node = new_unary(ND_EXPR_STMT, expr(&tok, tok));
	*rest = skip(tok, ";");
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
		struct Node *node =
			new_unary(ND_RETURN, expr(&tok, tok->next));
		*rest = skip(tok, ";");
		return node;
	}

	if (equal(tok, "if")) {
		struct Node *n = new_node(ND_IF);

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
		struct Node *n = new_node(ND_FOR);

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
		struct Node *n = new_node(ND_FOR);

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

static struct Node *compound_stmt(struct Token **rest, struct Token *tok)
{
	struct Node head;
	struct Node *cur = &head;

	while (!equal(tok, "}")) {
		cur->next = stmt(&tok, tok);
		cur = cur->next;
	}

	struct Node *node = new_node(ND_BLOCK);
	node->body = head.next;

	// skip "}"
	*rest = tok->next;
	return node;
}

struct Function *parser(struct Token *tok)
{
	struct Function *prog = malloc(sizeof(struct Function));

	tok = skip(tok, "{");
	prog->body = compound_stmt(&tok, tok);
	prog->locals = locals;
	return prog;
}
