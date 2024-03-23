#include <toycc.h>
#include <type.h>

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

static struct Type *ty_uchar = &(struct Type){
				.kind = TY_CHAR,
				.size = sizeof(char),
				.align = sizeof(char),
				.is_unsigned = true,
};
static struct Type *ty_ushort = &(struct Type){
				.kind = TY_SHORT,
				.size = sizeof(short),
				.align = sizeof(short),
				.is_unsigned = true,
};
static struct Type *ty_uint = &(struct Type){
				.kind = TY_INT,
				.size = sizeof(int),
				.align = sizeof(int),
				.is_unsigned = true,
};
static struct Type *ty_ulong = &(struct Type){
				.kind = TY_LONG,
				.size = sizeof(long),
				.align = sizeof(long),
				.is_unsigned = true,
};

static struct Type *ty_float = &(struct Type){
				.kind = TY_FLOAT,
				.size = sizeof(float),
				.align = sizeof(float),
};
static struct Type *ty_double = &(struct Type){
				.kind = TY_DOUBLE,
				.size = sizeof(double),
				.align = sizeof(double),
};
static struct Type *ty_ldouble = &(struct Type){
				.kind = TY_LDOUBLE,
				.size = sizeof(long double),
				.align = sizeof(long double),
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

struct Type *p_ty_uchar(void)
{
	return ty_uchar;
}

struct Type *p_ty_ushort(void)
{
	return ty_ushort;
}

struct Type *p_ty_uint(void)
{
	return ty_uint;
}

struct Type *p_ty_ulong(void)
{
	return ty_ulong;
}

struct Type *p_ty_float(void)
{
	return ty_float;
}

struct Type *p_ty_double(void)
{
	return ty_double;
}

struct Type *p_ty_ldouble(void)
{
	return ty_ldouble;
}

static struct Type *new_type(enum TypeKind kind, int size, int align)
{
	struct Type *ty = calloc(1, sizeof(struct Type));
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

bool is_float(struct Type *ty)
{
	return ty->kind == TY_FLOAT ||
		ty->kind == TY_DOUBLE ||
		ty->kind == TY_LDOUBLE;
}

bool is_float_arg(struct Type *ty)
{
	return ty->kind == TY_FLOAT ||
		ty->kind == TY_DOUBLE;
}

bool is_struct_union(struct Type *ty)
{
	return ty->kind == TY_STRUCT || ty->kind == TY_UNION;
}

bool is_numeric(struct Type *ty)
{
	return is_integer(ty) || is_float(ty);
}

bool is_compatible(struct Type *t1, struct Type *t2)
{
	if (t1 == t2)
		return true;

	if (t1->origin)
		return is_compatible(t1->origin, t2);

	if (t2->origin)
		return is_compatible(t1, t2->origin);

	if (t1->kind != t2->kind)
		return false;

	switch (t1->kind) {
	case TY_CHAR:
	case TY_SHORT:
	case TY_INT:
	case TY_LONG:
		return t1->is_unsigned == t2->is_unsigned;

	case TY_FLOAT:
	case TY_DOUBLE:
	case TY_LDOUBLE:
		return true;

	case TY_PTR:
		return is_compatible(t1->base, t2->base);

	case TY_FUNC:
		if (!is_compatible(t1->return_ty, t2->return_ty))
			return false;

		if (t1->is_variadic != t2->is_variadic)
			return false;

		struct Type *p1 = t1->params;
		struct Type *p2 = t2->params;

		for (; p1 && p2; p1 = p1->next, p2 = p2->next)
			if (!is_compatible(p1, p2))
				return false;

		// number of parameters are equal
		return p1 == NULL && p2 == NULL;

	case TY_ARRAY:
		if (!is_compatible(t1->base, t2->base))
			return false;

		return t1->array_len >= 0 && t2->array_len >= 0 &&
			t1->array_len == t2->array_len;

	default:
		break;
	}
	return false;
}

struct Type *copy_type(struct Type *ty)
{
	struct Type *ret = malloc(sizeof(struct Type));
	*ret = *ty;
	ret->origin = ty;
	return ret;
}

struct Type *pointer_to(struct Type *base)
{
	struct Type *ty = new_type(TY_PTR, sizeof(long), sizeof(long));
	ty->base = base;
	ty->is_unsigned = true;
	return ty;
}

struct Type *func_type(struct Type *return_ty)
{
	// The C spec disallows sizeof(<function type>), but
	// GCC allows that and the expression is evaluated to 1.
	struct Type *ty = new_type(TY_FUNC, 1, 1);
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

struct Type *vla_of(struct Type *base, struct Node *len)
{
	struct Type *ty = new_type(TY_VLA, 8, 8);
	ty->base = base;
	ty->vla_len = len;
	return ty;
}

struct Type *enum_type(void)
{
	return new_type(TY_ENUM, 4, 4);
}

struct Type *struct_type(void)
{
	return new_type(TY_STRUCT, 0, 1);
}

static struct Type *get_common_type(struct Type *ty1, struct Type *ty2)
{
	if (ty1->base)
		return pointer_to(ty1->base);

