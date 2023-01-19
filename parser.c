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

// Scope for local variables, global variables,
// typedefs or enum constants
struct VarScope {
	struct VarScope *next;
	const char *name;
	struct Obj *var;
	struct Type *type_def;
	struct Type *enum_ty;
	int enum_val;
};

// Scope for struct, union or enum tags
struct TagScope {
	struct TagScope *next;
	const char *name;
	struct Type *ty;
};

// represents a block scope
struct Scope {
	struct Scope *next;

	// C has two block scopes:
	// one is for variables/typedefs
	struct VarScope *vars;
	// and the other is for struct/union/enum tags.
	struct TagScope *tags;
};

// Variable attributes such as typedef or extern.
struct VarAttr {
	bool is_typedef;
	bool is_static;
};

// This struct represents a variable initializer. Since initializers
// can be nested (e.g. `int x[2][2] = {{1, 2}, {3, 4}}`), this struct
// is a tree data structure.
struct Initializer {
	struct Initializer *next;

	struct Type *ty;
	struct Token *tok;

	// If it's not an aggregate type and has an initializer,
	// `expr` has an initialization expression.
	struct Node *expr;

	// If it's an initializer for an aggregate type (e.g. array or struct),
	// `children` has initializers for its children.
	struct Initializer **children;
};

// For local variable initializer.
struct InitDesg {
	// former level of array
	struct InitDesg *next;
	int idx;
	struct Obj *var;
};

// All local variable instances created during parsing are
// accumulated to this list.
static struct Obj *locals;
// Likewise, global variables are accumulated to this list.
static struct Obj *globals;

static struct Scope *scope = &(struct Scope){};

// Points to the function object the parser is currently parsing.
static struct Obj *current_fn;

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

static struct Node *new_num(int64_t val, struct Token *tok)
{
	struct Node *node = new_node(ND_NUM, tok);
	node->val = val;
	node->tok = tok;
	return node;
}

static struct Node *new_long(int64_t val, struct Token *tok)
{
	struct Node *node = new_node(ND_NUM, tok);
	node->val = val;
	node->ty = p_ty_long();
	return node;
}

struct Node *new_cast(struct Node *expr, struct Type *ty)
{
	add_type(expr);

	struct Node *n = malloc(sizeof(struct Node));

	n->kind = ND_CAST;
	n->tok = expr->tok;
	n->lhs = expr;
	// explicit conversion of type
	n->ty = copy_type(ty);
	return n;
}

static void enter_scope(void)
{
	struct Scope *sc = malloc(sizeof(struct Scope));
	sc->vars = NULL;
	sc->next = scope;
	scope = sc;
}

static void leave_scope(void)
{
	scope = scope->next;
}

static struct VarScope *find_var(struct Token *tok)
{
	for (struct Scope *sc = scope; sc; sc = sc->next)
		for (struct VarScope *vars = sc->vars; vars; vars = vars->next)
			if (equal(tok, vars->name))
				return vars;
	return NULL;
}

static struct Type *find_tag(struct Token *tok)
{
	for (struct Scope *sc = scope; sc; sc = sc->next)
		for (struct TagScope *tag = sc->tags; tag; tag = tag->next)
			if (equal(tok, tag->name))
				return tag->ty;
	return NULL;
}

static void push_tag_scope(struct Token *tok, struct Type *ty)
{
	struct TagScope *sc = malloc(sizeof(struct TagScope));
	sc->name = strndup(tok->loc, tok->len);
	sc->ty = ty;
	sc->next = scope->tags;
	scope->tags = sc;
}

static struct VarScope *push_scope(const char *name)
{
	struct VarScope *sc = malloc(sizeof(struct VarScope));
	sc->name = name;
	sc->next = scope->vars;
	scope->vars = sc;
	return sc;
}

// create variable and link to `locals` list
static struct Obj *new_var(const char *name, struct Type *ty)
{
	struct Obj *var = malloc(sizeof(struct Obj));
	var->name = name;
	var->ty = ty;
	push_scope(name)->var = var;
	return var;
}

static struct Obj *new_lvar(const char *name, struct Type *ty)
{
	struct Obj *var = new_var(name, ty);
	var->is_local = true;
	var->next = locals;
	locals = var;
	return var;
}

static struct Obj *new_gvar(const char *name, struct Type *ty)
{
	struct Obj *var = new_var(name, ty);
	var->is_local = false;
	var->next = globals;
	globals = var;
	return var;
}

static const char *new_unique_name(void)
{
	static int id = 0;

	return format(".L..%d", id++);
}

// anonymous global variable
static struct Obj *new_anon_gvar(struct Type *ty)
{
	return new_gvar(new_unique_name(), ty);
}

static struct Obj *new_string_literal(const char *p, struct Type *ty)
{
	struct Obj *var = new_anon_gvar(ty);
	var->init_data = p;
	return var;
}

// parse AST(abstract syntax tree)
// expr -> assign -> equality -> relational -> add -> mul ->
// unary -> postfix -> primary(num -> identifier -> bracket) ->
// expr -> ...
// expr:
// 	tok: current tok pointer
// 	rest: return current tok pointer
static struct Node *expr(struct Token **rest, struct Token *tok);
static struct Node *assign(struct Token **rest, struct Token *tok);
static struct Node *new_add(struct Node *lhs, struct Node *rhs, struct Token *tok);
static struct Node *unary(struct Token **rest, struct Token *tok);
static struct Node *compound_stmt(struct Token **rest, struct Token *tok);

// funcall = ident "(" (assign ("," assign)*)? ")"
static struct Node *funcall(struct Token **rest, struct Token *tok)
{
	struct Token *start = tok;
	tok = tok->next->next;

