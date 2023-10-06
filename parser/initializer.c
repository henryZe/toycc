#include <initializer.h>
#include <parser.h>
#include <type.h>

static struct Initializer *new_initializer(struct Type *ty, bool is_flexible)
{
	struct Initializer *init = calloc(1, sizeof(struct Initializer));

	init->ty = ty;
	if (ty->kind == TY_ARRAY) {
		if (is_flexible && ty->size < 0) {
			init->is_flexible = true;
			return init;
		}

		init->children = malloc(ty->array_len * sizeof(struct Initializer *));

		for (int i = 0; i < ty->array_len; i++)
			init->children[i] = new_initializer(ty->base, false);
	}

	if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
		// count the number of struct members
		int len = 0;
		struct Member *start = ty->members;
		for (struct Member *mem = start; mem; mem = mem->next)
			len++;

		init->children = malloc(len * sizeof(struct Initializer *));

		for (struct Member *mem = start; mem; mem = mem->next) {
			if (is_flexible && ty->is_flexible && !mem->next) {
				struct Initializer *child = calloc(1, sizeof(struct Initializer));

				child->ty = mem->ty;
				child->is_flexible = true;
				init->children[mem->idx] = child;

			} else {
				init->children[mem->idx] = new_initializer(mem->ty, false);
			}
		}
	}

	return init;
}

static struct Token *skip_excess_element(struct Token *tok)
{
	if (equal(tok, "{")) {
		tok = skip_excess_element(tok->next);
		return skip(tok, "}");
	}

	assign(&tok, tok);
	return tok;
}

// string-initializer = string-literal
static void string_initializer(struct Token **rest, struct Token *tok,
			       struct Initializer *init)
{
	if (init->is_flexible)
		*init = *new_initializer(array_of(init->ty->base, tok->ty->array_len), false);

	int len = MIN(init->ty->array_len, tok->ty->array_len);

	for (int i = 0; i < len; i++)
		init->children[i]->expr = new_num(tok->str[i], tok);

	*rest = tok->next;
}

static void initializer2(struct Token **rest, struct Token *tok,
			 struct Initializer *init);
static int count_array_init_elements(struct Token *tok, struct Type *ty)
{
	struct Initializer *dummy = new_initializer(ty->base, false);
	int i;

	for (i = 0; !consume_end(&tok, tok); i++) {
		if (i > 0)
			tok = skip(tok, ",");
		initializer2(&tok, tok, dummy);
	}
	return i;
}

static bool is_end(struct Token *tok)
{
	return equal(tok, "}") || (equal(tok, ",") && equal(tok->next, "}"));
}

// array-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void array_initializer1(struct Token **rest, struct Token *tok,
				struct Initializer *init)
{
	tok = skip(tok, "{");

	if (init->is_flexible) {
		int len = count_array_init_elements(tok, init->ty);
		*init = *new_initializer(array_of(init->ty->base, len), false);
	}

	for (int i = 0; !consume_end(rest, tok); i++) {
		if (i > 0)
			tok = skip(tok, ",");

		if (i < init->ty->array_len)
			initializer2(&tok, tok, init->children[i]);
		else
			// ignore excess elements
			tok = skip_excess_element(tok);
	}
}

// array-initializer2 = initializer ("," initializer)*
static void array_initializer2(struct Token **rest, struct Token *tok,
				struct Initializer *init)
{
	if (init->is_flexible) {
		int len = count_array_init_elements(tok, init->ty);
		*init = *new_initializer(array_of(init->ty->base, len), false);
	}

	for (int i = 0; i < init->ty->array_len && !is_end(tok); i++) {
		if (i > 0)
			tok = skip(tok, ",");
		initializer2(&tok, tok, init->children[i]);
	}

	*rest = tok;
}

// struct-initializer1 = "{" initializer ("," initializer)* ","? "}"
static void struct_initializer1(struct Token **rest, struct Token *tok,
				struct Initializer *init)
{
	tok = skip(tok, "{");

	bool first = true;
	struct Member *mem = init->ty->members;

	while (!consume_end(rest, tok)) {
		if (!first)
			tok = skip(tok, ",");
		first = false;

		if (mem) {
			initializer2(&tok, tok, init->children[mem->idx]);
			mem = mem->next;
		} else {
			// ignore excess elements
			tok = skip_excess_element(tok);
		}
	}
}

// struct-initializer2 = initializer ("," initializer)*
static void struct_initializer2(struct Token **rest, struct Token *tok,
				struct Initializer *init)
{
	bool first = true;

	for (struct Member *mem = init->ty->members;
		mem && !is_end(tok); mem = mem->next) {
		if (!first)
			tok = skip(tok, ",");
		first = false;
		initializer2(&tok, tok, init->children[mem->idx]);
	}

	*rest = tok;
}

