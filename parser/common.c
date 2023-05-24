#include <parser.h>
#include <type.h>

struct Token *skip(struct Token *tok, const char *s)
{
	if (!equal(tok, s))
		error_tok(tok, "expected '%s'", s);
	return tok->next;
}

bool consume_end(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "}")) {
		*rest = tok->next;
		return true;
	}

	if (equal(tok, ",") && equal(tok->next, "}")) {
		*rest = tok->next->next;
		return true;
	}

	return false;
}

static int64_t eval_rval(struct Node *node, const char **label)
{
	switch (node->kind) {
		case ND_VAR:
			if (node->var->is_local)
				error_tok(node->tok, "not a compile-time constant");

			// return variable name
			*label = node->var->name;
			return 0;

		case ND_DEREF:
			return eval2(node->lhs, label);

		case ND_MEMBER:
			// return address
			return eval_rval(node->lhs, label) +
					 node->member->offset;

		default:
			error_tok(node->tok, "invalid initializer");
	}
}

// Evaluate a given node as a constant expression.
// A constant expression is either just a number or ptr+n
// where ptr is a pointer to a global variable and
// n is a positive/negative number.
// The latter form is accepted only as an initialization
// expression for a global variable.
int64_t eval2(struct Node *node, const char **label)
{
	add_type(node);

	switch (node->kind) {
	case ND_ADD:
		return eval2(node->lhs, label) + eval(node->rhs);

	case ND_SUB:
		return eval2(node->lhs, label) - eval(node->rhs);

	case ND_MUL:
		return eval(node->lhs) * eval(node->rhs);

	case ND_DIV:
		return eval(node->lhs) / eval(node->rhs);

	case ND_NEG:
		return -eval(node->lhs);

	case ND_MOD:
		return eval(node->lhs) % eval(node->rhs);

	case ND_BITAND:
		return eval(node->lhs) & eval(node->rhs);

	case ND_BITOR:
		return eval(node->lhs) | eval(node->rhs);

	case ND_BITXOR:
		return eval(node->lhs) ^ eval(node->rhs);

	case ND_SHL:
		return eval(node->lhs) << eval(node->rhs);

	case ND_SHR:
		return eval(node->lhs) >> eval(node->rhs);

	case ND_EQ:
		return eval(node->lhs) == eval(node->rhs);

	case ND_NE:
		return eval(node->lhs) != eval(node->rhs);

	case ND_LT:
		return eval(node->lhs) < eval(node->rhs);

	case ND_LE:
		return eval(node->lhs) <= eval(node->rhs);

	case ND_COND:
		return eval(node->cond) ?
		       eval2(node->then, label) : eval2(node->els, label);

	case ND_COMMA:
		return eval2(node->rhs, label);

	case ND_NOT:
		return !eval(node->lhs);

	case ND_BITNOT:
		return ~eval(node->lhs);

	case ND_LOGAND:
		return eval(node->lhs) && eval(node->rhs);

	case ND_LOGOR:
		return eval(node->lhs) || eval(node->rhs);

	case ND_CAST:
		int64_t val = eval2(node->lhs, label);

		if (is_integer(node->ty)) {
			switch (node->ty->size) {
			case 1:
				return (uint8_t)val;
			case 2:
				return (uint16_t)val;
			case 4:
				return (uint32_t)val;
			default:
				break;
			}
		}
		return val;

	case ND_ADDR:
		// return label
		return eval_rval(node->lhs, label);

	case ND_MEMBER:
		if (!label)
			error_tok(node->tok, "not a compile-time constant");
		if (node->ty->kind != TY_ARRAY)
			error_tok(node->tok, "invalid initializer");

		// return address
		return eval_rval(node->lhs, label) + node->member->offset;

	case ND_VAR:
		if (!label)
			error_tok(node->tok, "not a compile-time constant");
		if (node->var->ty->kind != TY_ARRAY && node->var->ty->kind != TY_FUNC)
			error_tok(node->tok, "invalid initializer");

		// return label
		*label = node->var->name;
		return 0;

	case ND_NUM:
		return node->val;

	default:
		error_tok(node->tok, "not a compile-time constant");
	}
}

// Evaluate a given node as a constant expression.
int64_t eval(struct Node *node)
{
	return eval2(node, NULL);
}

struct Node *new_node(enum NodeKind kind, struct Token *tok)
{
	struct Node *node = malloc(sizeof(struct Node));
	memset(node, 0, sizeof(struct Node));
	node->kind = kind;
	node->tok = tok;
	return node;
}

struct Node *new_num(int64_t val, struct Token *tok)
{
	struct Node *node = new_node(ND_NUM, tok);
	node->val = val;
	node->tok = tok;
	return node;
}

struct Node *new_binary(enum NodeKind kind,
			struct Node *lhs, struct Node *rhs,
			struct Token *tok)
{
	struct Node *node = new_node(kind, tok);
	node->lhs = lhs;
	node->rhs = rhs;
	node->tok = tok;
	return node;
}

struct Node *new_unary(enum NodeKind kind, struct Node *expr, struct Token *tok)
{
	struct Node *node = new_node(kind, tok);
	node->lhs = expr;
	node->tok = tok;
	return node;
}

struct Node *new_var_node(struct Obj *var, struct Token *tok)
{
	struct Node *node = new_node(ND_VAR, tok);
	node->var = var;
	node->tok = tok;
	return node;
}

const char *new_unique_name(void)
{
	static int id = 0;

	return format(".L..%d", id++);
}

struct Node *new_ulong(long val, struct Token *tok)
{
	struct Node *node = new_node(ND_NUM, tok);
	node->val = val;
	node->ty = p_ty_ulong();
	return node;
}