	struct VarScope *sc = find_var(start);
	if (!sc)
		error_tok(start, "implicit declaration of a function");
	if (!sc->var || sc->var->ty->kind != TY_FUNC)
		error_tok(start, "not a function");

	struct Type *ty = sc->var->ty;
	struct Type *param_ty = ty->params;

	struct Node head = {};
	struct Node *cur = &head;

	while (!equal(tok, ")")) {
		if (cur != &head)
			tok = skip(tok, ",");

		struct Node *arg = assign(&tok, tok);
		add_type(arg);

		if (param_ty) {
			if (param_ty->kind == TY_STRUCT || param_ty->kind == TY_UNION) {
				error_tok(arg->tok, "passing struct or union is not supported yet");
			}
			arg = new_cast(arg, param_ty);
			param_ty = param_ty->next;
		}

		cur->next = arg;
		cur = cur->next;
	}
	*rest = skip(tok, ")");

	struct Node *node = new_node(ND_FUNCALL, tok);
	node->funcname = strndup(start->loc, start->len);
	node->ty = ty->return_ty;
	node->args = head.next;

	return node;
}

static struct Type *type_suffix(struct Token **rest, struct Token *tok, struct Type *ty);
// abstract_declarator = "*"* ("(" abstract-declarator ")")? type-suffix
static struct Type *abstract_declarator(struct Token **rest, struct Token *tok, struct Type *ty)
{
	// like "sizeof(char *)"
	while (equal(tok, "*")) {
		ty = pointer_to(ty);
		tok = tok->next;
	}

	// like "sizeof(char(*)[4])"
	if (equal(tok, "(")) {
		struct Token *start = tok->next;
		struct Type dummy = {};

		// get 'type' & update 'rest'
		abstract_declarator(&tok, start, &dummy);
		tok = skip(tok, ")");
		ty = type_suffix(rest, tok, ty);

		return abstract_declarator(&tok, start, ty);
	}

	// like "sizeof(char[4][4])"
	return type_suffix(rest, tok, ty);
}

static struct Type *declspec(struct Token **rest, struct Token *tok, struct VarAttr *attr);
static struct Type *typename(struct Token **rest, struct Token *tok)
{
	struct Type *base = declspec(&tok, tok, NULL);
	return abstract_declarator(rest, tok, base);
}

static struct Type *find_typedef(struct Token *tok)
{
	if (tok->kind == TK_IDENT) {
		struct VarScope *sc = find_var(tok);
		if (sc)
			return sc->type_def;
	}
	return NULL;
}

// Returns true if a given token represents a type.
static bool is_typename(struct Token *tok)
{
	static const char * const kw[] = {
		"void",
		"_Bool",
		"char",
		"short",
		"int",
		"long",
		"struct",
		"union",
		"typedef",
		"enum",
		"static",
	};

	for (size_t i = 0; i < ARRAY_SIZE(kw); i++)
		if (equal(tok, kw[i]))
			return true;

	return find_typedef(tok);
}

// primary = "(" "{" stmt+ "}" ")"
// 	| "(" expr ")"
//	| "sizeof" "(" type-name ")"
// 	| "sizeof" unary
// 	| ident (func-args)?
// 	| str
// 	| num
static struct Node *primary(struct Token **rest, struct Token *tok)
{
	struct Token *start = tok;

	if (equal(tok, "(") && equal(tok->next, "{")) {
		// This is a GNU statement expression.
		struct Node *n = new_node(ND_STMT_EXPR, tok);

		n->body = compound_stmt(&tok, tok->next->next)->body;
		*rest = skip(tok, ")");
		return n;
	}

	if (equal(tok, "(")) {
		struct Node *node = expr(&tok, tok->next);
		*rest = skip(tok, ")");
		return node;
	}

	if (equal(tok, "sizeof") && equal(tok->next, "(") && is_typename(tok->next->next)) {
		struct Type *ty = typename(&tok, tok->next->next);
		// update rest
		*rest = skip(tok, ")");
		return new_num(ty->size, start);
	}

	if (equal(tok, "sizeof")) {
		// update rest pointer
		struct Node *node = unary(rest, tok->next);

		add_type(node);
		return new_num(node->ty->size, tok);
	}

	if (tok->kind == TK_IDENT) {
		// function call
		if (equal(tok->next, "("))
			return funcall(rest, tok);

		// variable or enum constant
		struct VarScope *sc = find_var(tok);
		if (!sc || (!sc->var && !sc->enum_ty))
			error_tok(tok, "undefined variable or enum constant");

		struct Node *n;
		if (sc->var)
			// variable
			n = new_var_node(sc->var, tok);
		else
			// enum constant
			n = new_num(sc->enum_val, tok);
		*rest = tok->next;
		return n;
	}

