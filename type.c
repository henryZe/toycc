#include <toycc.h>

static struct Type *ty_char = &(struct Type){
				.kind = TY_CHAR,
				.size = sizeof(char),
				.align = sizeof(char),
};
static struct Type *ty_short = &(struct Type){
				.kind = TY_SHORT,
				.size = sizeof(short),
				.align = sizeof(short),
};
static struct Type *ty_int = &(struct Type){
				.kind = TY_INT,
				.size = sizeof(int),
				.align = sizeof(int),
};
static struct Type *ty_long = &(struct Type){
				.kind = TY_LONG,
				.size = sizeof(long),
				.align = sizeof(long),
};

struct Type *p_ty_char(void)
{
	return ty_char;
}

struct Type *p_ty_short(void)
{
	return ty_short;
}

struct Type *p_ty_int(void)
{
	return ty_int;
}

struct Type *p_ty_long(void)
{
	return ty_long;
}

static struct Type *new_type(enum TypeKind kind, int size, int align)
{
	struct Type *ty = malloc(sizeof(struct Type));
	ty->kind = kind;
	ty->size = size;
	ty->align = align;
	return ty;
}

bool is_integer(struct Type *ty)
{
	return ty->kind == TY_CHAR ||
		ty->kind == TY_SHORT ||
		ty->kind == TY_INT ||
		ty->kind == TY_LONG;
}

struct Type *copy_type(struct Type *ty)
{
	struct Type *ret = malloc(sizeof(struct Type));
	*ret = *ty;
	return ret;
}

struct Type *pointer_to(struct Type *base)
{
	struct Type *ty = new_type(TY_PTR, sizeof(long), sizeof(long));
	ty->base = base;
	return ty;
}

struct Type *func_type(struct Type *return_ty)
{
	struct Type *ty = malloc(sizeof(struct Type));
	ty->kind = TY_FUNC;
	ty->return_ty = return_ty;
	return ty;
}

struct Type *array_of(struct Type *base, int len)
{
	struct Type *ty = new_type(TY_ARRAY, base->size * len, base->align);
	ty->base = base;
	ty->array_len = len;
	return ty;
}

void add_type(struct Node *node)
{
	if (!node || node->ty)
		return;

	add_type(node->lhs);
	add_type(node->rhs);
	add_type(node->cond);
	add_type(node->then);
	add_type(node->els);
	add_type(node->init);
	add_type(node->inc);

	for (struct Node *n = node->body; n; n = n->next)
		add_type(n);
	for (struct Node *n = node->args; n; n = n->next)
		add_type(n);

	switch (node->kind) {
	case ND_ADD:
	case ND_SUB:
	case ND_MUL:
	case ND_DIV:
	case ND_NEG:
		node->ty = node->lhs->ty;
		break;

	case ND_ASSIGN:
		if (node->lhs->ty->kind == TY_ARRAY)
			error_tok(node->lhs->tok, "not an lvalue");
		node->ty = node->lhs->ty;
		break;

	case ND_EQ:
	case ND_NE:
	case ND_LT:
	case ND_LE:
	case ND_NUM:
	case ND_FUNCALL:
		node->ty = ty_long;
		break;

	case ND_VAR:
		node->ty = node->var->ty;
		break;

	case ND_COMMA:
		node->ty = node->rhs->ty;
		break;

	case ND_MEMBER:
		node->ty = node->member->ty;
		break;

	case ND_ADDR:
		if (node->lhs->ty->kind == TY_ARRAY)
			// TODO: &array is not (array base *).
			node->ty = pointer_to(node->lhs->ty->base);
		else
			node->ty = pointer_to(node->lhs->ty);
		break;

	case ND_DEREF:
		if (!node->lhs->ty->base)
			error_tok(node->tok, "invalid pointer dereference");
		node->ty = node->lhs->ty->base;
		break;

	case ND_STMT_EXPR:
		if (node->body) {
			struct Node *stmt = node->body;

			while (stmt->next)
				stmt = stmt->next;

			if (stmt->kind == ND_EXPR_STMT)
				node->ty = stmt->lhs->ty;

		} else {
			error_tok(node->tok,
				"statement expression returning void is not supported");
		}
		break;

	default:
		break;
	}
}
