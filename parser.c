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

// scope for local or global variables or typedefs
struct VarScope {
	struct VarScope *next;
	const char *name;
	struct Obj *var;
	struct Type *type_def;
};

// scope for struct tags or union tags
struct TagScope {
	struct TagScope *next;
	const char *name;
	struct Type *ty;
};

// represents a block scope
struct Scope {
	struct Scope *next;

	// C has two block scopes:
	// one is for variables
	// and the other is for struct tags.
	struct VarScope *vars;
	struct TagScope *tags;
};

// Variable attributes such as typedef or extern.
struct VarAttr {
	bool is_typedef;
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
		"char",
		"short",
		"int",
		"long",
		"struct",
		"union",
		"typedef",
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

		// variable
		struct VarScope *sc = find_var(tok);
		if (!sc || !sc->var)
			error_tok(tok, "undefined variable");
		*rest = tok->next;
		return new_var_node(sc->var, tok);
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

// postfix = primary ("[" expr "]" | "." ident | "->" ident)*
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

// unary = ("+" | "-" | "*" | "&") cast
// 		| postfix
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

	return postfix(rest, tok);
}

// mul = cast ("*" cast | "/" cast)*
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

static int get_number(struct Token *tok)
{
	if (tok->kind != TK_NUM)
		error_tok(tok, "expected a number");
	return tok->val;
}

static struct Type *declspec(struct Token **rest, struct Token *tok, struct VarAttr *attr);
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
	// read a tag
	struct Token *tag = NULL;

	if (tok->kind == TK_IDENT) {
		tag = tok;
		tok = tok->next;
	}

	// var return
	if (tag && !equal(tok, "{")) {
		struct Type *ty = find_tag(tag);
		if (!ty)
			error_tok(tag, "unknown struct type");

		*rest = tok;
		return ty;
	}

	// Construct a struct object
	struct Type *ty = malloc(sizeof(struct Type));
	ty->kind = TY_STRUCT;
	// skip "{"
	struct_members(rest, tok->next, ty);
	ty->align = 1;

	// register the struct type if a name was given
	if (tag)
		push_tag_scope(tag, ty);
	return ty;
}

// struct-decl = struct-union-decl
static struct Type *struct_decl(struct Token **rest, struct Token *tok)
{
	struct Type *ty = struct_union_decl(rest, tok);
	ty->kind = TY_STRUCT;

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

// declspec = ("void" | "char" | "short" | "int" | "long" |
//		struct-decl | union-decl |
//		"typedef" | typedef-name)+
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
		CHAR  = 1 << 2,
		SHORT = 1 << 4,
		INT   = 1 << 6,
		LONG  = 1 << 8,
		OTHER = 1 << 10,
	};

	// "typedef t" means "typedef int t"
	struct Type *ty = p_ty_int();
	int counter = 0;

	while (is_typename(tok)) {
		// handle "typedef" keyword
		if (equal(tok, "typedef")) {
			if (!attr)
				error_tok(tok, "storage class specifier is not allowed in this context");
			attr->is_typedef = true;
			tok = tok->next;
			continue;
		}

		// Handle user-defined types.
		struct Type *ty2 = find_typedef(tok);
		if (equal(tok, "struct") || equal(tok, "union") || ty2) {
			if (counter)
				break;

			if (equal(tok, "struct")) {
				ty = struct_decl(&tok, tok->next);

			} else if (equal(tok, "union")) {
				ty = union_decl(&tok, tok->next);

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
		struct Type *tmp_ty = declarator(&tok, tok, basety);
		cur->next = copy_type(tmp_ty);
		cur = cur->next;
	}

	ty = func_type(ty);
	ty->params = head.next;

	*rest = tok->next;
	return ty;
}

// type-suffix = "(" func-params
// 		| "[" num "]" type_suffix
// 		| NULL
static struct Type *type_suffix(struct Token **rest, struct Token *tok,
				struct Type *ty)
{
	// deal with function identifier
	if (equal(tok, "("))
		return func_params(rest, tok->next, ty);

	if (equal(tok, "[")) {
		int sz = get_number(tok->next);

		// skip "]"
		tok = skip(tok->next->next, "]");
		ty = type_suffix(rest, tok, ty);
		return array_of(ty, sz);
	}

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

static const char *get_ident(struct Token *tok)
{
	if (tok->kind != TK_IDENT)
		error_tok(tok, "expected an identifier");
	return strndup(tok->loc, tok->len);
}

// declaration = declspec (declarator ("=" expr)? ("," declarator ("=" expr)?)*)? ";"
static struct Node *declaration(struct Token **rest, struct Token *tok, struct Type *basety)
{
	struct Node head = {}; // memset 'head' with 0
	struct Node *cur = &head;
	int i = 0;

	while (!equal(tok, ";")) {
		if (i++ > 0)
			tok = skip(tok, ",");

		struct Type *ty = declarator(&tok, tok, basety);
		if (ty->kind == TY_VOID)
			error_tok(tok, "variable declared void");

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
		if (is_typename(tok)) {
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

static struct Token *function(struct Token *tok, struct Type *basety)
{
	struct Type *ty = declarator(&tok, tok, basety);

	struct Obj *fn = new_gvar(get_ident(ty->name), ty);
	fn->is_function = true;
	fn->is_definition = !consume(&tok, tok, ";");

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
			tok = function(tok, basety);
		} else {
			// global variable
			tok = global_variable(tok, basety);
		}
	}

	return globals;
}