	if (tok->kind == TK_STR) {
		struct Obj *var = new_string_literal(tok->str, tok->ty);
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

static struct Member *get_struct_member(struct Type *ty, struct Token *tok)
{
	for (struct Member *mem = ty->members; mem; mem = mem->next)
		if (mem->name->len == tok->len &&
			!strncmp(mem->name->loc, tok->loc, tok->len))
			return mem;

	error_tok(tok, "no such member");
}

static struct Node *struct_ref(struct Node *lhs, struct Token *tok)
{
	add_type(lhs);

	if (lhs->ty->kind != TY_STRUCT && lhs->ty->kind != TY_UNION)
		error_tok(lhs->tok, "not a struct nor a union");

	struct Node *n = new_unary(ND_MEMBER, lhs, tok);
	n->member = get_struct_member(lhs->ty, tok);
	return n;
}

// Convert `A op= B` to `tmp = &A, *tmp = *tmp op B`
// where tmp is a fresh pointer variable.
static struct Node *to_assign(struct Node *binary)
{
	add_type(binary->lhs);
	add_type(binary->rhs);

	struct Token *tok = binary->tok;
	// var tmp
	struct Obj *var = new_lvar("", pointer_to(binary->lhs->ty));
	// tmp = &A
	struct Node *expr1 = new_binary(ND_ASSIGN, new_var_node(var, tok),
					new_unary(ND_ADDR, binary->lhs, tok),
					tok);
	// *tmp = *tmp op B
	struct Node *expr2 = new_binary(ND_ASSIGN,
					new_unary(ND_DEREF, new_var_node(var, tok), tok),
					new_binary(binary->kind,
						   new_unary(ND_DEREF, new_var_node(var, tok), tok),
						   binary->rhs, tok),
					tok);

	return new_binary(ND_COMMA, expr1, expr2, tok);
}

// Convert A++ to `(typeof A)((A += 1) - 1)`
static struct Node *new_inc_dec(struct Node *node, struct Token *tok, int addend)
{
	add_type(node);
	return new_cast(new_add(to_assign(new_add(node, new_num(addend, tok), tok)),
				new_num(-addend, tok), tok),
			node->ty);
}

// postfix = primary ("[" expr "]" | "." ident | "->" ident | "++" | "--")*
static struct Node *postfix(struct Token **rest, struct Token *tok)
{
	struct Node *node = primary(&tok, tok);

	while (1) {
		if (equal(tok, "[")) {
			// x[y] is short for *(x+y)
			struct Token *start = tok;
			struct Node *idx = expr(&tok, tok->next);

			tok = skip(tok, "]");
			node = new_unary(ND_DEREF, new_add(node, idx, start), start);
			continue;
		}

		if (equal(tok, ".")) {
			node = struct_ref(node, tok->next);
			// skip ident
			tok = tok->next->next;
			continue;
		}

		if (equal(tok, "->")) {
			// x->y is short for (*x).y
			node = new_unary(ND_DEREF, node, tok);
			node = struct_ref(node, tok->next);
			// skip member
			tok = tok->next->next;
			continue;
		}

		if (equal(tok, "++")) {
			node = new_inc_dec(node, tok, 1);
			tok = tok->next;
			continue;
		}

		if (equal(tok, "--")) {
			node = new_inc_dec(node, tok, -1);
			tok = tok->next;
			continue;
		}

		break;
	}

	*rest = tok;
	return node;
}

// cast = "(" type-name ")" cast | unary
static struct Node *cast(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "(") && is_typename(tok->next)) {
		struct Token *start = tok;
		struct Type *ty = typename(&tok, tok->next);
		tok = skip(tok, ")");

		struct Node *n = new_cast(cast(rest, tok), ty);
		n->tok = start;
		return n;
	}

	return unary(rest, tok);
}