static void union_initializer(struct Token **rest, struct Token *tok,
			      struct Initializer *init)
{
	bool parentheses = equal(tok, "{");
	if (parentheses)
		tok = tok->next;

	// Unlike structs, union initializers take *only one* initializer,
	// and that initializes the *first* union member.
	initializer2(&tok, tok, init->children[0]);

	if (parentheses) {
		consume(&tok, tok, ",");
		*rest = skip(tok, "}");
	} else {
		*rest = tok;
	}
}

// initializer = string-initializer | array-initializer |
//		 struct_initializer | union-initializer |
//		 assign
static void initializer2(struct Token **rest, struct Token *tok,
			 struct Initializer *init)
{
	if (init->ty->kind == TY_ARRAY && tok->kind == TK_STR) {
		string_initializer(rest, tok, init);
		return;
	}

	if (init->ty->kind == TY_ARRAY) {
		if (equal(tok, "{"))
			array_initializer1(rest, tok, init);
		else
			array_initializer2(rest, tok, init);
		return;
	}

	if (init->ty->kind == TY_STRUCT) {
		if (equal(tok, "{")) {
			struct_initializer1(rest, tok, init);
			return;
		}

		// A struct can be initialized with another struct. E.g.
		// `struct T x = y;` where y is a variable of type `struct T`.
		// Handle that case first.
		struct Node *expr = assign(rest, tok);
		add_type(expr);
		if (expr->ty->kind == TY_STRUCT) {
			init->expr = expr;
			return;
		}

		struct_initializer2(rest, tok, init);
		return;
	}

	if (init->ty->kind == TY_UNION) {
		union_initializer(rest, tok, init);
		return;
	}

	if (equal(tok, "{")) {
		// An initializer for a scalar variable can be surrounded by
		// braces.
		// E.g. `int x = {3};`. Handle that case.
		initializer2(&tok, tok->next, init);
		*rest = skip(tok, "}");
		return;
	}

	init->expr = assign(rest, tok);
}

static struct Type *copy_struct_type(struct Type *ty)
{
	ty = copy_type(ty);

	struct Member head = {};
	struct Member *cur = &head;

	for (struct Member *mem = ty->members; mem; mem = mem->next) {
		struct Member *m = malloc(sizeof(struct Member));
		*m = *mem;

		cur->next = m;
		cur = cur->next;
	}

	ty->members = head.next;
	return ty;
}

static struct Initializer *initializer(struct Token **rest, struct Token *tok,
				       struct Type *ty, struct Type **new_ty)
{
	// allocate initializer
	struct Initializer *init = new_initializer(ty, true);
	// assign expr to initializer
	initializer2(rest, tok, init);

	if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->is_flexible) {
		// allocate new ty, just for calculate the size of the last member
		ty = copy_struct_type(ty);

		struct Member *mem = ty->members;
		while (mem->next)
			mem = mem->next;

		mem->ty = init->children[mem->idx]->ty;
		ty->size += mem->ty->size;

		*new_ty = ty;
		return init;
	}

	*new_ty = init->ty;
	return init;
}

static struct Node *init_desg_expr(struct InitDesg *desg, struct Token *tok)
{
	// last one which is the variable self
	if (desg->var)
		return new_var_node(desg->var, tok);

	if (desg->member) {
		struct Node *node = new_unary(ND_MEMBER, init_desg_expr(desg->next, tok), tok);
		node->member = desg->member;
		return node;
	}

	struct Node *lhs = init_desg_expr(desg->next, tok);
	struct Node *rhs = new_num(desg->idx, tok);
	// x[a] => *(x + a)
	return new_unary(ND_DEREF, new_add(lhs, rhs, tok), tok);
}

static struct Node *create_lvar_init(struct Initializer *init, struct Type *ty,
				     struct InitDesg *desg, struct Token *tok)
{
	if (ty->kind == TY_ARRAY) {
		struct Node *node = new_node(ND_NULL_EXPR, tok);

		for (int i = 0; i < ty->array_len; i++) {
			struct InitDesg desg2 = { desg, i, NULL, NULL };
			struct Node *rhs = create_lvar_init(init->children[i],
							    ty->base, &desg2, tok);
			// node, x[a] = expr
			node = new_binary(ND_COMMA, node, rhs, tok);
		}
		return node;
	}

	if (ty->kind == TY_STRUCT && !init->expr) {
		struct Node *node = new_node(ND_NULL_EXPR, tok);

		for (struct Member *mem = ty->members; mem; mem = mem->next) {
			struct InitDesg desg2 = { desg, 0, mem, NULL };
			struct Node *rhs = create_lvar_init(init->children[mem->idx],
							    mem->ty, &desg2, tok);
			// node, x.a = expr
			node = new_binary(ND_COMMA, node, rhs, tok);
		}
		return node;
	}

