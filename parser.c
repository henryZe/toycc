#include <toycc.h>

static bool equal(struct Token *tok, const char *op)
{
	return !memcmp(tok->loc, op, tok->len) && !op[tok->len];
}

static struct Token *skip(struct Token *tok, const char *s)
{
	if (!equal(tok, s))
		error_tok(tok, "expected '%s'", s);
	return tok->next;
}

static struct Node *new_node(enum NodeKind kind)
{
	struct Node *node = malloc(sizeof(struct Node));
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

static struct Node *new_num(int val)
{
	struct Node *node = new_node(ND_NUM);
	node->val = val;
	return node;
}

// parse AST(abstract syntax tree)
// expr -> equality -> relational -> add -> mul -> unary -> primary -> expr -> ...
// expr:
// 	rest: return previous tok pointer
// 	tok: current tok pointer
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

static struct Node *expr(struct Token **rest, struct Token *tok)
{
	return equality(rest, tok);
}

struct Node *parser(struct Token *tok)
{
	struct Node *node = expr(&tok, tok);

	if (tok->kind != TK_EOF)
		error_tok(tok, "extra token");

	return node;
}