// mul = cast ("*" cast | "/" cast | "%" cast)*
static struct Node *mul(struct Token **rest, struct Token *tok)
{
	struct Node *node = cast(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "*")) {
			node = new_binary(ND_MUL, node, cast(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "/")) {
			node = new_binary(ND_DIV, node, cast(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "%")) {
			node = new_binary(ND_MOD, node, cast(&tok, tok->next), start);
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
	rhs = new_binary(ND_MUL, rhs, new_long(lhs->ty->base->size, tok), tok);
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
		return new_binary(ND_DIV, node, new_num(lhs->ty->base->size, tok), tok);
	}

	// ptr - num
	if (lhs->ty->base && is_integer(rhs->ty)) {
		rhs = new_binary(ND_MUL, rhs, new_long(lhs->ty->base->size, tok), tok);
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

static struct Node *to_assign(struct Node *binary);
// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
// 	 | ("++" | "--") unary
// 	 | postfix
static struct Node *unary(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "+"))
		// ignore
		return cast(rest, tok->next);

	if (equal(tok, "-"))
		// depth--
		return new_unary(ND_NEG, cast(rest, tok->next), tok);

	if (equal(tok, "&"))
		return new_unary(ND_ADDR, cast(rest, tok->next), tok);

	if (equal(tok, "*"))
		return new_unary(ND_DEREF, cast(rest, tok->next), tok);

	if (equal(tok, "!"))
		return new_unary(ND_NOT, cast(rest, tok->next), tok);

	if (equal(tok, "~"))
		return new_unary(ND_BITNOT, cast(rest, tok->next), tok);

	// read ++i as i+=1
	if (equal(tok, "++"))
		return to_assign(new_add(unary(rest, tok->next),
					 new_num(1, tok), tok));

	// read --i as i-=1
	if (equal(tok, "--"))
		return to_assign(new_sub(unary(rest, tok->next),
					 new_num(1, tok), tok));

	return postfix(rest, tok);
}

// shift = add ("<<" add | ">>" add)*
static struct Node *shift(struct Token **rest, struct Token *tok)
{
	struct Node *node = add(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "<<")) {
			node = new_binary(ND_SHL, node, add(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, ">>")) {
			node = new_binary(ND_SHR, node, add(&tok, tok->next), start);
			continue;
		}

		*rest = tok;
		return node;
	}
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static struct Node *relational(struct Token **rest, struct Token *tok)
{
	struct Node *node = shift(&tok, tok);

	while (1) {
		struct Token *start = tok;

		if (equal(tok, "<")) {
			node = new_binary(ND_LT, node, shift(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, "<=")) {
			node = new_binary(ND_LE, node, shift(&tok, tok->next), start);
			continue;
		}

		if (equal(tok, ">")) {
			node = new_binary(ND_LT, shift(&tok, tok->next), node, start);
			continue;
		}

		if (equal(tok, ">=")) {
			node = new_binary(ND_LE, shift(&tok, tok->next), node, start);
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

// bitand = equality ("&" equality)*
static struct Node *bitand(struct Token **rest, struct Token *tok)
{
	struct Node *node = equality(&tok, tok);

	while (equal(tok, "&")) {
		struct Token *start = tok;
		node = new_binary(ND_BITAND, node, equality(&tok, tok->next), start);
	}

	*rest = tok;
	return node;
}

// bitxor = bitand ("&" bitand)*
static struct Node *bitxor(struct Token **rest, struct Token *tok)
{
	struct Node *node = bitand(&tok, tok);

	while (equal(tok, "^")) {
		struct Token *start = tok;
		node = new_binary(ND_BITXOR, node, bitand(&tok, tok->next), start);
	}

	*rest = tok;
	return node;
}

// bitor = bitxor ("&" bitxor)*
static struct Node *bitor(struct Token **rest, struct Token *tok)
{
	struct Node *node = bitxor(&tok, tok);

	while (equal(tok, "|")) {
		struct Token *start = tok;
		node = new_binary(ND_BITOR, node, bitxor(&tok, tok->next), start);
	}

	*rest = tok;
	return node;
}

// logand = bitor ("&&" bitor)*
static struct Node *logand(struct Token **rest, struct Token *tok)
{
	struct Node *node = bitor(&tok, tok);
	while (equal(tok, "&&")) {
		struct Token *start = tok;
		node = new_binary(ND_LOGAND, node, bitor(&tok, tok->next), start);
	}
	*rest = tok;
	return node;
}

// logor = logand ("||" logand)*
static struct Node *logor(struct Token **rest, struct Token *tok)
{
	struct Node *node = logand(&tok, tok);
	while (equal(tok, "||")) {
		struct Token *start = tok;
		node = new_binary(ND_LOGOR, node, logand(&tok, tok->next), start);
	}
	*rest = tok;
	return node;
}

// conditional = logor ("?" expr ":" conditional)?
static struct Node *conditional(struct Token **rest, struct Token *tok)
{
	struct Node *cond = logor(&tok, tok);

	if (!equal(tok, "?")) {
		*rest = tok;
		return cond;
	}

	struct Node *node = new_node(ND_COND, tok);
	node->cond = cond;
	node->then = expr(&tok, tok->next);

	tok = skip(tok, ":");
	node->els = conditional(rest, tok);
	return node;
}

// Evaluate a given node as a constant expression.
static int64_t eval(struct Node *node)
{
	add_type(node);

	switch (node->kind) {
	case ND_ADD:
		return eval(node->lhs) + eval(node->rhs);
	case ND_SUB:
		return eval(node->lhs) - eval(node->rhs);
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
		return eval(node->cond) ? eval(node->then) : eval(node->els);
	case ND_COMMA:
		return eval(node->rhs);
	case ND_NOT:
		return !eval(node->lhs);
	case ND_BITNOT:
		return ~eval(node->lhs);
	case ND_LOGAND:
		return eval(node->lhs) && eval(node->rhs);
	case ND_LOGOR:
		return eval(node->lhs) || eval(node->rhs);
	case ND_CAST:
		if (is_integer(node->ty)) {
			switch (node->ty->size) {
			case 1:
				return (uint8_t)eval(node->lhs);
			case 2:
				return (uint16_t)eval(node->lhs);
			case 4:
				return (uint32_t)eval(node->lhs);
			}
		}
		return eval(node->lhs);
	case ND_NUM:
		return node->val;
	default:
		error_tok(node->tok, "not a compile-time constant");
	}
}

static int64_t const_expr(struct Token **rest, struct Token *tok)
{
	struct Node *n = conditional(rest, tok);
	return eval(n);
}

// assign    = conditional (assign-op assign)?
// assign-op = "=" | "+=" | "-=" | "*=" | "/=" | "%="
//	     | "&=" | "|=" | "^=" | "<<=" | ">>="
static struct Node *assign(struct Token **rest, struct Token *tok)
{
	struct Node *node = conditional(&tok, tok);

	if (equal(tok, "="))
		return new_binary(ND_ASSIGN, node, assign(rest, tok->next), tok);

	if (equal(tok, "+="))
		return to_assign(new_add(node, assign(rest, tok->next), tok));

	if (equal(tok, "-="))
		return to_assign(new_sub(node, assign(rest, tok->next), tok));

	if (equal(tok, "*="))
		return to_assign(new_binary(ND_MUL, node, assign(rest, tok->next), tok));

	if (equal(tok, "/="))
		return to_assign(new_binary(ND_DIV, node, assign(rest, tok->next), tok));

	if (equal(tok, "%="))
		return to_assign(new_binary(ND_MOD, node, assign(rest, tok->next), tok));

	if (equal(tok, "&="))
		return to_assign(new_binary(ND_BITAND, node, assign(rest, tok->next), tok));

	if (equal(tok, "|="))
		return to_assign(new_binary(ND_BITOR, node, assign(rest, tok->next), tok));

	if (equal(tok, "^="))
		return to_assign(new_binary(ND_BITXOR, node, assign(rest, tok->next), tok));

	if (equal(tok, "<<="))
		return to_assign(new_binary(ND_SHL, node, assign(rest, tok->next), tok));

	if (equal(tok, ">>="))
		return to_assign(new_binary(ND_SHR, node, assign(rest, tok->next), tok));

	*rest = tok;
	return node;
}

// expr = assign ("," expr)?
static struct Node *expr(struct Token **rest, struct Token *tok)
{
	struct Node *n = assign(&tok, tok);

	if (equal(tok, ","))
		return new_binary(ND_COMMA, n, expr(rest, tok->next), tok);

	*rest = tok;
	return n;
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

static struct Type *declarator(struct Token **rest, struct Token *tok, struct Type *ty);

// struct-members = (declspec declarator ("," declarator)* ";")*
static void struct_members(struct Token **rest, struct Token *tok, struct Type *ty)
{
	struct Member head = {};
	struct Member *cur = &head;

	while (!equal(tok, "}")) {
		struct Type *basety = declspec(&tok, tok, NULL);
		int i = 0;

		while (!consume(&tok, tok, ";")) {
			if (i++)
				tok = skip(tok, ",");

			struct Member *mem = malloc(sizeof(struct Member));
			mem->ty = declarator(&tok, tok, basety);
			mem->name = mem->ty->name;
			cur->next = mem;
			cur = cur->next;
		}
	}

	// skip "}"
	*rest = tok->next;
	ty->members = head.next;
}

// struct-union-decl = ident? ("{" struct-members)?
static struct Type *struct_union_decl(struct Token **rest, struct Token *tok)
{
	// read a struct/union tag
	struct Token *tag = NULL;

	if (tok->kind == TK_IDENT) {
		tag = tok;
		tok = tok->next;
	}

	// not struct/union declaration but variable
	if (tag && !equal(tok, "{")) {
		*rest = tok;

		struct Type *ty = find_tag(tag);
		if (ty)
			return ty;

		// no struct/union declare yet
		ty = struct_type();
		ty->size = -1;
		push_tag_scope(tag, ty);
		return ty;
	}

	tok = skip(tok, "{");

	// Construct a struct object
	struct Type *ty = struct_type();
	struct_members(rest, tok, ty);

	// register the struct type if a name was given
	if (tag) {
		// If there is a redefinition, overwrite a previous type.
		for (struct TagScope *sc = scope->tags; sc; sc = sc->next) {
			if (equal(tag, sc->name)) {
				*sc->ty = *ty;
				return sc->ty;
			}
		}
		// Otherwise, register the struct type.
		push_tag_scope(tag, ty);
	}

	return ty;
}

// struct-decl = struct-union-decl
static struct Type *struct_decl(struct Token **rest, struct Token *tok)
{
	struct Type *ty = struct_union_decl(rest, tok);
	ty->kind = TY_STRUCT;

	if (ty->size < 0)
		return ty;

	// Assign offsets within the struct to members
	int offset = 0;
	for (struct Member *mem = ty->members; mem; mem = mem->next) {
		offset = align_to(offset, mem->ty->align);
		mem->offset = offset;
		offset += mem->ty->size;

		if (ty->align < mem->ty->align)
			ty->align = mem->ty->align;
	}
	ty->size = align_to(offset, ty->align);

	return ty;
}

// union-decl = struct-union-decl
static struct Type *union_decl(struct Token **rest, struct Token *tok)
{
	struct Type *ty = struct_union_decl(rest, tok);
	ty->kind = TY_UNION;

	if (ty->size < 0)
		return ty;

	// If union, we don't have to assign offsets because they
	// are already initialized to 0.
	// We need to compute the alignment and the size though.
	for (struct Member *mem = ty->members; mem; mem = mem->next) {
		if (ty->align < mem->ty->align)
			ty->align = mem->ty->align;
		if (ty->size < mem->ty->size)
			ty->size = mem->ty->size;
	}
	ty->size = align_to(ty->size, ty->align);

	return ty;
}

static const char *get_ident(struct Token *tok)
{
	if (tok->kind != TK_IDENT)
		error_tok(tok, "expected an identifier");
	return strndup(tok->loc, tok->len);
}

// enum-specifier = ident? "{" enum-list? "}"
//		  | ident ("{" enum-list? "}")?
//
// enum-list      = ident ("=" num)? ("," ident ("=" num)?)*
static struct Type *enum_specifier(struct Token **rest, struct Token *tok)
{
	struct Type *ty = enum_type();

	// read a enum tag
	struct Token *tag = NULL;
	if (tok->kind == TK_IDENT) {
		tag = tok;
		tok = tok->next;
	}

	// enum variable
	if (tag && !equal(tok, "{")) {
		struct Type *ty = find_tag(tag);
		if (!ty)
			error_tok(tag, "unknown enum type");
		if (ty->kind != TY_ENUM)
			error_tok(tag, "not an enum tag");
		*rest = tok;
		return ty;
	}

	// enum definition
	tok = skip(tok, "{");

	// read an enum-list
	int i = 0, val = 0;
	while (!equal(tok, "}")) {
		if (i++ > 0)
			tok = skip(tok, ",");

		const char *name = get_ident(tok);
		tok = tok->next;

		if (equal(tok, "=")) {
			val = const_expr(&tok, tok->next);
		}

		struct VarScope *sc = push_scope(name);
		sc->enum_ty = ty;
		sc->enum_val = val++;
	}

	*rest = tok->next;

	if (tag)
		push_tag_scope(tag, ty);
	return ty;
}

// declspec = ("void" | "_Bool" | "char" | "short" | "int" | "long" |
//		struct-decl | union-decl | "typedef" | "static" |
//		typedef-name | enum-specifier)+
//
// The order of typenames in a type-specifier doesn't matter. For
// example, `int long static` means the same as `static long int`.
// That can also be written as `static long` because you can omit
// `int` if `long` or `short` are specified. However, something like
// `char int` is not a valid type specifier. We have to accept only a
// limited combinations of the typenames.
//
// In this function, we count the number of occurrences of each typename
// while keeping the "current" type object that the typenames up
// until that point represent. When we reach a non-typename token,
// we returns the current type object.
static struct Type *declspec(struct Token **rest, struct Token *tok,
			     struct VarAttr *attr)
{
	// We use a single integer as counters for all typenames.
	// For example, bits 0 and 1 represents how many times we saw the
	// keyword "void" so far. With this, we can use a switch statement
	// as you can see below.
	enum {
		VOID  = 1 << 0,
		BOOL  = 1 << 2,
		CHAR  = 1 << 4,
		SHORT = 1 << 6,
		INT   = 1 << 8,
		LONG  = 1 << 10,
		OTHER = 1 << 12,
	};

	// "typedef t" means "typedef int t"
	struct Type *ty = p_ty_int();
	int counter = 0;

	while (is_typename(tok)) {
		// handle "typedef" keyword or handle storage class specifiers
		if (equal(tok, "typedef") || equal(tok, "static")) {
			if (!attr)
				error_tok(tok, "storage class specifier is not allowed in this context");

			if (equal(tok, "typedef"))
				attr->is_typedef = true;
			else
				attr->is_static = true;

			if (attr->is_typedef + attr->is_static > 1)
				error_tok(tok, "typedef and static may not be used together");

			tok = tok->next;
			continue;
		}

		// Handle user-defined types.
		struct Type *ty2 = find_typedef(tok);
		if (equal(tok, "struct") || equal(tok, "union") ||
		    equal(tok, "enum") || ty2) {
			if (counter)
				break;

			if (equal(tok, "struct")) {
				ty = struct_decl(&tok, tok->next);

			} else if (equal(tok, "union")) {
				ty = union_decl(&tok, tok->next);

			} else if (equal(tok, "enum")) {
				ty = enum_specifier(&tok, tok->next);

			} else {
				ty = ty2;
				tok = tok->next;
			}
			counter += OTHER;
			continue;
		}

		// Handle built-in types.
		if (equal(tok, "void"))
			counter += VOID;
		else if (equal(tok, "_Bool"))
			counter += BOOL;
		else if (equal(tok, "char"))
			counter += CHAR;
		else if (equal(tok, "short"))
			counter += SHORT;
		else if (equal(tok, "int"))
			counter += INT;
		else if (equal(tok, "long"))
			counter += LONG;
		else
			unreachable();

		switch (counter) {
		case VOID:
			ty = p_ty_void();
			break;
		case BOOL:
			ty = p_ty_bool();
			break;
		case CHAR:
			ty = p_ty_char();
			break;
		case SHORT:
		case SHORT + INT:
			ty = p_ty_short();
			break;
		case INT:
			ty = p_ty_int();
			break;
		case LONG:
		case LONG + INT:
		case LONG + LONG:
		case LONG + LONG + INT:
			ty = p_ty_long();
			break;
		default:
			error_tok(tok, "invalid type");
			break;
		}

		tok = tok->next;
	}

	*rest = tok;
	return ty;
}

// func-params = (param ("," param)*)? ")"
// param = declspec declarator
static struct Type *func_params(struct Token **rest, struct Token *tok, struct Type *ty)
{
	struct Type head = {};
	struct Type *cur = &head;

	while (!equal(tok, ")")) {
		if (cur != &head)
			tok = skip(tok, ",");

		struct Type *basety = declspec(&tok, tok, NULL);
		struct Type *ty2 = declarator(&tok, tok, basety);

		if (ty2->kind == TY_ARRAY) {
			struct Token *name = ty2->name;
			ty2 = pointer_to(ty2->base);
			ty2->name = name;
		}

		cur->next = copy_type(ty2);
		cur = cur->next;
	}

	ty = func_type(ty);
	ty->params = head.next;

	*rest = tok->next;
	return ty;
}

// array-dimension = const-expr? "]" type-suffix
static struct Type *array_dimension(struct Token **rest, struct Token *tok,
				    struct Type *ty)
{
	if (equal(tok, "]")) {
		ty = type_suffix(rest, tok->next, ty);
		// set flag for incomplete array
		return array_of(ty, -1);
	}

	int sz = const_expr(&tok, tok);
	// skip "]"
	tok = skip(tok, "]");
	ty = type_suffix(rest, tok, ty);
	return array_of(ty, sz);
}

// type-suffix = "(" func-params
// 		| "[" array_dimension
// 		| NULL
static struct Type *type_suffix(struct Token **rest, struct Token *tok,
				struct Type *ty)
{
	// deal with function identifier
	if (equal(tok, "("))
		return func_params(rest, tok->next, ty);

	if (equal(tok, "["))
		return array_dimension(rest, tok->next, ty);

	*rest = tok;
	return ty;
}

// declarator = "*"* ("(" ident ")" | "(" declarator ")" | ident) (type-suffix)?
static struct Type *declarator(struct Token **rest, struct Token *tok,
			       struct Type *ty)
{
	while (consume(&tok, tok, "*"))
		ty = pointer_to(ty);

	if (equal(tok, "(")) {
		struct Token *start = tok->next;
		struct Type dummy = {};

		// get the token of type_suffix firstly
		declarator(&tok, start, &dummy);
		tok = skip(tok, ")");

		// get the type of declarator, and update 'rest'
		ty = type_suffix(rest, tok, ty);

		// process 'start' token, and keep the 'rest' & return it
		return declarator(&tok, start, ty);
	}

	if (tok->kind != TK_IDENT)
		error_tok(tok, "expected a variable name");

	// deal with after identifier
	ty = type_suffix(rest, tok->next, ty);
	ty->name = tok;
	return ty;
}

static struct Initializer *new_initializer(struct Type *ty)
{
	struct Initializer *init = malloc(sizeof(struct Initializer));

	init->ty = ty;
	if (ty->kind == TY_ARRAY) {
		init->children = malloc(ty->array_len * sizeof(struct Initializer *));

		for (int i = 0; i < ty->array_len; i++)
			init->children[i] = new_initializer(ty->base);
	}

	return init;
}

// initializer = "{" initializer ("," initializer)* "}"
//             | assign
static void initializer2(struct Token **rest, struct Token *tok,
			 struct Initializer *init)
{
	if (init->ty->kind == TY_ARRAY) {
		tok = skip(tok, "{");

		for (int i = 0; i < init->ty->array_len && !equal(tok, "}"); i++) {
			if (i > 0)
				tok = skip(tok, ",");
			initializer2(&tok, tok, init->children[i]);
		}

		*rest = skip(tok, "}");
		return;
	}

	init->expr = assign(rest, tok);
}

static struct Initializer *initializer(struct Token **rest, struct Token *tok,
				       struct Type *ty)
{
	// allocate initializer
	struct Initializer *init = new_initializer(ty);
	// assign expr to initializer
	initializer2(rest, tok, init);
	return init;
}

static struct Node *init_desg_expr(struct InitDesg *desg, struct Token *tok)
{
	// last one which is the variable self
	if (desg->var)
		return new_var_node(desg->var, tok);

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
			struct InitDesg desg2 = { desg, i, NULL };
			struct Node *rhs = create_lvar_init(init->children[i],
							    ty->base, &desg2, tok);
			// node, x[a] = expr
			node = new_binary(ND_COMMA, node, rhs, tok);
		}
		return node;
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
static struct Node *lvar_initializer(struct Token **rest, struct Token *tok,
				     struct Obj *var)
{
	struct Initializer *init = initializer(rest, tok, var->ty);
	struct InitDesg desg = { NULL, 0, var };

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

// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static struct Node *declaration(struct Token **rest, struct Token *tok,
				struct Type *basety)
{
	struct Node head = {}; // memset 'head' with 0
	struct Node *cur = &head;
	int i = 0;

	while (!equal(tok, ";")) {
		if (i++ > 0)
			tok = skip(tok, ",");

		struct Token *start = tok;
		struct Type *ty = declarator(&tok, tok, basety);
		if (ty->size < 0)
			error_tok(start, "variable has incomplete type");
		if (ty->kind == TY_VOID)
			error_tok(start, "variable declared void");

		struct Obj *var = new_lvar(get_ident(ty->name), ty);

		if (equal(tok, "=")) {
			// local variable initializer
			struct Node *expr = lvar_initializer(&tok, tok->next, var);
			// a series of expressions & statements
			cur = cur->next = new_unary(ND_EXPR_STMT, expr, tok);
		}
	}

	// might empty block here
	struct Node *node = new_node(ND_BLOCK, tok);
	node->body = head.next;

	*rest = tok->next;
	return node;
}

// Lists of all goto
static struct Node *gotos;
static struct Node *labels;

// current "goto" jump target
static const char *brk_label;
// current "continue" jump target
static const char *cont_label;

// Points to a node representing a switch if we are parsing
// a switch statement. Otherwise, NULL.
static struct Node *current_switch;

// stmt = "return" expr ";"
// 	| "if" "(" expr ")" stmt ("else" stmt)?
//	| "switch" "(" expr ")" stmt
//	| "case" const-expr ":" stmt
//	| "default" ":" stmt
// 	| "for" "(" expr-stmt expr? ";" expr? ")" stmt
// 	| "while" "(" expr ")" stmt
// 	| "goto" ident ";"
// 	| "break" ";"
// 	| "continue" ";"
// 	| ident ":" stmt
// 	| "{" compound-stmt
// 	| expr_stmt
static struct Node *stmt(struct Token **rest, struct Token *tok)
{
	if (equal(tok, "return")) {
		struct Node *node = new_node(ND_RETURN, tok);
		struct Node *exp = expr(&tok, tok->next);
		*rest = skip(tok, ";");

		node->lhs = new_cast(exp, current_fn->ty->return_ty);
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

	if (equal(tok, "switch")) {
		struct Node *n = new_node(ND_SWITCH, tok);

		tok = skip(tok->next, "(");
		n->cond = expr(&tok, tok);
		tok = skip(tok, ")");

		struct Node *sw_prev = current_switch;
		current_switch = n;

		const char *brk_prev = brk_label;
		brk_label = n->brk_label = new_unique_name();

		// a series of "case"s
		n->then = stmt(rest, tok);

		current_switch = sw_prev;
		brk_label = brk_prev;
		return n;
	}

	if (equal(tok, "case")) {
		if (!current_switch)
			error_tok(tok, "stray case");

		struct Node *n = new_node(ND_CASE, tok);
		int val = const_expr(&tok, tok->next);
		tok = skip(tok, ":");

		n->label = new_unique_name();
		n->lhs = stmt(rest, tok);
		n->val = val;
		n->case_next = current_switch->case_next;
		current_switch->case_next = n;
		return n;
	}

	if (equal(tok, "default")) {
		if (!current_switch)
			error_tok(tok, "stray default");

		struct Node *n = new_node(ND_CASE, tok);

		tok = skip(tok->next, ":");
		n->label = new_unique_name();
		n->lhs = stmt(rest, tok);
		current_switch->default_case = n;
		return n;
	}

	if (equal(tok, "for")) {
		struct Node *n = new_node(ND_FOR, tok);

		tok = skip(tok->next, "(");

		enter_scope();

		const char *brk_prev = brk_label;
		const char *cont_prev = cont_label;
		brk_label = n->brk_label = new_unique_name();
		cont_label = n->cont_label = new_unique_name();

		if (is_typename(tok)) {
			struct Type *basety = declspec(&tok, tok, NULL);
			n->init = declaration(&tok, tok, basety);
		} else {
			n->init = expr_stmt(&tok, tok);
		}

		if (!equal(tok, ";"))
			n->cond = expr(&tok, tok);
		tok = skip(tok, ";");

		if (!equal(tok, ")"))
			n->inc = expr(&tok, tok);
		tok = skip(tok, ")");

		n->then = stmt(rest, tok);

		leave_scope();

		brk_label = brk_prev;
		cont_label = cont_prev;
		return n;
	}

	if (equal(tok, "while")) {
		struct Node *n = new_node(ND_FOR, tok);

		tok = skip(tok->next, "(");
		n->cond = expr(&tok, tok);

		tok = skip(tok, ")");

		const char *brk_prev = brk_label;
		const char *cont_prev = cont_label;

		brk_label = n->brk_label = new_unique_name();
		cont_label = n->cont_label = new_unique_name();

		n->then = stmt(rest, tok);

		brk_label = brk_prev;
		cont_label = cont_prev;
		return n;
	}

	if (equal(tok, "goto")) {
		struct Node *node = new_node(ND_GOTO, tok);

		node->label = get_ident(tok->next);
		node->goto_next = gotos;
		gotos = node;
		*rest = skip(tok->next->next, ";");
		return node;
	}

	if (equal(tok, "break")) {
		if (!brk_label)
			error_tok(tok, "stray break");

		struct Node *node = new_node(ND_GOTO, tok);
		node->unique_label = brk_label;
		*rest = skip(tok->next, ";");
		return node;
	}

	if (equal(tok, "continue")) {
		if (!cont_label)
			error_tok(tok, "stray continue");

		struct Node *node = new_node(ND_GOTO, tok);
		node->unique_label = cont_label;
		*rest = skip(tok->next, ";");
		return node;
	}

	if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
		struct Node *node = new_node(ND_LABEL, tok);

		node->label = strndup(tok->loc, tok->len);
		node->unique_label = new_unique_name();
		// skip ":"
		node->lhs = stmt(rest, tok->next->next);
		node->goto_next = labels;
		labels = node;
		return node;
	}

	if (equal(tok, "{"))
		return compound_stmt(rest, tok->next);

	return expr_stmt(rest, tok);
}

static struct Token *parse_typedef(struct Token *tok, struct Type *basety)
{
	bool first = true;

	while (!consume(&tok, tok, ";")) {
		if (!first)
			tok = skip(tok, ",");
		first = false;

		struct Type *ty = declarator(&tok, tok, basety);
		push_scope(get_ident(ty->name))->type_def = ty;
	}

	return tok;
}

// compound-stmt = (declaration | stmt)* "}"
static struct Node *compound_stmt(struct Token **rest, struct Token *tok)
{
	struct Node head = {};
	struct Node *cur = &head;
	struct Node *node = new_node(ND_BLOCK, tok);

	enter_scope();
	while (!equal(tok, "}")) {
		// make sure it's not a label
		if (is_typename(tok) && !equal(tok->next, ":")) {
			// variable declaration
			struct VarAttr attr = {};
			struct Type *basety = declspec(&tok, tok, &attr);

			if (attr.is_typedef) {
				tok = parse_typedef(tok, basety);
				continue;
			}

			cur->next = declaration(&tok, tok, basety);
		} else {
			cur->next = stmt(&tok, tok);
		}
		cur = cur->next;
		add_type(cur);
	}
	node->body = head.next;
	leave_scope();

	// skip "}"
	*rest = tok->next;
	return node;
}

static void create_param_lvars(struct Type *param)
{
	if (param) {
		create_param_lvars(param->next);
		// locals -> arg1 -> arg2 -> ... -> argn
		new_lvar(get_ident(param->name), param);
	}
}

// This function matches gotos with labels.
//
// We cannot resolve gotos as we parse a function because gotos
// can refer a label that appears later in the function.
// So, we need to do this after we parse the entire function.
static void resolve_goto_labels(void)
{
	for (struct Node *x = gotos; x; x = x->goto_next) {
		for (struct Node *y = labels; y; y = y->goto_next) {
			if (!strcmp(x->label, y->label)) {
				x->unique_label = y->unique_label;
				break;
			}
		}

		if (!x->unique_label)
			error_tok(x->tok->next, "use of undeclared label");
	}

	// scope of function
	gotos = labels = NULL;
}

static struct Token *function(struct Token *tok, struct Type *basety,
			      const struct VarAttr *attr)
{
	struct Type *ty = declarator(&tok, tok, basety);

	struct Obj *fn = new_gvar(get_ident(ty->name), ty);
	fn->is_function = true;
	fn->is_definition = !consume(&tok, tok, ";");
	fn->is_static = attr->is_static;

	if (!fn->is_definition)
		return tok;

	current_fn = fn;

	// initialize local variables list
	locals = NULL;
	enter_scope();
	create_param_lvars(ty->params);
	fn->params = locals;

	tok = skip(tok, "{");

	fn->body = compound_stmt(&tok, tok);
	fn->locals = locals;
	leave_scope();

	resolve_goto_labels();
	return tok;
}

static struct Token *global_variable(struct Token *tok, struct Type *basety)
{
	bool first = true;

	while (!consume(&tok, tok, ";")) {
		if (!first)
			tok = skip(tok, ",");
		first = false;

		struct Type *ty = declarator(&tok, tok, basety);
		new_gvar(get_ident(ty->name), ty);
	}
	return tok;
}

static bool is_function(struct Token *tok)
{
	if (equal(tok, ";"))
		return false;

	struct Type dummy = {};
	struct Type *ty = declarator(&tok, tok, &dummy);

	return ty->kind == TY_FUNC;
}

// program = (typedef | function-definition | global-variable)*
struct Obj *parser(struct Token *tok)
{
	globals = NULL;

	while (tok->kind != TK_EOF) {
		struct VarAttr attr = {};
		struct Type *basety = declspec(&tok, tok, &attr);

		// typedef
		if (attr.is_typedef) {
			tok = parse_typedef(tok, basety);
			continue;
		}

		if (is_function(tok)) {
			// function
			tok = function(tok, basety, &attr);
		} else {
			// global variable
			tok = global_variable(tok, basety);
		}
	}

	return globals;
}
