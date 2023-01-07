#include <toycc.h>

static struct Type *ty_void = &(struct Type){
				.kind = TY_VOID,
				.size = 1,
				.align = 1,
};
static struct Type *ty_bool = &(struct Type){
				.kind = TY_BOOL,
				.size = 1,
				.align = 1,
};
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

struct Type *p_ty_void(void)
{
	return ty_void;
}

struct Type *p_ty_bool(void)
{
	return ty_bool;
}

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
	return ty->kind == TY_BOOL ||
		ty->kind == TY_CHAR ||
		ty->kind == TY_SHORT ||
		ty->kind == TY_INT ||
		ty->kind == TY_LONG ||
		ty->kind == TY_ENUM;
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

struct Type *enum_type(void)
{
	return new_type(TY_ENUM, 4, 4);
}

static struct Type *get_common_type(struct Type *ty1, struct Type *ty2)
{
	if (ty1->base)
		return pointer_to(ty1->base);

	if (ty1->size == sizeof(long) || ty2->size == sizeof(long))
		return p_ty_long();

	return p_ty_int();
}

// For many binary operators, we implicitly promote operands so that
// both operands have the same type. Any integral type smaller than
// int is always promoted to int. If the type of one operand is larger
// than the other's (e.g. "long" vs. "int"), the smaller operand will
// be promoted to match with the other.
//
// This operation is called the "usual arithmetic conversion".
static void usual_arith_conv(struct Node **lhs, struct Node **rhs)
{
	struct Type *ty = get_common_type((*lhs)->ty, (*rhs)->ty);

	*lhs = new_cast(*lhs, ty);
	*rhs = new_cast(*rhs, ty);
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
	case ND_NUM:
		if (node->val == (int)node->val)
			node->ty = p_ty_int();
		else
			node->ty = p_ty_long();
		break;

	case ND_ADD:
	case ND_SUB:
	case ND_MUL:
	case ND_DIV:
	case ND_MOD:
	case ND_BITAND:
	case ND_BITOR:
	case ND_BITXOR:
		usual_arith_conv(&node->lhs, &node->rhs);
		node->ty = node->lhs->ty;
		break;

	case ND_NEG:
		struct Type *ty = get_common_type(p_ty_int(), node->lhs->ty);
		node->lhs = new_cast(node->lhs, ty);
		node->ty = ty;
		break;

	case ND_ASSIGN:
		if (node->lhs->ty->kind == TY_ARRAY)
			error_tok(node->lhs->tok, "not an lvalue");
		if (node->lhs->ty->kind != TY_STRUCT)
			node->rhs = new_cast(node->rhs, node->lhs->ty);
		node->ty = node->lhs->ty;
		break;

	case ND_EQ:
	case ND_NE:
	case ND_LT:
	case ND_LE:
		usual_arith_conv(&node->lhs, &node->rhs);
		node->ty = p_ty_int();
		break;

	case ND_FUNCALL:
		node->ty = p_ty_long();
		break;

	case ND_NOT:
		node->ty = p_ty_int();
		break;

	case ND_BITNOT:
		node->ty = node->lhs->ty;
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
		if (node->lhs->ty->base->kind == TY_VOID)
			error_tok(node->tok, "dereferencing a void pointer");

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