	if (ty->kind == TY_UNION) {
		struct InitDesg desg2 = { desg, 0, ty->members, NULL };
		return create_lvar_init(init->children[0], ty->members->ty, &desg2, tok);
	}

	if (!init->expr)
		// there is no user-supplied values
		return new_node(ND_NULL_EXPR, tok);

	struct Node *lhs = init_desg_expr(desg, tok);
	struct Node *rhs = init->expr;
	// x[a] = expr;
	return new_binary(ND_ASSIGN, lhs, rhs, tok);
}

// A variable definition with an initializer is a shorthand notation
// for a variable definition followed by assignments. This function
// generates assignment expressions for an initializer. For example,
// `int x[2][2] = {{6, 7}, {8, 9}}` is converted to the following
// expressions:
//
//   x[0][0] = 6;
//   x[0][1] = 7;
//   x[1][0] = 8;
//   x[1][1] = 9;
struct Node *lvar_initializer(struct Token **rest, struct Token *tok,
				     struct Obj *var)
{
	struct Initializer *init = initializer(rest, tok, var->ty, &var->ty);
	struct InitDesg desg = { NULL, 0, NULL, var };

	// If a partial initializer list is given, the standard requires
	// that unspecified elements are set to 0. Here, we simply
	// zero-initialize the entire memory region of a variable before
	// initializing it with user-supplied values.
	struct Node *lhs = new_node(ND_MEMZERO, tok);
	lhs->var = var;

	// initializing var with user-supplied values.
	struct Node *rhs = create_lvar_init(init, var->ty, &desg, tok);
	return new_binary(ND_COMMA, lhs, rhs, tok);
}

static void write_buf(char *buf, uint64_t val, int sz)
{
	if (sz == 1)
		*buf = val;
	else if (sz == 2)
		*(uint16_t *)buf = val;
	else if (sz == 4)
		*(uint32_t *)buf = val;
	else if (sz == 8)
		*(uint64_t *)buf = val;
	else
		unreachable();
}

static uint64_t read_buf(const char *buf, int sz)
{
	if (sz == 1)
		return *buf;
	if (sz == 2)
		return *(uint16_t *)buf;
	if (sz == 4)
		return *(uint32_t *)buf;
	if (sz == 8)
		return *(uint64_t *)buf;
	unreachable();
}

static struct Relocation *write_gvar_data(struct Relocation *cur,
					  struct Initializer *init,
					  struct Type *ty, char *buf,
					  int offset)
{
	if (ty->kind == TY_ARRAY) {
		int sz = ty->base->size;
		for (int i = 0; i < ty->array_len; i++)
			cur = write_gvar_data(cur, init->children[i],
					ty->base, buf, offset + sz * i);
		return cur;
	}

	if (ty->kind == TY_STRUCT) {
		for (struct Member *mem = ty->members; mem; mem = mem->next) {
			if (mem->is_bitfield) {
				struct Node *expr = init->children[mem->idx]->expr;
				if (!expr)
					break;

				char *loc = buf + offset + mem->offset;

				uint64_t oldval = read_buf(loc, mem->ty->size);
				uint64_t newval = eval(expr);
				uint64_t mask = (1L << mem->bit_width) - 1;
				uint64_t combined = oldval | ((newval & mask) << mem->bit_offset);

				write_buf(loc, combined, mem->ty->size);
			} else {
				cur = write_gvar_data(cur, init->children[mem->idx],
						mem->ty, buf, offset + mem->offset);
			}
		}
		return cur;
	}

	if (ty->kind == TY_UNION)
		return write_gvar_data(cur, init->children[0],
					ty->members->ty, buf, offset);

	if (!init->expr)
		return cur;

	if (ty->kind == TY_FLOAT) {
		*(float *)(buf + offset) = eval_double(init->expr);
		return cur;
	}

	if (ty->kind == TY_DOUBLE) {
		*(double *)(buf + offset) = eval_double(init->expr);
		return cur;
	}

	const char *label = NULL;
	uint64_t val = eval2(init->expr, &label);

	if (!label) {
		write_buf(buf + offset, val, ty->size);
		return cur;
	}

	struct Relocation *rel = calloc(1, sizeof(struct Relocation));
	rel->offset = offset;
	rel->label = label;
	rel->addend = val;

	cur->next = rel;
	return rel;
}

// Initializers for global variables are evaluated at compile-time and
// embedded to .data section. This function serializes Initializer
// objects to a flat byte array. It is a compile error if an
// initializer list contains a non-constant expression.
void gvar_initializer(struct Token **rest, struct Token *tok,
			     struct Obj *var)
{
	struct Initializer *init = initializer(rest, tok, var->ty, &var->ty);
	struct Relocation head = {};
	char *buf = malloc(var->ty->size);

	write_gvar_data(&head, init, var->ty, buf, 0);
	var->init_data = buf;
	var->rel = head.next;
}