	if (ty1->kind == TY_FUNC)
		return pointer_to(ty1);
	if (ty2->kind == TY_FUNC)
		return pointer_to(ty2);

	if (ty1->kind == TY_LDOUBLE || ty2->kind == TY_LDOUBLE)
		return p_ty_ldouble();
	if (ty1->kind == TY_DOUBLE || ty2->kind == TY_DOUBLE)
		return p_ty_double();
	if (ty1->kind == TY_FLOAT || ty2->kind == TY_FLOAT)
		return p_ty_float();

	if (ty1->size < 4)
		ty1 = p_ty_int();
	if (ty2->size < 4)
		ty2 = p_ty_int();

	if (ty1->size != ty2->size)
		return ty1->size < ty2->size ? ty2 : ty1;

	// unsigned-number takes priority over signed-number
	if (ty2->is_unsigned)
		return ty2;

	return ty1;
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
		node->ty = p_ty_int();
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

	case ND_NEG: {
		struct Type *ty = get_common_type(p_ty_int(), node->lhs->ty);
		node->lhs = new_cast(node->lhs, ty);
		node->ty = ty;
		break;
	}
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
		node->ty = node->func_ty->return_ty;
		break;

	case ND_NOT:
	case ND_LOGOR:
	case ND_LOGAND:
		node->ty = p_ty_int();
		break;

	case ND_BITNOT:
	case ND_SHL:
	case ND_SHR:
		node->ty = node->lhs->ty;
		break;

	case ND_VAR:
	case ND_VLA_PTR:
		node->ty = node->var->ty;
		break;

	case ND_COND:
		if (node->then->ty->kind == TY_VOID || node->els->ty->kind == TY_VOID) {
			node->ty = p_ty_void();
		} else {
			usual_arith_conv(&node->then, &node->els);
			node->ty = node->then->ty;
		}
		break;

	case ND_COMMA:
		node->ty = node->rhs->ty;
		break;

	case ND_MEMBER:
		node->ty = node->member->ty;
		break;

	case ND_ADDR: {
		struct Type *ty = node->lhs->ty;

		if (ty->kind == TY_ARRAY)
			// &array is the same as (array base *).
			node->ty = pointer_to(ty->base);
		else
			node->ty = pointer_to(ty);
		break;
	}
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

	case ND_LABEL_VAL:
		node->ty = pointer_to(ty_void);
		break;

	case ND_CAS:
		add_type(node->cas_addr);
		add_type(node->cas_old);
		add_type(node->cas_new);

		if (node->cas_addr->ty->kind != TY_PTR)
			error_tok(node->cas_addr->tok, "pointer expected");
		if (node->cas_old->ty->kind != TY_PTR)
			error_tok(node->cas_old->tok, "pointer expected");

		node->ty = p_ty_bool();
		break;

	case ND_EXCH:
		if (node->lhs->ty->kind != TY_PTR)
			error_tok(node->cas_addr->tok, "pointer expected");

		node->ty = node->lhs->ty->base;
		break;

	default:
		break;
	}
}
